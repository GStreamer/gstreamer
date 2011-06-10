import gst

from common import TestCase
from gst import ges

class Track(TestCase):

    def testTrack(self):
        tck = ges.track_video_raw_new()
        tl = ges.Timeline()
        lyr = ges.TimelineLayer()
        src = ges.TimelineTestSource()
        caps = gst.caps_from_string("image/jpeg")
        obj = ges.TrackParseLaunchEffect ("agingtv")

        tl.add_layer(lyr)
        src.add_track_object(obj)
        lyr.add_object(src)
        tck.set_timeline(tl)

        assert (tck.add_object(obj) == True)
        assert (tck.get_timeline() == tl)
        tck.set_caps(caps)
        assert (tck.get_caps().to_string() == "image/jpeg")
