# macOS DriverKit USB transport

This opt-in backend connects librealsense's existing RSUSB protocols to the
RealSense USBDriverKit driver through IOKit. It replaces USB discovery and transfer
primitives; UVC negotiation, frame decoding, metadata, calibration, controls and
firmware protocols stay in librealsense. The public C/C++ and Python APIs are
unchanged. With this option off, existing platform backends remain available.

## Requirements and build

Use macOS with full Xcode and CMake. Before using the SDK with hardware, build,
sign, install and approve **RealSense USB Driver** through RealSense Manager from
the companion [realsense-macos](https://github.com/cansik/realsense-macos)
repository. The companion stack targets macOS 15 or later.

Configure from the librealsense repository root:

```sh
cmake -S . -B build-driverkit \
  -DMACOS_USB_DRIVERKIT=ON \
  -DFORCE_RSUSB_BACKEND=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-driverkit --parallel
```

Use the usual librealsense options for Viewer, tools and Python bindings. The
companion repository also provides build and packaging commands for the complete
stack. This CMake build does not build, install or activate the USB driver.

The installed driver must permit ordinary application user clients. With the
appropriate driver provisioning, Viewer, command-line tools and Python connect as
normal users. No application-specific transport changes, sudo, driver detachment
or libusb fallback are required. Signing and installation instructions are in the
companion repository's [README](https://github.com/cansik/realsense-macos#build).

## Transport and ownership

`transport/` contains the fixed-width user-client ABI, descriptor validation,
physical-camera lease policy and IOKit client. The companion driver opens the
physical device and its interfaces, replacing Apple's consumers for that matched
device. Matching is restricted to the D400/D500 USB identifiers in the companion
repository's [device catalog](https://github.com/cansik/realsense-macos/blob/main/config/supported-devices.json),
including camera, motion-only and firmware-recovery devices. Catalog membership
does not imply that every model or stream configuration has been validated on
hardware. Synthetic MIPI/GMSL identifiers are excluded.

All SDK messengers for a physical camera in one process share its connection and
lease. Depth, color, hardware-monitor and HID traffic therefore use the same
camera identity and ownership. Another process cannot capture that same physical
camera concurrently; different cameras have independent leases. Close other
capture clients before opening Viewer or starting an SDK pipeline.

Supported primitives include control transfers, bulk and interrupt transfers,
cached descriptors, alternate settings, endpoint stall clearing and cancellation.
Interrupt transfers use a zero DriverKit timeout. Cancellation aborts the endpoint,
including its outstanding requests. USB isochronous transfers are not implemented.

The backend exposes the DriverKit registry entry ID as the SDK physical port.
The USB serial used by Manager can differ from the firmware serial returned by
`RS2_CAMERA_INFO_SERIAL_NUMBER`; do not interchange them. Applications using SDK
serial selection should use the serial reported by the SDK.

## Motion sensors

DriverKit builds expose the D455 Motion Module through the existing SDK API.
Gyroscope and accelerometer streams use RSUSB HID feature reports and interrupt
transfers on the same physical-camera lease as video. The SDK handles report
formats, motion correction, calibration, extrinsics and timestamps. The legacy
Apple backend remains unchanged.

Choose rates from the device's advertised profiles; accelerometer rates depend on
the IMU revision. Motion profiles use `MOTION_XYZ32F`. Acceleration is reported in
m/s² and angular velocity in rad/s. Applications can request motion and video in
the same pipeline using the existing SDK stream configuration and frame callbacks.

## Constraints

- The installed driver must match the physical camera's USB identifier.
- A physical camera has one process owner at a time; close other capture clients
  before opening it from an SDK application.
- USB isochronous transfers are not implemented.

## Disclaimer

This backend is part of the independent realsense-macos experiment. It is not an
official RealSense driver and is not affiliated with, sponsored by, or endorsed by
RealSense. Product names and trademarks belong to their respective owners.
