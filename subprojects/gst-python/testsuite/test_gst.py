# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2009 Thomas Vander Stichele
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
import overrides_hack
import sys
from common import TestCase, unittest
from gi.repository import Gst
import gi


gi.require_version('Gst', '1.0')


overrides_hack


class TimeArgsTest(TestCase):
    def testNoneTime(self):
        self.assertRaises(TypeError, Gst.TIME_ARGS, None)

    def testStringTime(self):
        self.assertRaises(TypeError, Gst.TIME_ARGS, "String")

    def testClockTimeNone(self):
        self.assertEqual(Gst.TIME_ARGS(Gst.CLOCK_TIME_NONE), 'CLOCK_TIME_NONE')

    def testOneSecond(self):
        self.assertEqual(Gst.TIME_ARGS(Gst.SECOND), '0:00:01.000000000')


class TestNotInitialized(TestCase):
    def testNotInitialized(self):
        if sys.version_info >= (3, 0):
            assert_type = Gst.NotInitialized
        else:
            assert_type = TypeError

        with self.assertRaises(assert_type):
            Gst.Caps.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.Structure.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.ElementFactory.make("identity", None)

    def testNotDeinitialized(self):
        Gst.init(None)

        self.assertIsNotNone(Gst.Caps.from_string("audio/x-raw"))
        self.assertIsNotNone(Gst.Structure.from_string("audio/x-raw"))
        self.assertIsNotNone(Gst.ElementFactory.make("identity", None))

        Gst.deinit()
        if sys.version_info >= (3, 0):
            assert_type = Gst.NotInitialized
        else:
            assert_type = TypeError

        with self.assertRaises(assert_type):
            Gst.Caps.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.Structure.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.ElementFactory.make("identity", None)


class TestCaps(TestCase):

    def test_writable_make_writable_no_copy(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")
        repr = caps.__repr__()
        caps.make_writable()
        self.assertEqual(repr, caps.__repr__())
        del caps

    def test_make_writable_with_copy(self):
        Gst.init(None)

        caps = Gst.Caps("audio/x-raw")
        repr = caps.__repr__()
        miniobj = caps.mini_object
        self.assertFalse(caps.is_writable())
        repr = caps.__repr__()
        self.assertTrue(caps.make_writable())
        self.assertNotEqual(repr, caps.__repr__())
        self.assertTrue(caps.is_writable())
        del caps
        del miniobj

    def test_no_writable(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")
        caps.mini_object.refcount += 1

        with self.assertRaises(Gst.NotWritableCaps):
            with caps.writable_structure(0) as s:
                s.set_value("rate", 44100)

        caps.mini_object.refcount -= 1

    def test_make_writable(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")

        capsfilter = Gst.ElementFactory.make("capsfilter", None)

        if not capsfilter:
            self.skipTest("capsfilter not available")

        capsfilter.set_property("caps", caps)
        caps = capsfilter.get_property("caps")

        with self.assertRaises(Gst.NotWritableCaps):
            with caps.writable_structure(0) as s:
                s.set_value("rate", 44100)
        caps.make_writable()
        with caps.writable_structure(0) as s:
            s.set_value("rate", 44100)
        capsfilter.set_property("caps", caps)
        caps = capsfilter.get_property("caps")
        with caps.get_structure(0) as s:
            self.assertEqual(s["rate"], 44100)

    def test_writable(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")

        with caps.writable_structure(0) as s:
            s.set_value("rate", 44100)
            s.set_value("channels", 2)
        with caps.get_structure(0) as s:
            self.assertEqual(s["rate"], 44100)
            self.assertEqual(s["channels"], 2)

    def test_delete_caps_while_accessing(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")

        with caps.writable_structure(0) as s:
            del caps
            s.set_value("rate", 44100)
            s.set_value("channels", 2)
            self.assertEqual(s["rate"], 44100)
            self.assertEqual(s["channels"], 2)

    def test_read_no_copy(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")

        with caps.get_structure(0) as s:
            ptr = s.__ptr__()

        with caps.get_structure(0) as s:
            self.assertEqual(ptr, s.__ptr__())

        c2 = caps.mini_object
        self.assertEqual(c2.refcount, 2)
        with caps.get_structure(0) as s:
            with self.assertRaises(Gst.NotWritableStructure):
                s.set_value("rate", 44100)
        caps.make_writable()

        with caps.get_structure(0) as s:
            self.assertNotEqual(ptr, s.__ptr__())

    def test_read_no_copy_no_cm(self):
        Gst.init(None)
        caps = Gst.Caps("audio/x-raw")

        s = caps.get_structure(0)
        ptr = s.__ptr__()

        s2 = caps.get_structure(0)
        self.assertEqual(ptr, s2.__ptr__())

        c2 = caps.mini_object
        self.assertEqual(c2.refcount, 2)
        with self.assertRaises(Gst.NotWritableStructure):
            caps.get_structure(0).set_value("rate", 44100)

        caps.make_writable()
        s = caps.get_structure(0)
        self.assertNotEqual(ptr, s.__ptr__())


class TestStructure(TestCase):

    def test_new(self):
        Gst.init(None)
        test = Gst.Structure('test', test=1)
        self.assertEqual(test['test'], 1)

        test = Gst.Structure('test,test=1')
        self.assertEqual(test['test'], 1)


class TestEvent(TestCase):
    def test_writable(self):
        Gst.init(None)
        event = Gst.Event.new_caps(Gst.Caps("audio/x-raw"))

        with event.writable_structure() as s:
            s.set_value("rate", 44100)
            s.set_value("channels", 2)

        with event.get_structure() as s:
            self.assertEqual(s["rate"], 44100)
            self.assertEqual(s["channels"], 2)

    def test_no_structure(self):
        Gst.init(None)
        event = Gst.Event.new_flush_start()
        s = event.get_structure()
        self.assertIsNone(s)


class TestQuery(TestCase):
    def test_writable(self):
        Gst.init(None)
        query = Gst.Query.new_caps(Gst.Caps("audio/x-raw"))

        with query.writable_structure() as s:
            del query
            s.set_value("rate", 44100)
            s.set_value("channels", 2)
            self.assertEqual(s["rate"], 44100)
            self.assertEqual(s["channels"], 2)


class TestContext(TestCase):
    def test_writable(self):
        Gst.init(None)
        context = Gst.Context.new("test", True)

        with context.writable_structure() as s:
            s.set_value("one", 1)
            s.set_value("two", 2)
            self.assertEqual(s["one"], 1)
            self.assertEqual(s["two"], 2)


class TestBin(TestCase):

    def test_add_pad(self):
        Gst.init(None)
        self.assertEqual(Gst.ElementFactory.make("bin", None).sinkpads, [])


class TestBuffer(TestCase):

    def test_set_metas(self):
        Gst.init(None)
        buf = Gst.Buffer.new_wrapped([42])
        buf.pts = 42
        self.assertEqual(buf.pts, 42)

        make_not_writable = buf.mini_object
        with self.assertRaises(Gst.NotWritableMiniObject):
            buf.pts = 52

        buf.make_writable()
        buf.dts = 62
        self.assertEqual(buf.dts, 62)
        self.assertTrue(buf.flags & Gst.BufferFlags.DISCONT == 0)
        buf.flags = Gst.BufferFlags.DISCONT
        buf.duration = 72
        self.assertEqual(buf.duration, 72)
        buf.offset = 82
        self.assertEqual(buf.offset, 82)
        buf.offset_end = 92
        self.assertEqual(buf.offset_end, 92)
        del make_not_writable

        meta = buf.add_reference_timestamp_meta(Gst.Caps("yes"), 10, 10)
        self.assertEqual(buf.get_reference_timestamp_meta(), meta)

    def test_map_unmap_manual(self):
        Gst.init(None)
        buf = Gst.Buffer.new_wrapped([42])
        info = buf.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE)
        self.assertEqual(info.data[0], 42)
        buf.unmap(info)
        with self.assertRaises(ValueError):
            info.data[0]

    def test_map_unmap_context(self):
        Gst.init(None)
        buf = Gst.Buffer.new_wrapped([42])
        with buf.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE) as info:
            self.assertEqual(info.data[0], 42)
        with self.assertRaises(ValueError):
            info.data[0]


if __name__ == "__main__":
    unittest.main()
