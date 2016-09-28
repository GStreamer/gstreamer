# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Debug Viewer - View and analyze GStreamer debug log files
#
#  Copyright (C) 2007 René Stadler <mail@renestadler.de>
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 3 of the License, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program.  If not, see <http://www.gnu.org/licenses/>.

"""GStreamer Debug Viewer row colorization plugin."""

from GstDebugViewer.Plugins import FeatureBase, PluginBase


class ColorizeLevels (FeatureBase):

    def attach(self, window):

        pass

    def detach(self, window):

        pass


class LevelColorSentinel (object):

    def processor(self, proc):

        for row in proc:

            yield None


class ColorizeCategories (FeatureBase):

    def attach(self, window):

        pass

    def detach(self, window):

        pass


class CategoryColorSentinel (object):

    def processor(self):

        pass


class Plugin (PluginBase):

    features = [ColorizeLevels, ColorizeCategories]
