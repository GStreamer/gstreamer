from gi.repository import Gst, GES, GLib

import os

class Simple:
    def __init__(self, uri):
        timeline = GES.Timeline()
        trackv = GES.Track.video_raw_new()
        self.layer = GES.TimelineLayer()
        self.pipeline = GES.TimelinePipeline()
        self.pipeline.add_timeline(timeline)


        timeline.add_track(trackv)
        timeline.add_layer(self.layer)

        GES.Asset.new_async(GES.UriClip, uri, None, self.discoveredCb, None)
        self.loop = GLib.MainLoop()
        self.loop.run()

    def discoveredCb(self, asset, result, blop):
        self.layer.add_asset(asset, long(0), long(0), long(10 * Gst.SECOND), 1.0, GES.TrackType.VIDEO)
        self.start()

    def busMessageCb(self, bus, message, udata):
        if message.type == Gst.MessageType.EOS:
            print "EOS"
            self.loop.quit()
        if message.type == Gst.MessageType.ERROR:
            print "ERROR"
            self.loop.quit()

    def start(self):
        self.pipeline.set_state(Gst.State.PLAYING)
        self.pipeline.get_bus().add_watch(GLib.PRIORITY_DEFAULT, self.busMessageCb, None)

if __name__ == "__main__":
    if len(os.sys.argv) < 2:
        print "You must specify a file URI"
        exit(-1)
    GES.init()

    # And try!
    Simple(os.sys.argv[1])
