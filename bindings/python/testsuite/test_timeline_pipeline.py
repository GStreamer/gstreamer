import glib
import gst

from common import TestCase
import ges

class TimelinePipeline(TestCase):

    def testTimelinePipeline(self):
        stgs = gst.pbutils.EncodingAudioProfile(gst.Caps("video/x-dirac"), "test", gst.caps_new_any(), 0)
        ppln = ges.TimelinePipeline()
        tl = ges.Timeline()
        assert (ppln.add_timeline (tl) == True)
        assert (ppln.set_mode("TIMELINE_MODE_PREVIEW_AUDIO") == True)
