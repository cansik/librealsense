// License: Apache 2.0. See LICENSE file in root directory.

#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
namespace rsmacos {
struct Descriptor { uint8_t type; std::vector<uint8_t> bytes; };
struct Endpoint { uint8_t address, type, interface; uint16_t packetSize; };
struct Interface { uint8_t number, alternate, cls, subclass; std::vector<Endpoint> endpoints; };
struct Configuration {
    uint16_t vendor, product, usbVersion;
    std::vector<Descriptor> descriptors;
    std::vector<Interface> interfaces;
};
Configuration parseDescriptors(const std::vector<uint8_t> &bytes);
}
