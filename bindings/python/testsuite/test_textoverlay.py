import gst

from common import TestCase
from gst import ges

class Timeline(TestCase):

    def testTimeline(self):

        tl = ges.timeline_new_audio_video()
        lyr = ges.SimpleTimelineLayer()
        src = ges.TimelineTestSource()
        pip = ges.TimelinePipeline()
        ovrl = ges.TimelineTextOverlay()
        bus = pip.get_bus()

        # Let's add the layer to the timeline, and the sources to the layer.

        tl.add_layer(lyr)
        src.set_duration(long(gst.SECOND * 10))
        ovrl.set_duration(long(gst.SECOND * 5))
        ovrl.set_start(long(gst.SECOND * 5))
        ovrl.set_text("Foo")

        lyr.add_object(src, -1)
        lyr.add_object(ovrl, -1)

        pip.add_timeline(tl)
        bus.set_sync_handler(self.bus_handler)

        self.pipeline = pip
        self.layer = lyr

        #Mainloop is finished, tear down.
        self.pipeline = None


    def bus_handler(self, unused_bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print "ERROR"
            self.mainloop.quit()
        elif message.type == gst.MESSAGE_EOS:
            print "Done"
            self.mainloop.quit()
        return gst.BUS_PASS
