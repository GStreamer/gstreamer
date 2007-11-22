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

"""GStreamer Debug Viewer Plugins package."""

__all__ = ["_", "FeatureBase", "PluginBase"]

import os.path
from gettext import gettext as _

def load (paths = ()):

    for path in paths:
        for plugin_module in _load_plugins (path):
            yield plugin_module.Plugin

def _load_plugins (path):

    import imp, glob

    files = glob.glob (os.path.join (path, "*.py"))

    for filename in files:

        name = os.path.basename (os.path.splitext (filename)[0])
        if name == "__init__":
            continue
        fp, pathname, description = imp.find_module (name, [path])
        module = imp.load_module (name, fp, pathname, description)
        yield module

class FeatureBase (object):

    state_section_name = None

    def handle_log_file_changed (self):

        pass

class PluginBase (object):

    features = ()

    def __init__ (self):

        pass
