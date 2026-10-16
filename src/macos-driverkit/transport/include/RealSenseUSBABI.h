// License: Apache 2.0. See LICENSE file in root directory.

#pragma once
#include <stdint.h>

// Fixed-width, versioned wire protocol shared by DriverKit and its clients.
// All reserved fields must be zero. Addresses are validated/mapped by IOUserClient.
enum { RSUSB_ABI_VERSION = 1, RSUSB_MAX_TRANSFER = 16 * 1024 * 1024,
       RSUSB_MAX_INFLIGHT = 128, RSUSB_MAX_INTERFACES = 32 };
enum RSUSBMethod {
    RSUSBGetInfo, RSUSBGetDescriptors, RSUSBAcquire, RSUSBRelease,
    RSUSBControl, RSUSBTransfer, RSUSBAbort, RSUSBClearStall, RSUSBSetAlternate,
    RSUSBMethodCount
};
enum RSUSBMode { RSUSBIdle = 0, RSUSBSDK = 1, RSUSBCamera = 2 };
struct RSUSBDeviceInfo {
    uint32_t version;
    uint32_t mode;
    uint64_t registryID;
    uint16_t vendorID;
    uint16_t productID;
    uint16_t usbVersion;
    uint16_t descriptorLength;
    uint8_t speed;
    uint8_t interfaceCount;
    uint8_t reserved[6];
};
#ifdef __cplusplus
static_assert(sizeof(RSUSBDeviceInfo) == 32, "ABI layout changed");
#endif
// Scalar signatures:
// GetInfo: [] -> struct RSUSBDeviceInfo (does not acquire the camera)
// GetDescriptors: [] -> device descriptor followed by active configuration bytes
// Acquire: [version, mode] -> []; Release: [] -> []
// Control: [bmRequestType, bRequest, wValue, wIndex, address, length, timeout] -> [actual]
// Transfer (async): [interface, endpoint, address, length, timeout] -> completion [actual, timestamp]
// Abort/ClearStall: [interface, endpoint] -> []; SetAlternate: [interface, alt] -> []
