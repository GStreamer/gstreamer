import gst

from common import TestCase
import ges

class TimelineTitleSource(TestCase):

    def testTimelineTitleSource(self):
        src = ges.TimelineTitleSource()
        lyr = ges.TimelineLayer()
        tck = ges.track_video_raw_new()

        src.set_text("Foo")
        self.failIf (src.get_text() != "Foo")
        src.set_font_desc ("Arial")
        self.failIf (src.get_font_desc() != "Arial")
        src.set_valignment("top")
        assert (src.get_valignment().value_name == "top")
        src.set_halignment("left")
        assert (src.get_halignment().value_name == "left")
        src.set_mute(True)
        assert (src.is_muted() == True)
        
