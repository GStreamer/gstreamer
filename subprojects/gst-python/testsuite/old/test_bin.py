# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
# Copyright (C) 2005 Thomas Vander Stichele
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

from common import gobject, gst, unittest, TestCase, pygobject_2_13

import sys
import time

# see
# http://www.sicem.biz/personal/lgs/docs/gobject-python/gobject-tutorial.html
class MyBin(gst.Bin):
    _state_changed = False

    def __init__(self, name):
        # we need to call GObject's init to be able to do self.do_*
        gobject.GObject.__init__(self)
        # since we can't chain up to our parent's __init__, we set the
        # name manually 
        self.set_property('name', name)

    def do_change_state(self, state_change):
        if state_change == gst.STATE_CHANGE_PAUSED_TO_PLAYING:
            self._state_changed = True
        # FIXME: it seems a vmethod increases the refcount without unreffing
        # print self.__gstrefcount__
        # print self.__grefcount__

        # chain up to parent
        return gst.Bin.do_change_state(self, state_change)

# we need to register the type for PyGTK < 2.8
gobject.type_register(MyBin)

# FIXME: fix leak in vmethods before removing overriding fixture
class BinSubclassTest(TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def testStateChange(self):
        bin = MyBin("mybin")
        self.assertEquals(bin.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(bin), pygobject_2_13 and 2 or 3)

        self.assertEquals(bin.get_name(), "mybin")
        self.assertEquals(bin.__gstrefcount__, 1)

        # test get_state with no timeout
        (ret, state, pending) = bin.get_state()
        self.failIfEqual(ret, gst.STATE_CHANGE_FAILURE)
        self.assertEquals(bin.__gstrefcount__, 1)

        # set to playing
        bin.set_state(gst.STATE_PLAYING)
        self.failUnless(bin._state_changed)

        # test get_state with no timeout
        (ret, state, pending) = bin.get_state()
        self.failIfEqual(ret, gst.STATE_CHANGE_FAILURE)

        if ret == gst.STATE_CHANGE_SUCCESS:
            self.assertEquals(state, gst.STATE_PLAYING)
            self.assertEquals(pending, gst.STATE_VOID_PENDING)

        # test get_state with a timeout
        (ret, state, pending) = bin.get_state(1)
        self.failIfEqual(ret, gst.STATE_CHANGE_FAILURE)

        if ret == gst.STATE_CHANGE_SUCCESS:
            self.assertEquals(state, gst.STATE_PLAYING)
            self.assertEquals(pending, gst.STATE_VOID_PENDING)

        (ret, state, pending) = bin.get_state(timeout=gst.SECOND)

        # back to NULL
        bin.set_state(gst.STATE_NULL)

class BinAddRemove(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.bin = gst.Bin('bin')

    def tearDown(self):
        del self.bin
        TestCase.tearDown(self)

    def testError(self):
        gst.info("creating fakesrc")
        src = gst.element_factory_make('fakesrc', 'name')
        gst.info("creating fakesink")
        sink = gst.element_factory_make('fakesink', 'name')
        gst.info("adding src:%d to bin" % src.__gstrefcount__)
        self.assertEqual(src.__gstrefcount__, 1)
        self.bin.add(src)
        self.assertEqual(src.__gstrefcount__, 2)
        gst.info("added src:%d" % src.__gstrefcount__)
        self.assertRaises(gst.AddError, self.bin.add, sink)
        self.assertRaises(gst.AddError, self.bin.add, src)
        self.assertRaises(gst.RemoveError, self.bin.remove, sink)
        gst.info("removing src")
        self.bin.remove(src)
        gst.info("removed")
        self.assertRaises(gst.RemoveError, self.bin.remove, src)
        
    def testMany(self):
        src = gst.element_factory_make('fakesrc')
        sink = gst.element_factory_make('fakesink')
        self.bin.add(src, sink)
        self.assertRaises(gst.AddError, self.bin.add, src, sink)
        self.bin.remove(src, sink)
        self.assertRaises(gst.RemoveError, self.bin.remove, src, sink)

class Preroll(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.bin = gst.Bin('bin')

    def tearDown(self):
        # FIXME: wait for state change thread to settle down
        while self.bin.__gstrefcount__ > 1:
            time.sleep(0.1)
        self.assertEquals(self.bin.__gstrefcount__, 1)
        del self.bin
        TestCase.tearDown(self)

    def testFake(self):
        src = gst.element_factory_make('fakesrc')
        sink = gst.element_factory_make('fakesink')
        self.bin.add(src)

        # bin will go to paused, src pad task will start and error out
        self.bin.set_state(gst.STATE_PAUSED)
        ret = self.bin.get_state()
        self.assertEquals(ret[0], gst.STATE_CHANGE_SUCCESS)
        self.assertEquals(ret[1], gst.STATE_PAUSED)
        self.assertEquals(ret[2], gst.STATE_VOID_PENDING)

        # adding the sink will cause the bin to go in preroll mode
        gst.debug('adding sink and setting to PAUSED, should cause preroll')
        self.bin.add(sink)
        sink.set_state(gst.STATE_PAUSED)
        ret = self.bin.get_state(timeout=0)
        self.assertEquals(ret[0], gst.STATE_CHANGE_ASYNC)
        self.assertEquals(ret[1], gst.STATE_PAUSED)
        self.assertEquals(ret[2], gst.STATE_PAUSED)

        # to actually complete preroll, we need to link and re-enable fakesrc
        src.set_state(gst.STATE_READY)
        src.link(sink)
        src.set_state(gst.STATE_PAUSED)
        ret = self.bin.get_state()
        self.assertEquals(ret[0], gst.STATE_CHANGE_SUCCESS)
        self.assertEquals(ret[1], gst.STATE_PAUSED)
        self.assertEquals(ret[2], gst.STATE_VOID_PENDING)

        self.bin.set_state(gst.STATE_NULL)
        self.bin.get_state()
 
class ConstructorTest(TestCase):
    def testGood(self):
        bin = gst.Bin()
        bin = gst.Bin(None)
        bin = gst.Bin('')
        bin = gst.Bin('myname')
        
    def testBad(self):
        # these are now valid. pygobject will take care of converting
        # the arguments to a string.
        #self.assertRaises(TypeError, gst.Bin, 0)
        #self.assertRaises(TypeError, gst.Bin, gst.Bin())
        pass
        
if __name__ == "__main__":
    unittest.main()
