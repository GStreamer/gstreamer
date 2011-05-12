import glib
import gst

from common import TestCase
from gst import ges

class Timeline(TestCase):

    def testTimeline(self):

        tl = ges.timeline_new_audio_video()
        lyr = ges.SimpleTimelineLayer()
        src = ges.TimelineTestSource()
        pip = ges.TimelinePipeline()
        bus = pip.get_bus()
        self.mainloop = glib.MainLoop()

        #Let's add the layer to the timeline, and the source to the layer.
        tl.add_layer(lyr)
        src.set_duration(long(gst.SECOND * 10))
        src.set_vpattern("Random (television snow)")
        lyr.add_object(src, -1)

        pip.add_timeline(tl)
        bus.set_sync_handler(self.bus_handler)

        self.pipeline = pip
        self.layer = lyr

        #Mainloop is finished, tear down.
        self.pipeline = None


    def bus_handler(self, unused_bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print "ERROR"
        elif message.type == gst.MESSAGE_EOS:
            print "Done"
        return gst.BUS_PASS
