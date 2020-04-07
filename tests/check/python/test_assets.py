# -*- coding: utf-8 -*-
#
# Copyright (c) 2019 Thibault Saunier <tsaunier@igalia.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

from . import overrides_hack

import os
import gi
import tempfile

gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GLib  # noqa
from gi.repository import GES  # noqa
import unittest  # noqa
from unittest import mock

try:
    gi.require_version("GstTranscoder", "1.0")
    from gi.repository import GstTranscoder
except ValueError:
    GstTranscoder = None

from . import common
from .common import GESSimpleTimelineTest  # noqa

Gst.init(None)
GES.init()


class TestTimeline(GESSimpleTimelineTest):

    def test_request_relocated_assets_sync(self):
        path = os.path.join(__file__, "../../../", "png.png")
        with self.assertRaises(GLib.Error):
            GES.UriClipAsset.request_sync(Gst.filename_to_uri(path))

        GES.add_missing_uri_relocation_uri(Gst.filename_to_uri(os.path.join(__file__, "../../assets")), False)
        path = os.path.join(__file__, "../../", "png.png")
        self.assertEqual(GES.UriClipAsset.request_sync(Gst.filename_to_uri(path)).props.id,
            Gst.filename_to_uri(os.path.join(__file__, "../../assets/png.png")))

    def test_request_relocated_twice(self):
        GES.add_missing_uri_relocation_uri(Gst.filename_to_uri(os.path.join(__file__, "../../")), True)
        proj = GES.Project.new()

        asset = proj.create_asset_sync("file:///png.png", GES.UriClip)
        self.assertIsNotNone(asset)
        asset = proj.create_asset_sync("file:///png.png", GES.UriClip)
        self.assertIsNotNone(asset)

    @unittest.skipIf(GstTranscoder is None, "GstTranscoder is not available")
    @unittest.skipIf(Gst.ElementFactory.make("testsrcbin") is None, "testbinsrc is not available")
    def test_reload_asset(self):
        with tempfile.NamedTemporaryFile(suffix=".ogg") as f:
            uri = Gst.filename_to_uri(f.name)
            transcoder = GstTranscoder.Transcoder.new("testbin://video,num-buffers=30",
                uri, "application/ogg:video/x-theora:audio/x-vorbis")
            transcoder.run()

            asset0 = GES.UriClipAsset.request_sync(uri)
            self.assertEqual(asset0.props.duration, Gst.SECOND)

            transcoder = GstTranscoder.Transcoder.new("testbin://video,num-buffers=60",
                uri, "application/ogg:video/x-theora:audio/x-vorbis")
            transcoder.run()

            GES.Asset.needs_reload(GES.UriClip, uri)
            asset1 = GES.UriClipAsset.request_sync(uri)
            self.assertEqual(asset1.props.duration, 2 * Gst.SECOND)
            self.assertEqual(asset1, asset0)

            transcoder = GstTranscoder.Transcoder.new("testbin://video,num-buffers=90",
                uri, "application/ogg:video/x-theora:audio/x-vorbis")
            transcoder.run()
            mainloop = common.create_main_loop()
            def asset_loaded_cb(_, res, mainloop):
                asset2 = GES.Asset.request_finish(res)
                self.assertEqual(asset2.props.duration, 3 * Gst.SECOND)
                self.assertEqual(asset2, asset0)
                mainloop.quit()

            GES.Asset.needs_reload(GES.UriClip, uri)
            GES.Asset.request_async(GES.UriClip, uri, None, asset_loaded_cb, mainloop)
            mainloop.run()
