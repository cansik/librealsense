// License: Apache 2.0. See LICENSE file in root directory.
// macOS USBDriverKit transport. All RealSense protocols remain in RSUSB.
#include "usb/usb-enumerator.h"
#include "transport/include/Client.hpp"
#include <IOKit/IOReturn.h>
#include <IOKit/usb/USB.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>

namespace librealsense { namespace platform {
namespace {
usb_status status(uint32_t value) {
    switch (static_cast<IOReturn>(value)) {
    case kIOReturnSuccess: return RS2_USB_STATUS_SUCCESS;
    case kIOReturnBadArgument: return RS2_USB_STATUS_INVALID_PARAM;
    case kIOReturnNotPermitted: case kIOReturnNotPrivileged: return RS2_USB_STATUS_ACCESS;
    case kIOReturnNoDevice: case kIOReturnOffline: return RS2_USB_STATUS_NO_DEVICE;
    case kIOReturnNotFound: return RS2_USB_STATUS_NOT_FOUND;
    case kIOReturnBusy: case kIOReturnExclusiveAccess: return RS2_USB_STATUS_BUSY;
    case kIOReturnTimeout: return RS2_USB_STATUS_TIMEOUT;
    case kIOReturnOverrun: return RS2_USB_STATUS_OVERFLOW;
    case kIOUSBPipeStalled: return RS2_USB_STATUS_PIPE;
    case kIOReturnAborted: return RS2_USB_STATUS_INTERRUPTED;
    case kIOReturnNoMemory: return RS2_USB_STATUS_NO_MEM;
    case kIOReturnUnsupported: return RS2_USB_STATUS_NOT_SUPPORTED;
    default: return RS2_USB_STATUS_IO;
    }
}
RSUSBMode client_mode() {
    const auto mode = CFBundleGetValueForInfoDictionaryKey(CFBundleGetMainBundle(), CFSTR("RealSenseUSBMode"));
    return mode && CFGetTypeID(mode) == CFStringGetTypeID() && CFEqual(mode, CFSTR("camera")) ? RSUSBCamera : RSUSBSDK;
}
class endpoint final : public usb_endpoint {
    rsmacos::Endpoint _value;
public:
    explicit endpoint(rsmacos::Endpoint value) : _value(value) {}
    uint8_t get_address() const override { return _value.address; }
    endpoint_type get_type() const override { return endpoint_type(_value.type); }
    endpoint_direction get_direction() const override { return endpoint_direction(_value.address & 128); }
    uint8_t get_interface_number() const override { return _value.interface; }
};
class interface final : public usb_interface {
    rsmacos::Interface _value;
    std::vector<rs_usb_endpoint> _endpoints;
public:
    explicit interface(rsmacos::Interface value) : _value(value) {
        for (auto &ep : value.endpoints) _endpoints.push_back(std::make_shared<endpoint>(ep));
    }
    uint8_t get_number() const override { return _value.number; }
    uint8_t get_class() const override { return _value.cls; }
    uint8_t get_subclass() const override { return _value.subclass; }
    const std::vector<rs_usb_endpoint> get_endpoints() const override { return _endpoints; }
    const rs_usb_endpoint first_endpoint(endpoint_direction direction, endpoint_type type) const override {
        for (auto &ep : _endpoints) if (ep->get_direction() == direction && ep->get_type() == type) return ep;
        return nullptr;
    }
};
class request final : public usb_request_base {
public:
    std::atomic<int> actual{0};
    std::mutex mutex;
    std::condition_variable condition;
    bool active = false;
    bool cancelled = false;
    explicit request(rs_usb_endpoint ep) { _endpoint = std::move(ep); _client_data = nullptr; }
    int get_actual_length() const override { return actual; }
    void *get_native_request() const override { return const_cast<request *>(this); }
protected:
    void set_native_buffer_length(int) override {}
    int get_native_buffer_length() override { return static_cast<int>(_buffer.size()); }
    void set_native_buffer(uint8_t *) override {}
    uint8_t *get_native_buffer() const override { return const_cast<uint8_t *>(_buffer.data()); }
};
class messenger final : public usb_messenger {
    std::shared_ptr<rsmacos::Client> _client;
public:
    explicit messenger(std::shared_ptr<rsmacos::Client> client) : _client(std::move(client)) {}
    usb_status control_transfer(int type, int req, int value, int index, uint8_t *buffer, uint32_t length,
        uint32_t &actual, uint32_t timeout) override {
        actual = 0;
        if (type < 0 || type > 255 || req < 0 || req > 255 || value < 0 || value > 65535 || index < 0 || index > 65535)
            return RS2_USB_STATUS_INVALID_PARAM;
        return status(_client->control(type, req, value, index, buffer, length, actual, timeout));
    }
    usb_status bulk_transfer(const rs_usb_endpoint &ep, uint8_t *buffer, uint32_t length, uint32_t &actual,
        uint32_t timeout) override {
        std::mutex mutex; std::condition_variable condition; bool done = false; uint32_t result = 0;
        actual = 0;
        auto submitted = _client->submit(ep->get_interface_number(), ep->get_address(), buffer, length,
            ep->get_type() == RS2_USB_ENDPOINT_INTERRUPT ? 0 : timeout,
            [&](uint32_t code, uint32_t bytes, uint64_t) {
                std::lock_guard<std::mutex> lock(mutex); result = code; actual = bytes; done = true; condition.notify_all();
            });
        if (submitted) return status(submitted);
        std::unique_lock<std::mutex> lock(mutex); condition.wait(lock, [&] { return done; });
        return status(result);
    }
    usb_status reset_endpoint(const rs_usb_endpoint &ep, uint32_t) override {
        return status(_client->clearStall(ep->get_interface_number(), ep->get_address()));
    }
    rs_usb_request create_request(rs_usb_endpoint ep) override { return std::make_shared<request>(std::move(ep)); }
    usb_status submit_request(const rs_usb_request &value) override {
        auto req = std::dynamic_pointer_cast<request>(value);
        if (!req || req->get_buffer().empty()) return RS2_USB_STATUS_INVALID_PARAM;
        std::lock_guard<std::mutex> lock(req->mutex);
        if (req->active) return RS2_USB_STATUS_BUSY;
        req->active = true; req->cancelled = false; req->actual = 0;
        auto ep = req->get_endpoint();
        auto result = _client->submit(ep->get_interface_number(), ep->get_address(),
            const_cast<uint8_t *>(req->get_buffer().data()), req->get_buffer().size(),
            ep->get_type() == RS2_USB_ENDPOINT_INTERRUPT ? 0 : 5000,
            [req](uint32_t code, uint32_t bytes, uint64_t) {
                rs_usb_request_callback callback;
                {
                    std::lock_guard<std::mutex> lock(req->mutex);
                    req->actual = code ? 0 : static_cast<int>(bytes); req->active = false;
                    if (!req->cancelled && code != kIOReturnAborted && code != kIOReturnNoDevice) callback = req->get_callback();
                    req->condition.notify_all();
                }
                if (callback) callback->callback(req);
            });
        if (result) { req->active = false; req->condition.notify_all(); }
        return status(result);
    }
    usb_status cancel_request(const rs_usb_request &value) override {
        auto req = std::dynamic_pointer_cast<request>(value);
        if (!req) return RS2_USB_STATUS_INVALID_PARAM;
        {
            std::lock_guard<std::mutex> lock(req->mutex); req->cancelled = true;
            if (!req->active) return RS2_USB_STATUS_SUCCESS;
        }
        auto ep = req->get_endpoint();
        auto result = _client->abort(ep->get_interface_number(), ep->get_address());
        std::unique_lock<std::mutex> lock(req->mutex);
        req->condition.wait(lock, [&] { return !req->active; });
        return status(result);
    }
};
class device final : public usb_device {
    rsmacos::Device _device;
    usb_device_info _info;
    std::vector<rs_usb_interface> _interfaces;
    std::vector<usb_descriptor> _descriptors;
public:
    device(rsmacos::Device value, usb_device_info info) : _device(std::move(value)), _info(std::move(info)) {
        auto config = rsmacos::parseDescriptors(_device.descriptors);
        for (auto &inf : config.interfaces) if (inf.alternate == 0) _interfaces.push_back(std::make_shared<interface>(inf));
        for (auto &desc : config.descriptors) _descriptors.push_back({uint8_t(desc.bytes.size()), desc.type, desc.bytes});
    }
    const usb_device_info get_info() const override { return _info; }
    const std::vector<rs_usb_interface> get_interfaces() const override { return _interfaces; }
    const rs_usb_interface get_interface(uint8_t number) const override {
        for (auto &inf : _interfaces) if (inf->get_number() == number) return inf;
        return nullptr;
    }
    const rs_usb_messenger open(uint8_t number) override {
        if (!get_interface(number)) return nullptr;
        try { return std::make_shared<messenger>(rsmacos::Client::open(_device.info.registryID, client_mode())); }
        catch (const std::exception &) { return nullptr; }
    }
    const std::vector<usb_descriptor> get_descriptors() const override { return _descriptors; }
};
}
std::vector<usb_device_info> usb_enumerator::query_devices_info() {
    std::vector<usb_device_info> results;
    for (auto &dev : rsmacos::enumerate()) {
        if (dev.info.mode != RSUSBIdle && dev.info.mode != client_mode()) continue;
        auto config = rsmacos::parseDescriptors(dev.descriptors);
        for (auto &inf : config.interfaces) {
            if (inf.alternate || (inf.cls == RS2_USB_CLASS_VIDEO && inf.subclass == RS2_USB_SUBCLASS_VIDEO_STREAMING)
                || inf.cls == RS2_USB_CLASS_APPLICATION_SPECIFIC) continue;
            usb_device_info info{};
            info.id = std::to_string(dev.info.registryID); info.unique_id = info.id;
            info.serial = dev.serial; info.vid = config.vendor; info.pid = config.product;
            info.mi = inf.number; info.cls = usb_class(inf.cls); info.conn_spec = usb_spec(config.usbVersion);
            results.push_back(info);
        }
    }
    return results;
}
rs_usb_device usb_enumerator::create_usb_device(const usb_device_info &info) {
    for (auto &dev : rsmacos::enumerate()) if (std::to_string(dev.info.registryID) == info.id)
        return std::make_shared<device>(std::move(dev), info);
    return nullptr;
}
}}
