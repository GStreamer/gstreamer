#!/usr/bin/python3

import sys
import gi
gi.require_version('Gst', '1.0')
from gi.repository import GObject, Gst

def bus_call(bus, msg, *args):
    # print("BUSCALL", msg, msg.type, *args)
    if msg.type == Gst.MessageType.EOS:
        print("End-of-stream")
        loop.quit()
        return
    elif msg.type == Gst.MessageType.ERROR:
        print("GST ERROR", msg.parse_error())
        loop.quit()
        return
    return True

saturation = -100
def set_saturation(pipeline):
    global saturation
    if saturation <= 100:
      print("Setting saturation to {0}".format(saturation))
      videosrc.set_property("saturation", saturation)
      videosrc.set_property("annotation-text", "Saturation %d" % (saturation))
    else:
      pipeline.send_event (Gst.Event.new_eos())
      return False
    saturation += 10
    return True


if __name__ == "__main__":
    GObject.threads_init()
    # initialization
    loop = GObject.MainLoop()
    Gst.init(None)

    pipeline = Gst.parse_launch ("rpicamsrc name=src ! video/x-h264,width=320,height=240 ! h264parse ! mp4mux ! filesink name=s")
    if pipeline == None:
      print ("Failed to create pipeline")
      sys.exit(0)

    # watch for messages on the pipeline's bus (note that this will only
    # work like this when a GLib main loop is running)
    bus = pipeline.get_bus()
    bus.add_watch(0, bus_call, loop)

    videosrc = pipeline.get_by_name ("src")
    videosrc.set_property("saturation", saturation)
    videosrc.set_property("annotation-mode", 1)

    sink = pipeline.get_by_name ("s")
    sink.set_property ("location", "test.mp4")

    # this will call set_saturation every 1s
    GObject.timeout_add(1000, set_saturation, pipeline)

    # run
    pipeline.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    except Exception as e:
        print(e)
    # cleanup
    pipeline.set_state(Gst.State.NULL)

