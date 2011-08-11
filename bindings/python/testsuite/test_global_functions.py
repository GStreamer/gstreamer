import glib
import gst

from common import TestCase
import ges

class GlobalFunctions(TestCase):

    def testGlobalFunctions(self):
        tl = ges.timeline_new_audio_video()
        tr = ges.timeline_standard_transition_new_for_nick("crossfade")
