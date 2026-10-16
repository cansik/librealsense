// License: Apache 2.0. See LICENSE file in root directory.

#include "Descriptors.hpp"
#include <set>
namespace rsmacos {
static uint16_t word(const std::vector<uint8_t> &b, size_t n) { return b.at(n) | uint16_t(b.at(n+1)) << 8; }
Configuration parseDescriptors(const std::vector<uint8_t> &bytes) {
    if (bytes.size() < 27 || bytes[0] != 18 || bytes[1] != 1 || bytes[18] < 9 || bytes[19] != 2)
        throw std::invalid_argument("Missing USB device/configuration descriptors");
    if (word(bytes, 20) != bytes.size() - 18) throw std::invalid_argument("USB configuration length mismatch");
    Configuration config{word(bytes, 8), word(bytes, 10), word(bytes, 2), {}, {}};
    Interface *current = nullptr;
    std::set<unsigned> settings;
    std::set<unsigned> numbers;
    unsigned expectedEndpoints = 0;
    for (size_t offset = 0; offset < bytes.size();) {
        if (bytes.size() - offset < 2 || bytes[offset] < 2 || bytes[offset] > bytes.size() - offset)
            throw std::invalid_argument("Truncated USB descriptor");
        auto length = bytes[offset]; auto type = bytes[offset + 1];
        std::vector<uint8_t> data(bytes.begin()+offset, bytes.begin()+offset+length);
        config.descriptors.push_back({type, data});
        if (type == 4) {
            if (length < 9) throw std::invalid_argument("Short USB interface descriptor");
            if (current && current->endpoints.size() != expectedEndpoints)
                throw std::invalid_argument("USB endpoint count mismatch");
            if (!settings.insert((unsigned(data[2]) << 8) | data[3]).second)
                throw std::invalid_argument("Duplicate USB interface/alternate setting");
            numbers.insert(data[2]);
            config.interfaces.push_back({data[2], data[3], data[5], data[6], {}});
            current = &config.interfaces.back(); expectedEndpoints = data[4];
        } else if (type == 5) {
            if (length < 7 || !current || !(data[2] & 15) || (data[2] & 0x70))
                throw std::invalid_argument("Invalid USB endpoint descriptor");
            for (auto &ep : current->endpoints) if (ep.address == data[2])
                throw std::invalid_argument("Duplicate USB endpoint address");
            current->endpoints.push_back({data[2], uint8_t(data[3] & 3), current->number, word(data, 4)});
        }
        offset += length;
    }
    if (current && current->endpoints.size() != expectedEndpoints) throw std::invalid_argument("USB endpoint count mismatch");
    if (numbers.size() != bytes[22]) throw std::invalid_argument("USB interface count mismatch");
    return config;
}
}
