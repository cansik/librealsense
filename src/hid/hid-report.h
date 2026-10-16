// License: Apache 2.0. See LICENSE file in root directory.
#pragma once

#include "hid-types.h"
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace librealsense { namespace platform {

// Decode wire bytes without unaligned typed loads. Old firmware sends 16-bit axes;
// firmware >=5.16 sends 32-bit axes. Reject malformed/unknown reports before dispatch.
inline bool decode_hid_report(const uint8_t * bytes, size_t length, size_t expected,
                              REALSENSE_HID_REPORT & report)
{
    static_assert(sizeof(REALSENSE_HID_REPORT) == 38, "HID wire layout changed");
    if (!bytes || length != expected || (expected != 32 && expected != 38)) return false;
    if (bytes[0] != REPORT_ID_ACCELEROMETER_3D && bytes[0] != REPORT_ID_GYROMETER_3D
        && bytes[0] != REPORT_ID_CUSTOM) return false;
    report = {};
    if (expected == 38) std::memcpy(&report, bytes, expected);
    else {
        std::memcpy(&report, bytes, 10);
        int16_t axes[3];
        std::memcpy(axes, bytes + 10, sizeof(axes));
        report.x = axes[0]; report.y = axes[1]; report.z = axes[2];
        std::memcpy(&report.customValue1, bytes + 16, 16);
    }
    return true;
}

inline int hid_report_id(const std::string & sensor)
{
    if (sensor == gyro) return REPORT_ID_GYROMETER_3D;
    if (sensor == accel) return REPORT_ID_ACCELEROMETER_3D;
    if (sensor == custom) return REPORT_ID_CUSTOM;
    throw std::invalid_argument("Unknown HID sensor: " + sensor);
}

// Include the failing report in rollback: SET_REPORT may have succeeded before
// readback failed. Never claim open succeeded with only some sensors configured.
template<class Profiles, class Configure>
void configure_hid_profiles(const Profiles & profiles, Configure configure)
{
    for (const auto & profile : profiles) hid_report_id(profile.sensor_name);
    std::vector<int> attempted;
    try {
        for (const auto & profile : profiles) {
            const auto id = hid_report_id(profile.sensor_name);
            attempted.push_back(id);
            const auto result = configure(DEVICE_POWER_D0, id, profile.frequency, profile.sensitivity);
            if (result != 0) throw std::runtime_error("HID configuration failed for " + profile.sensor_name
                                                    + " (USB status " + std::to_string(result) + ")");
        }
    } catch (...) {
        for (auto it = attempted.rbegin(); it != attempted.rend(); ++it) {
            try { configure(DEVICE_POWER_D4, *it, 0, 0.1); } catch (...) {}
        }
        throw;
    }
}

inline bool valid_hid_feature_report(const FEATURE_REPORT & report, int id, size_t length)
{
    return length == sizeof(report) && report.reportId == id;
}

}}
