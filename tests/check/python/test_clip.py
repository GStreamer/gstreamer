# -*- coding: utf-8 -*-
#
# Copyright (c) 2015, Thibault Saunier
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

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GES  # noqa
import unittest  # noqa

Gst.init(None)
GES.init()


class TestCopyPaste(unittest.TestCase):

    def setUp(self):
        self.timeline = GES.Timeline.new_audio_video()
        self.assertEqual(len(self.timeline.get_tracks()), 2)
        self.layer = self.timeline.append_layer()

    def testCopyClipRemoveAndPaste(self):
        clip1 = GES.TestClip.new()
        clip1.props.duration = 10

        self.layer.add_clip(clip1)

        self.assertEqual(len(clip1.get_children(False)), 2)

        copy = clip1.copy(True)
        self.assertEqual(len(self.layer.get_clips()), 1)

        self.layer.remove_clip(clip1)

        copy.paste(10)
        self.assertEqual(len(self.layer.get_clips()), 1)

    def testCopyPasteTitleClip(self):
        clip1 = GES.TitleClip.new()
        clip1.props.duration = 10

        self.layer.add_clip(clip1)
        self.assertEqual(len(clip1.get_children(False)), 1)

        copy = clip1.copy(True)
        self.assertEqual(len(self.layer.get_clips()), 1)

        copy.paste(10)
        self.assertEqual(len(self.layer.get_clips()), 2)


class TestTitleClip(unittest.TestCase):
    def testGetPropertyNotInTrack(self):
        title_clip = GES.TitleClip.new()
        self.assertEqual(title_clip.props.text, "")
        self.assertEqual(title_clip.props.font_desc, "Serif 36")
