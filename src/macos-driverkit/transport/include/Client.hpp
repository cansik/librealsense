// License: Apache 2.0. See LICENSE file in root directory.

#pragma once
#include "RealSenseUSBABI.h"
#include "Descriptors.hpp"
#include <functional>
#include <memory>
#include <string>
namespace rsmacos {
struct Device {
    RSUSBDeviceInfo info{};
    std::string name, serial;
    std::vector<uint8_t> descriptors;
};
std::vector<Device> enumerate();
class Error : public std::runtime_error {
public:
    uint32_t code;
    Error(std::string operation, uint32_t code);
};
class Client {
public:
    using Completion = std::function<void(uint32_t status, uint32_t actual, uint64_t timestamp)>;
    static std::shared_ptr<Client> open(uint64_t registryID, RSUSBMode mode = RSUSBSDK);
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    uint32_t control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
        uint8_t *buffer, uint32_t length, uint32_t &actual, uint32_t timeout);
    uint32_t submit(uint8_t interface, uint8_t endpoint, uint8_t *buffer,
        uint32_t length, uint32_t timeout, Completion completion);
    uint32_t abort(uint8_t interface, uint8_t endpoint);
    uint32_t clearStall(uint8_t interface, uint8_t endpoint);
    uint32_t alternate(uint8_t interface, uint8_t setting);
    const Device &device() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Client(uint64_t registryID, RSUSBMode mode);
};
}
