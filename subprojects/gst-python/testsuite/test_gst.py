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

    def test_iterate(self):
        Gst.init(None)
        caps = Gst.Caps()
        caps.append_structure(Gst.Structure('test1', one=1))
        caps.append_structure(Gst.Structure('test2', two=2))
        caps.append_structure(Gst.Structure('test3', three=3))
        self.assertEqual(len(caps), 3)
        items = [s for s in caps]
        self.assertEqual(items[0]['one'], 1)
        self.assertEqual(items[1]['two'], 2)
        self.assertEqual(items[2]['three'], 3)
        with self.assertRaises(IndexError):
            caps[3]


class TestStructure(TestCase):

    def test_new(self):
        Gst.init(None)
        test = Gst.Structure('test', test=1)
        self.assertEqual(test['test'], 1)

        test = Gst.Structure('test,test=1')
        self.assertEqual(test['test'], 1)

    def test_iterate_items(self):
        Gst.init(None)
        test = Gst.Structure('test', one=1, two=2, three=3)
        self.assertEqual(len(test), 3)
        self.assertEqual(test["one"], 1)
        self.assertEqual(test["two"], 2)
        self.assertEqual(test["three"], 3)
        items = {k: v for k, v in test.items()}
        self.assertEqual(items, {'one': 1, 'two': 2, 'three': 3})
        keys = [k for k in test]
        self.assertEqual(keys, ['one', 'two', 'three'])
        with self.assertRaises(KeyError):
            test["four"]


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


class TestPadProbe(TestCase):

    def test_pad_probe(self):
        Gst.init(None)
        pipeline = Gst.Pipeline("pipeline")
        src = pipeline.make_and_add("fakesrc", "src")
        sink = pipeline.make_and_add("fakesink", "sink")
        src.link(sink)

        src.props.num_buffers = 5

        buffer_count = 0

        def probe_cb(pad, info):
            nonlocal buffer_count

            buffer_count += 1

            # Get a writable buffer from the probe info
            with info.writable_object() as buffer:
                self.assertIsInstance(buffer, Gst.Buffer)
                self.assertTrue(buffer.is_writable())
                buffer.pts = buffer_count * Gst.SECOND * 2
                ptr = buffer.__ptr__()
                # Info does not hold a buffer any more
                self.assertIsNone(info.get_buffer())

            # info.get_buffer() never returns a writable buffer because both
            # python and GstPadProbeInfo hold references to it.
            buffer = info.get_buffer()
            self.assertIsNotNone(buffer)
            self.assertFalse(buffer.is_writable())
            self.assertEqual(buffer.pts, buffer_count * Gst.SECOND * 2)
            self.assertEqual(buffer.__ptr__(), ptr)

            return Gst.PadProbeReturn.OK

        sink_pad = sink.get_static_pad("sink")
        probe_id = sink_pad.add_probe(Gst.PadProbeType.BUFFER, probe_cb)

        pipeline.set_state(Gst.State.PLAYING)
        bus = pipeline.get_bus()
        msg = bus.timed_pop_filtered(Gst.SECOND * 5, Gst.MessageType.EOS)
        self.assertIsNotNone(msg)

        # We should have seen exactly 5 buffers
        self.assertEqual(buffer_count, 5)

        # Check that last buffer has the expected PTS
        sample = sink.props.last_sample
        buffer = sample.get_buffer()
        self.assertEqual(buffer.pts, buffer_count * Gst.SECOND * 2)

        sink_pad.remove_probe(probe_id)
        pipeline.set_state(Gst.State.NULL)


if __name__ == "__main__":
    unittest.main()
