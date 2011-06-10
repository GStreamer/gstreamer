import gst

from common import TestCase
from gst import ges
from time import sleep

class ParseLaunchEffect(TestCase):

    def testParseLaunchEffect(self):
        tl = ges.Timeline()
        tck = ges.track_video_raw_new()
        lyr = ges.TimelineLayer()
        efct = ges.TimelineParseLaunchEffect("agingtv", None)
        tck_efct = ges.TrackParseLaunchEffect("agingtv")

        tl.add_layer(lyr)
        efct.add_track_object(tck_efct)
        lyr.add_object(efct)
        tck.set_timeline(tl)
        tck.add_object(tck_efct)
        tck_efct.set_child_property("GstAgingTV::scratch-lines", 17)
        self.failIf(tck_efct.get_child_property("GstAgingTV::scratch-lines") != 17)
        self.failIf(len(tck_efct.list_children_properties()) != 6)
        self.failIf (tck_efct.lookup_child ("scratch-lines") == None)
