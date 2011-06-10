import glib
import gst

from common import TestCase
from gst import ges

class TimelineTestSource(TestCase):

    def testTimelineTestSource(self):
        src = ges.TimelineTestSource()
        tck_src = ges.TrackAudioTestSource()
        src.set_mute(True)
        src.set_vpattern("snow")
        src.set_frequency(880)
        src.set_volume (1)
        assert (src.get_vpattern() != None)
        assert (src.is_muted() == True)
        assert (src.get_frequency() == 880)
        assert (src.get_volume() == 1)
