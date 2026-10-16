// License: Apache 2.0. See LICENSE file in root directory.

#pragma once
#include "RealSenseUSBABI.h"
namespace rsmacos {
struct Ownership {
    const void *owner = nullptr;
    uint32_t mode = RSUSBIdle;
    bool disconnected = false;
    bool acquire(const void *client, uint32_t requested) {
        if (!client || disconnected || (requested != RSUSBSDK && requested != RSUSBCamera)) return false;
        if (owner && (owner != client || mode != requested)) return false;
        owner = client; mode = requested; return true;
    }
    void release(const void *client) {
        if (owner == client) { owner = nullptr; mode = RSUSBIdle; }
    }
    bool owns(const void *client) const { return client && !disconnected && owner == client; }
    void disconnect() { disconnected = true; owner = nullptr; mode = RSUSBIdle; }
};
inline bool validTransfer(uint64_t interface, uint64_t endpoint, uint64_t address, uint64_t length,
    uint64_t timeout, uint8_t type) {
    return interface < RSUSB_MAX_INTERFACES && endpoint <= 255 && (endpoint & 15) && !(endpoint & 0x70)
        && address && length && length <= RSUSB_MAX_TRANSFER && address + length >= address
        && timeout <= 60000 && (type == 2 || (type == 3 && timeout == 0));
}
}
