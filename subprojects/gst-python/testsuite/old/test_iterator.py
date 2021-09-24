# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2005 Johan Dahlin
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

import unittest
from common import gst, TestCase

class IteratorTest(TestCase):
    # XXX: Elements
    def testBinIterateElements(self):
        pipeline = gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.elements())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")
        
        self.assertEqual(len(elements), 2)
        self.failUnless(fakesrc in elements)
        self.failUnless(fakesink in elements)

        pipeline.remove(fakesrc)
        elements = list(pipeline.elements())

        self.assertEqual(len(elements), 1)
        self.failUnless(not fakesrc in pipeline)

        # XXX : There seems to be a problem about the GType
        #       set in gst_bin_iterated_sorted

    def testBinIterateSorted(self):
        pipeline =  gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.sorted())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        self.assertEqual(elements[0], fakesink)
        self.assertEqual(elements[1], fakesrc)

    def testBinIterateRecurse(self):
        pipeline = gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.recurse())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        self.assertEqual(elements[0], fakesink)
        self.assertEqual(elements[1], fakesrc)

    def testBinIterateSinks(self):
        pipeline = gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.sinks())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        self.assertEqual(len(elements), 1)
        self.failUnless(fakesink in elements)
        self.failUnless(not fakesrc in elements)

    
    def testIteratePadsFakeSrc(self):
        fakesrc = gst.element_factory_make('fakesrc')
        pads = list(fakesrc.pads())
        srcpad = fakesrc.get_pad('src')
        self.assertEqual(len(pads), 1)
        self.assertEqual(pads[0], srcpad)
        srcpads = list(fakesrc.src_pads())
        self.assertEqual(len(srcpads), 1)
        self.assertEqual(srcpads[0], srcpad)
        sinkpads = list(fakesrc.sink_pads())
        self.assertEqual(sinkpads, [])

        self.assertEqual(len(list(fakesrc)), 1)
        for pad in fakesrc:
            self.assertEqual(pad, srcpad)
            break
        else:
            raise AssertionError
        
    def testIteratePadsFakeSink(self):
        fakesink = gst.element_factory_make('fakesink')
        pads = list(fakesink.pads())
        sinkpad = fakesink.get_pad('sink')
        self.assertEqual(len(pads), 1)
        self.assertEqual(pads[0], sinkpad)
        srcpads = list(fakesink.src_pads())
        self.assertEqual(srcpads, [])
        sinkpads = list(fakesink.sink_pads())
        self.assertEqual(len(sinkpads), 1)
        self.assertEqual(sinkpads[0], sinkpad)

        self.assertEqual(len(list(fakesink)), 1)
        for pad in fakesink:
            self.assertEqual(pad, sinkpad)
            break
        else:
            raise AssertionError

    def testInvalidIterator(self):
        p = gst.Pad("p", gst.PAD_SRC)
        # The C function will return NULL, we should
        # therefore have an exception raised
        self.assertRaises(TypeError, p.iterate_internal_links)
        del p

