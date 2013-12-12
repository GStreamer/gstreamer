# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

from common import gst, unittest, TestCase

import gobject

def find_mixer_element():
    """ Searches for an element implementing the mixer interface """
    allmix = [x for x in gst.registry_get_default().get_feature_list(gst.ElementFactory)
              if x.has_interface("GstMixer") and x.has_interface("GstPropertyProbe")]
    if allmix == []:
        return None
    return allmix[0]

class Availability(TestCase):
    def testXOverlay(self):
        assert hasattr(gst.interfaces, 'XOverlay')
        assert issubclass(gst.interfaces.XOverlay, gobject.GInterface)

    def testMixer(self):
        assert hasattr(gst.interfaces, 'Mixer')
        assert issubclass(gst.interfaces.Mixer, gobject.GInterface)

class FunctionCall(TestCase):
    def FIXME_testXOverlay(self):
        # obviously a testsuite is not allowed to instantiate this
        # since it needs a running X or will fail.  find some way to
        # deal with that.
        element = gst.element_factory_make('xvimagesink')
        assert isinstance(element, gst.Element)
        assert isinstance(element, gst.interfaces.XOverlay)
        element.set_xwindow_id(0L)

class MixerTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        amix = find_mixer_element()
        if amix:
            self.mixer = amix.create()
        else:
            self.mixer = None

    def tearDown(self):
        del self.mixer
        TestCase.tearDown(self)

    def testGetProperty(self):
        if self.mixer == None:
            return
        self.failUnless(self.mixer.probe_get_property('device'))
        self.assertRaises(ValueError,
                          self.mixer.probe_get_property, 'non-existent')

    def testGetProperties(self):
        if self.mixer == None:
            return
        properties = self.mixer.probe_get_properties()
        self.failUnless(properties)
        self.assertEqual(type(properties), list)
        prop = properties[0]
        self.assertEqual(prop.name, 'device')
        self.assertEqual(prop.value_type, gobject.TYPE_STRING)

    def testGetValuesName(self):
        if self.mixer == None:
            return
        values = self.mixer.probe_get_values_name('device')
        self.assertEqual(type(values), list)


if __name__ == "__main__":
    unittest.main()
