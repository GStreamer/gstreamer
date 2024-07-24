#!/usr/bin/env python3

'''
Simple example to demonstrate using GstRtspServer in Python with video source from camera
'''
import argparse
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstRtspServer


def start_rtsp_server(rtsp_port, device, stream_name):
    server = GstRtspServer.RTSPServer.new()
    server.props.service = rtsp_port
    server.attach(None)

    factory = GstRtspServer.RTSPMediaFactory.new()
    # Define pipeline which will be created during connection
    launch_str = f"( v4l2src device={device} ! videoconvert ! queue ! video/x-raw,format=I420 ! x264enc ! h264parse ! rtph264pay name=pay0 pt=96 )"
    factory.set_launch(launch_str)
    factory.set_shared(True)

    server.get_mount_points().add_factory("/" + stream_name, factory)
    print(f"Device: {device} \nUnder: rtsp://localhost:{rtsp_port}/{stream_name}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--camera",
        help="Camera file path",
        type=str,
        default="/dev/video0",
    )
    parser.add_argument("--rtsp-port", default="8557")
    parser.add_argument("--stream-name", default="cam")
    args = parser.parse_args()
    Gst.init(None)
    start_rtsp_server(args.rtsp_port, args.camera, args.stream_name)
    loop = GLib.MainLoop()
    loop.run()
