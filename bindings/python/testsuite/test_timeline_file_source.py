import gst

from common import TestCase
import ges

class TimelineFileSource(TestCase):

    def testTimelineFileSource(self):
        src = ges.TimelineFileSource("file://blahblahblah")

        src.set_mute(True)
        src.set_max_duration(long(100))
        src.set_supported_formats("video")
        assert (src.get_supported_formats().value_nicks[0] == "video")
        src.set_is_image(True)
        assert (src.get_max_duration() == 100)
        assert (src.is_image() == True)
        assert (src.get_uri() == "file://blahblahblah")
