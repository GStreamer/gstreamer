import gst

from common import TestCase
import ges
from time import sleep

class TextOverlay(TestCase):

    def testTextOverlay(self):

        ovrl = ges.TimelineTextOverlay()
        lyr = ges.TimelineLayer()
        tl = ges.timeline_new_audio_video()
        tck = tl.get_tracks()[0]

        ovrl.set_text("Foo")
        self.failIf (ovrl.get_text() != "Foo")
        ovrl.set_font_desc ("Arial")
        self.failIf (ovrl.get_font_desc() != "Arial")
        ovrl.set_valign("top")
        self.failIf (ovrl.get_valignment().value_name != "top")
        ovrl.set_halign("left")
        self.failIf (ovrl.get_halignment().value_name != "left")

        #We will test Timeline Object class functions here

        ovrl.set_start(long(100))
        ovrl.set_inpoint(long(50))
        ovrl.set_duration(long(500))
        ovrl.set_priority(2)
        ovrl.set_layer(lyr)
        tck_obj = ovrl.create_track_object(tck)
        ovrl.add_track_object(tck_obj)
        self.failIf (ovrl.release_track_object(tck_obj) != True)
        self.failIf (ovrl.add_track_object(tck_obj) != True)
        self.failIf (len(ovrl.get_track_objects()) != 1)
        self.failIf (ovrl.get_layer() != lyr)
        ovrl.release_track_object(tck_obj)

        #We test TrackTextOverlay and TrackObject here
        tck_obj.set_text("Bar")
        self.failIf (tck_obj.get_text() != "Bar")
        tck_obj.set_font_desc ("Arial")
        self.failIf (tck_obj.get_font_desc() != "Arial")
        tck_obj.set_valignment("top")
        self.failIf (tck_obj.get_valignment().value_name != "top")
        tck_obj.set_halignment("left")
        self.failIf (tck_obj.get_halignment().value_name != "left")

        tck_obj.set_locked(False)
        self.failIf (tck_obj.is_locked() != False)
        tck_obj.set_start(long(100))
        tck_obj.set_inpoint(long(50))
        tck_obj.set_duration(long(500))
        tck_obj.set_priority(2)
        self.failIf (tck_obj.get_start() != 100)
        self.failIf (tck_obj.get_inpoint() != 50)
        self.failIf (tck_obj.get_duration() != 500)
        self.failIf (tck_obj.get_priority() != 2)
        tck_obj.set_timeline_object(ovrl)
        self.failIf(tck_obj.get_timeline_object() != ovrl)
