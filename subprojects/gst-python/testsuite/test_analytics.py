# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2024 Collabora Ltd
#  Author: Olivier Crête <olivier.crete@collabora.com>
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
overrides_hack

from common import TestCase
import unittest
import sys

import gi
gi.require_version("GLib", "2.0")
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib
from gi.repository import Gst
from gi.repository import GstAnalytics
Gst.init(None)


class TestAnalyticsODMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        m2 = GstAnalytics.buffer_get_analytics_relation_meta(buf)
        self.assertEqual(meta, m2)

        qk = GLib.quark_from_string("testQuark")

        (ret, mtd) = meta.add_od_mtd(qk, 10, 20, 30, 40, 0.3)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        (ret, mtd) = meta.get_od_mtd(0)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        # Ensure there is no mtd 1, only 0
        (ret, _) = meta.get_mtd(1, GstAnalytics.MTD_TYPE_ANY)
        self.assertFalse(ret)

        # The is only one od mtd
        (ret, _) = meta.get_od_mtd(1)
        self.assertFalse(ret)

        # There is no Class mtd
        (ret, _) = meta.get_cls_mtd(0)
        self.assertFalse(ret)

        # meta and m2 should return the same tuple
        self.assertEqual(meta.get_od_mtd(0)[1].get_location(),
                         m2.get_od_mtd(0)[1].get_location())

        self.assertEqual(mtd.get_obj_type(), qk)

        location = meta.get_od_mtd(0)[1].get_location()
        self.assertEqual(location[1], 10)
        self.assertEqual(location[2], 20)
        self.assertEqual(location[3], 30)
        self.assertEqual(location[4], 40)
        self.assertAlmostEqual(location[5], 0.3, 3)


class TestAnalyticsClsMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        qks = (GLib.quark_from_string("q1"),
              GLib.quark_from_string("q2"),
              GLib.quark_from_string("q3"))

        (ret, mtd) = meta.add_cls_mtd([0.1, 0.2, 0.3], qks)
        self.assertTrue(ret)
        self.assertIsNotNone(mtd)

        cnt = mtd.get_length()
        self.assertEqual(cnt, 3)

        for i in range(cnt):
            self.assertEqual(mtd.get_index_by_quark(qks[i]), i)
            self.assertAlmostEqual(mtd.get_level(i), (i + 1) / 10, 7)
            self.assertEqual(mtd.get_quark(i), qks[i])


class TestAnalyticsTrackingMtd(TestCase):
    def test(self):
        buf = Gst.Buffer()
        self.assertIsNotNone(buf)

        meta = GstAnalytics.buffer_add_analytics_relation_meta(buf)
        self.assertIsNotNone(meta)

        (ret, mtd) = meta.add_tracking_mtd(1, 10)
        self.assertTrue(ret)
        rets = mtd.get_info()
        self.assertFalse(rets.tracking_lost)
        self.assertEqual(rets.tracking_first_seen, 10)
        self.assertEqual(rets.tracking_last_seen, 10)

        mtd.update_last_seen(20)

        rets = mtd.get_info()
        self.assertEqual(rets.tracking_first_seen, 10)
        self.assertEqual(rets.tracking_last_seen, 20)

        mtd.set_lost()
        rets = mtd.get_info()
        self.assertTrue(rets.tracking_lost)
