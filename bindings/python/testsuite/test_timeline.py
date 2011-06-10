import glib
import gst

from common import TestCase
from gst import ges

class Timeline(TestCase):

    def testTimeline(self):

        tl = ges.timeline_new_audio_video()
        lyr = ges.SimpleTimelineLayer()
        tck = ges.track_audio_raw_new()

        assert (tl.add_track(tck) == True)
        #We should have two tracks from the timeline_new_audio_video() function + 1
        self.failIf(len(tl.get_tracks()) != 3)
        assert (tl.remove_track(tck) == True)

        assert (tl.add_layer(lyr) == True)
        self.failIf(len(tl.get_layers()) != 1)
        assert (tl.remove_layer(lyr) == True)
