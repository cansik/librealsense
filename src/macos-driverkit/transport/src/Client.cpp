// License: Apache 2.0. See LICENSE file in root directory.

#include "Client.hpp"
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <utility>
namespace rsmacos {
Error::Error(std::string operation, uint32_t value) : std::runtime_error([&] {
    std::ostringstream out; out << operation << " failed (0x" << std::hex << value << ")"; return out.str();
}()), code(value) {}
static void check(kern_return_t status, const char *operation) { if (status) throw Error(operation, status); }
struct Service {
    io_object_t value = 0;
    explicit Service(io_object_t value = 0) : value(value) {}
    ~Service() { if (value) IOObjectRelease(value); }
};
static std::string property(io_registry_entry_t service, CFStringRef key) {
    auto value = IORegistryEntrySearchCFProperty(service, kIOServicePlane, key, kCFAllocatorDefault,
        kIORegistryIterateRecursively | kIORegistryIterateParents);
    if (!value) return {};
    char bytes[512]{};
    if (CFGetTypeID(value) == CFStringGetTypeID()) CFStringGetCString(static_cast<CFStringRef>(value), bytes, sizeof(bytes), kCFStringEncodingUTF8);
    CFRelease(value); return bytes;
}
static Device snapshot(io_service_t service, io_connect_t connection) {
    Device result;
    size_t size = sizeof(result.info);
    check(IOConnectCallStructMethod(connection, RSUSBGetInfo, nullptr, 0, &result.info, &size), "Read USB identity");
    if (size != sizeof(result.info) || result.info.version != RSUSB_ABI_VERSION) throw std::runtime_error("Driver ABI mismatch");
    result.descriptors.resize(result.info.descriptorLength);
    size = result.descriptors.size();
    check(IOConnectCallStructMethod(connection, RSUSBGetDescriptors, nullptr, 0, result.descriptors.data(), &size), "Read USB descriptors");
    if (size != result.descriptors.size()) throw std::runtime_error("Driver descriptor length mismatch");
    parseDescriptors(result.descriptors);
    result.name = property(service, CFSTR("USB Product Name"));
    result.serial = property(service, CFSTR("USB Serial Number"));
    return result;
}
std::vector<Device> enumerate() {
    auto matching = IOServiceMatching("IOUserService");
    if (!matching) throw std::bad_alloc();
    auto properties = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(properties, CFSTR("IOUserClass"), CFSTR("RealSenseUSBDriver"));
    CFDictionarySetValue(matching, CFSTR("IOPropertyMatch"), properties); CFRelease(properties);
    io_iterator_t iterator = 0;
    check(IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator), "Enumerate drivers");
    Service ownedIterator(iterator);
    std::vector<Device> devices;
    while (auto entry = IOIteratorNext(iterator)) {
        Service service(entry);
        io_connect_t connection = 0;
        auto status = IOServiceOpen(entry, mach_task_self(), 0, &connection);
        if (status == kIOReturnNoDevice || status == kIOReturnOffline) continue;
        if (status) throw Error("Open driver for status", status);
        try { devices.push_back(snapshot(entry, connection)); }
        catch (const Error &error) {
            IOServiceClose(connection);
            if (static_cast<IOReturn>(error.code) == kIOReturnNoDevice || static_cast<IOReturn>(error.code) == kIOReturnOffline) continue;
            throw;
        }
        catch (...) { IOServiceClose(connection); throw; }
        IOServiceClose(connection);
    }
    return devices;
}
struct Client::Impl {
    struct Pending { Impl *owner; Completion completion; };
    io_connect_t connection = 0;
    IONotificationPortRef port = nullptr;
    CFRunLoopRef loop = nullptr;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable condition;
    std::map<Pending *, std::unique_ptr<Pending>> pending;
    std::set<std::pair<uint8_t, uint8_t>> endpoints;
    Device device;
    bool stopping = false;
    uint32_t callbacks = 0;
    bool ready = false;
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex); stopping = true;
        }
        for (auto endpoint : endpoints) {
            uint64_t in[]{endpoint.first, endpoint.second};
            IOConnectCallScalarMethod(connection, RSUSBAbort, in, 2, nullptr, nullptr);
        }
        if (thread.joinable()) {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] { return pending.empty() && callbacks == 0; });
            if (loop) { CFRunLoopStop(loop); CFRunLoopWakeUp(loop); }
            lock.unlock(); thread.join();
        }
        if (connection) IOServiceClose(connection);
    }
    void run() {
        port = IONotificationPortCreate(kIOMainPortDefault);
        loop = CFRunLoopGetCurrent();
        if (port) CFRunLoopAddSource(loop, IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
        {
            std::lock_guard<std::mutex> lock(mutex); ready = true; condition.notify_all();
        }
        if (port) {
            while (true) {
                { std::lock_guard<std::mutex> lock(mutex); if (stopping) break; }
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, false);
            }
        }
        if (port) { IONotificationPortDestroy(port); port = nullptr; }
    }
    static void completed(void *reference, IOReturn status, void *actual, void *timestamp) {
        auto request = static_cast<Pending *>(reference); auto owner = request->owner;
        Completion callback;
        {
            std::lock_guard<std::mutex> lock(owner->mutex);
            auto found = owner->pending.find(request);
            if (found == owner->pending.end()) return;
            ++owner->callbacks;
            callback = std::move(found->second->completion);
            owner->pending.erase(found);
        }
        try { callback(status, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actual)),
            reinterpret_cast<uintptr_t>(timestamp)); }
        catch (...) { /* Never unwind through an IOKit callback. */ }
        { std::lock_guard<std::mutex> lock(owner->mutex); --owner->callbacks; }
        owner->condition.notify_all();
    }
};
Client::Client(uint64_t registryID, RSUSBMode mode) : impl(std::make_unique<Impl>()) {
    Service service(IOServiceGetMatchingService(kIOMainPortDefault, IORegistryEntryIDMatching(registryID)));
    if (!service.value) throw Error("Find camera", kIOReturnNoDevice);
    check(IOServiceOpen(service.value, mach_task_self(), 0, &impl->connection), "Connect to camera");
    impl->device = snapshot(service.value, impl->connection);
    uint64_t in[]{RSUSB_ABI_VERSION, static_cast<uint64_t>(mode)};
    check(IOConnectCallScalarMethod(impl->connection, RSUSBAcquire, in, 2, nullptr, nullptr), "Acquire camera");
    impl->thread = std::thread([this] { impl->run(); });
    std::unique_lock<std::mutex> lock(impl->mutex);
    impl->condition.wait(lock, [this] { return impl->ready; });
    if (!impl->port) throw Error("Create USB completion port", kIOReturnNoResources);
}
Client::~Client() = default;
std::shared_ptr<Client> Client::open(uint64_t registryID, RSUSBMode mode) {
    static std::mutex mutex;
    static std::map<std::pair<uint64_t, RSUSBMode>, std::weak_ptr<Client>> clients;
    std::lock_guard<std::mutex> lock(mutex);
    auto &weak = clients[{registryID, mode}];
    if (auto existing = weak.lock()) return existing;
    auto client = std::shared_ptr<Client>(new Client(registryID, mode)); weak = client; return client;
}
const Device &Client::device() const { return impl->device; }
uint32_t Client::control(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint8_t *buffer,
    uint32_t length, uint32_t &actual, uint32_t timeout) {
    uint64_t in[]{type, request, value, index, reinterpret_cast<uint64_t>(buffer), length, timeout}, out = 0;
    uint32_t count = 1;
    auto status = IOConnectCallScalarMethod(impl->connection, RSUSBControl, in, 7, &out, &count);
    actual = static_cast<uint32_t>(out); return status;
}
uint32_t Client::submit(uint8_t interface, uint8_t endpoint, uint8_t *buffer, uint32_t length,
    uint32_t timeout, Completion completion) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->stopping) return kIOReturnNoDevice;
    if (!buffer || !length || length > RSUSB_MAX_TRANSFER || !completion) return kIOReturnBadArgument;
    auto request = std::make_unique<Impl::Pending>(Impl::Pending{impl.get(), std::move(completion)});
    auto reference = request.get(); impl->pending.emplace(reference, std::move(request));
    impl->endpoints.emplace(interface, endpoint);
    io_async_ref64_t async{};
    async[kIOAsyncCalloutFuncIndex] = reinterpret_cast<uint64_t>(&Impl::completed);
    async[kIOAsyncCalloutRefconIndex] = reinterpret_cast<uint64_t>(reference);
    uint64_t in[]{interface, endpoint, reinterpret_cast<uint64_t>(buffer), length, timeout};
    auto status = IOConnectCallAsyncScalarMethod(impl->connection, RSUSBTransfer,
        IONotificationPortGetMachPort(impl->port), async, kIOAsyncCalloutCount, in, 5, nullptr, nullptr);
    if (status) { impl->pending.erase(reference); impl->condition.notify_all(); }
    return status;
}
uint32_t Client::abort(uint8_t interface, uint8_t endpoint) {
    uint64_t in[]{interface, endpoint}; return IOConnectCallScalarMethod(impl->connection, RSUSBAbort, in, 2, nullptr, nullptr);
}
uint32_t Client::clearStall(uint8_t interface, uint8_t endpoint) {
    uint64_t in[]{interface, endpoint}; return IOConnectCallScalarMethod(impl->connection, RSUSBClearStall, in, 2, nullptr, nullptr);
}
uint32_t Client::alternate(uint8_t interface, uint8_t setting) {
    uint64_t in[]{interface, setting}; return IOConnectCallScalarMethod(impl->connection, RSUSBSetAlternate, in, 2, nullptr, nullptr);
}
}
