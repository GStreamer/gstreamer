from gi.repository import Gst, GES, GLib

class Simple:
    def __init__(self, uri):
        timeline = GES.Timeline()
        trackv = GES.Track.video_raw_new()
        layer = GES.TimelineLayer()
        self.pipeline = GES.TimelinePipeline()
        self.pipeline.add_timeline(timeline)

        timeline.add_track(trackv)
        timeline.add_layer(layer)

        src = GES.UriClip.new(uri=uri)
        src.set_start(long(0))
        src.set_duration(long(10 * Gst.SECOND))
        print src
        layer.add_object(src)

    def start(self):
        self.pipeline.set_state(Gst.State.PLAYING)

if __name__ == "__main__":
    if len(os.sys.argv) < 2:
        print "You must specify a file URI"
        exit(-1)

    loop = GLib.MainLoop()
    widget = Simple()
    widget.start()
    loop.run()
