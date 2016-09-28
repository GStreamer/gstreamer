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

"""GStreamer Debug Viewer file properties plugin."""

import logging

from GstDebugViewer.Plugins import FeatureBase, PluginBase

from gettext import gettext as _
from gi.repository import Gtk


class FilePropertiesSentinel (object):

    pass


class FilePropertiesDialog (Gtk.Dialog):

    pass


class FilePropertiesFeature (FeatureBase):

    def __init__(self, *a, **kw):

        self.action_group = Gtk.ActionGroup("FilePropertiesActions")
        self.action_group.add_actions(
            [("show-file-properties", Gtk.STOCK_PROPERTIES,
              _("_Properties"), "<Ctrl>P")])

    def attach(self, window):

        ui = window.ui_manager
        ui.insert_action_group(self.action_group, 0)

        self.merge_id = ui.new_merge_id()
        ui.add_ui(self.merge_id, "/menubar/FileMenu/FileMenuAdditions",
                  "FileProperties", "show-file-properties",
                  Gtk.UIManagerItemType.MENUITEM, False)

        handler = self.handle_action_activate
        self.action_group.get_action(
            "show-file-properties").connect("activate", handler)

    def handle_action_activate(self, action):

        pass


class Plugin (PluginBase):

    features = (FilePropertiesFeature,)
