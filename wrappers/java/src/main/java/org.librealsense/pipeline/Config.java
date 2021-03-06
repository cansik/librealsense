package org.librealsense.pipeline;

import org.librealsense.Native;
import org.librealsense.devices.Device;

public class Config {

    protected long instance;

    public Config(long instance) {
        this.instance = instance;
    }

    public static Config create() {
        return new Config(Native.rs2CreateConfig());
    }

    public void enableDevice(Device device) {
        Native.rs2ConfigEnableDevice(instance, device.serial());
    }

    public void enableStream(Native.Stream stream, int index, int width, int height, Native.Format format, int fps) {
        Native.rs2ConfigEnableStream(instance, stream.ordinal(), index, width, height, format.ordinal(), fps);
    }
}