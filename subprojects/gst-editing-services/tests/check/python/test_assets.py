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
gi.require_version("GstPbutils", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GstPbutils  # noqa
from gi.repository import GLib  # noqa
from gi.repository import GES  # noqa
import unittest  # noqa
from unittest import mock

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

    @unittest.skipUnless(*common.can_generate_assets())
    def test_reload_asset(self):
        with common.created_video_asset(num_bufs=2, framerate="2/1") as uri:
            asset0 = GES.UriClipAsset.request_sync(uri)
            self.assertEqual(asset0.props.duration, Gst.SECOND)

            with common.created_video_asset(uri, 4, framerate="2/1") as uri:
                GES.Asset.needs_reload(GES.UriClip, uri)
                asset1 = GES.UriClipAsset.request_sync(uri)
                self.assertEqual(asset1.props.duration, 2 * Gst.SECOND)
                self.assertEqual(asset1, asset0)

                with common.created_video_asset(uri, 6, framerate="2/1") as uri:
                    mainloop = common.create_main_loop()

                    def asset_loaded_cb(_, res, mainloop):
                        asset2 = GES.Asset.request_finish(res)
                        self.assertEqual(asset2.props.duration, 3 * Gst.SECOND)
                        self.assertEqual(asset2, asset0)
                        mainloop.quit()

                    GES.Asset.needs_reload(GES.UriClip, uri)
                    GES.Asset.request_async(GES.UriClip, uri, None, asset_loaded_cb, mainloop)
                    mainloop.run()

    def test_asset_metadata_on_reload(self):
        mainloop = GLib.MainLoop()

        unused, xges_path = tempfile.mkstemp(suffix=".xges")
        project_uri = Gst.filename_to_uri(os.path.abspath(xges_path))

        asset_uri = Gst.filename_to_uri(os.path.join(__file__, "../../assets/audio_video.ogg"))
        xges = """<ges version='0.3'>
            <project properties='properties;' metadatas='metadatas;'>
                <ressources>
                    <asset id='%(uri)s' extractable-type-name='GESUriClip' properties='properties, supported-formats=(int)6, duration=(guint64)2003000000;' metadatas='metadatas, container-format=(string)Matroska, language-code=(string)und, application-name=(string)Lavc56.60.100, encoder-version=(uint)0, audio-codec=(string)Vorbis, nominal-bitrate=(uint)80000, bitrate=(uint)80000, video-codec=(string)&quot;On2\ VP8&quot;, file-size=(guint64)223340, foo=(string)bar;' >
                    </asset>
                </ressources>
            </project>
            </ges>""" % {"uri": asset_uri}
        with open(xges_path, "w") as xges_file:
            xges_file.write(xges)

        def loaded_cb(project, timeline):
            asset = project.list_assets(GES.Extractable)[0]
            self.assertEqual(asset.get_meta("foo"), "bar")
            mainloop.quit()

        loaded_project = GES.Project(uri=project_uri, extractable_type=GES.Timeline)
        loaded_project.connect("loaded", loaded_cb)
        timeline = loaded_project.extract()
        mainloop.run()

    def test_asset_load_serialized_info(self):
        mainloop = GLib.MainLoop()

        serialized_infos = {}
        n_calls = 0
        n_cache_hits = 0

        def load_serialized_info_cb(_manager, uri):
            nonlocal n_calls, n_cache_hits, serialized_infos

            n_calls += 1
            res = serialized_infos.get(uri)
            if res:
                n_cache_hits += 1
            return res

        GES.DiscovererManager.get_default().connect("load-serialized-info",
                                                    load_serialized_info_cb)

        self.assertEqual(n_calls, 0)
        asset = GES.UriClipAsset.request_sync(Gst.filename_to_uri(os.path.join(__file__, "../../assets/audio_video.ogg")))
        self.assertEqual(n_calls, 1)
        self.assertEqual(n_cache_hits, 0)

        serialized_infos[asset.get_id()] = asset.get_info()

        # Clear the GES internal asset cache
        GES.deinit()
        GES.init()

        # Connect to the new manager, previous one was destroyed on deinit
        GES.DiscovererManager.get_default().connect("load-serialized-info",
                                                    load_serialized_info_cb)
        asset = GES.UriClipAsset.request_sync(Gst.filename_to_uri(os.path.join(__file__, "../../assets/audio_video.ogg")))
        self.assertEqual(n_calls, 2)
        self.assertEqual(n_cache_hits, 1)
