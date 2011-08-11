import glib
import gst

from common import TestCase
import ges

class Layer(TestCase):

    def testLayer(self):
        lyr = ges.TimelineLayer()
        tl = ges.Timeline()
        src = ges.TimelineTestSource()

        lyr.set_timeline(tl)

        assert (lyr.add_object(src) == True)
        self.failIf(len (lyr.get_objects()) != 1)
        assert (lyr.remove_object(src) == True)

        lyr.set_priority(1)
        self.failIf(lyr.get_priority() != 1)
