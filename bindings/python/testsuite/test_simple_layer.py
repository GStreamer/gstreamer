import glib
import gst

from common import TestCase
import ges

class SimpleLayer(TestCase):

    def testSimpleLayer(self):
        lyr = ges.SimpleTimelineLayer()
        tl = ges.Timeline()
        src = ges.TimelineTestSource()
        src2 = ges.TimelineTestSource()

        lyr.set_timeline(tl)

        assert (lyr.add_object(src, 0) == True)
        assert (lyr.add_object(src2, 1) == True)
        assert (lyr.nth(0) == src)
        assert (lyr.move_object (src, 1) == True)
        self.failIf(lyr.index(src) != 1)
        assert (lyr.is_valid() == True)
