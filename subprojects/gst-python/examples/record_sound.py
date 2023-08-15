#!/usr/bin/env python3

'''
Simple example to demonstrate using Gst.DeviceMonitor and `transcodebin` to
record audio from a microphone into an .oga file
'''
import gi
import sys
gi.require_version('Gst', '1.0')
gi.require_version('GstPbutils', '1.0')
gi.require_version('GLib', '2.0')
gi.require_version('GObject', '2.0')
from gi.repository import GLib, GObject, Gst, GstPbutils


def bus_call(bus, message, loop):
    t = message.type
    Gst.debug_bin_to_dot_file_with_ts(pipeline, Gst.DebugGraphDetails.ALL, "test")
    if t == Gst.MessageType.EOS:
        sys.stdout.write("End-of-stream\n")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("Error: %s: %s\n" % (err, debug))
        loop.quit()
    return True


def stop(loop, pipeline):
    _, position = pipeline.query_position(Gst.Format.TIME)
    print("Position: %s\r" % Gst.TIME_ARGS(position))

    if position > 10 * Gst.SECOND:
        loop.quit()
        print("Stopping after 10 seconds")
        return False

    return True


def _link_to_filesink(element, pad, filesink):
    if not element.link(filesink):
        print(f"Failed to link transcodebin to output.")
        sys.exit(1)


if __name__ == "__main__":
    Gst.init(sys.argv)

    if len(sys.argv) != 2:
        print("Missing <output-location.ogg> parameter")
        sys.exit(1)

    monitor = Gst.DeviceMonitor.new()
    monitor.add_filter("Audio/Source", None)
    monitor.start()

    # This is happening synchonously, use the GstBus based API and
    # monitor.start() to avoid blocking the main thread.
    devices = monitor.get_devices()

    if not devices:
        print("No microphone found...")
        sys.exit(1)

    default = [d for d in devices if d.get_properties().get_value("is-default") is True]
    if len(default) == 1:
        device = default[0]
    else:
        print("Available microphones:")
        for i, d in enumerate(devices):
            print("%d - %s" % (i, d.get_display_name()))
        res = int(input("Select device: "))
        device = devices[res]

    pipeline = Gst.ElementFactory.make("pipeline", None)
    source = device.create_element()
    conv = Gst.ElementFactory.make("audioconvert", None)
    transcodebin = Gst.ElementFactory.make("transcodebin", None)
    Gst.util_set_object_arg(transcodebin, "profile", "video/ogg:audio/x-opus")
    filesink = Gst.ElementFactory.make("filesink", None)
    filesink.props.location = sys.argv[1]

    pipeline.add(source, conv, transcodebin, filesink)
    source.link(conv)
    conv.link(transcodebin)

    transcodebin.connect('pad-added', _link_to_filesink, filesink)

    pipeline.set_state(Gst.State.PLAYING)

    bus = pipeline.get_bus()
    bus.add_signal_watch()

    loop = GLib.MainLoop()
    GLib.timeout_add_seconds(1, stop, loop, pipeline)
    bus.connect("message", bus_call, loop)
    loop.run()

    pipeline.set_state(Gst.State.NULL)
    pipeline.get_state(Gst.CLOCK_TIME_NONE)
