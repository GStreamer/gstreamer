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

"""GStreamer Debug Viewer GUI module."""

import os.path

import gi
gi.require_version('Gdk', '3.0')
gi.require_version('Gtk', '3.0')

from gi.repository import GObject
from gi.repository import Gdk
from gi.repository import Gtk

from GstDebugViewer import Common
from GstDebugViewer.GUI.columns import ViewColumnManager
from GstDebugViewer.GUI.window import Window


class AppStateSection (Common.GUI.StateSection):

    _name = "state"

    geometry = Common.GUI.StateInt4("window-geometry")
    maximized = Common.GUI.StateBool("window-maximized")

    column_order = Common.GUI.StateItemList("column-order", ViewColumnManager)
    columns_visible = Common.GUI.StateItemList(
        "columns-visible", ViewColumnManager)

    zoom_level = Common.GUI.StateInt("zoom-level")


class AppState (Common.GUI.State):

    def __init__(self, *a, **kw):

        Common.GUI.State.__init__(self, *a, **kw)

        self.add_section_class(AppStateSection)


class App (object):

    def __init__(self):

        self.attach()

    def load_plugins(self):

        from GstDebugViewer import Plugins

        plugin_classes = list(
            Plugins.load([os.path.dirname(Plugins.__file__)]))
        self.plugins = []
        for plugin_class in plugin_classes:
            plugin = plugin_class(self)
            self.plugins.append(plugin)

    def iter_plugin_features(self):

        for plugin in self.plugins:
            for feature in plugin.features:
                yield feature

    def attach(self):

        config_home = Common.utils.XDG.CONFIG_HOME

        state_filename = os.path.join(
            config_home, "gst-debug-viewer", "state")

        self.state = AppState(state_filename)
        self.state_section = self.state.sections["state"]

        self.load_plugins()

        self.windows = []

        # Apply custom widget stying
        # TODO: check for dark theme
        css = b"""
        @define-color normal_bg_color #FFFFFF;
        @define-color shade_bg_color shade(@normal_bg_color, 0.95);
        #log_view row:nth-child(even) {
            background-color: @normal_bg_color;
        }
        #log_view row:nth-child(odd) {
            background-color: @shade_bg_color;
        }
        #log_view row:selected {
            background-color: #4488FF;
        }
        #log_view {
          -GtkTreeView-horizontal-separator: 0;
          -GtkTreeView-vertical-separator: 1;
          outline-width: 0;
          outline-offset: 0;
        }
        """

        style_provider = Gtk.CssProvider()
        style_provider.load_from_data(css)

        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(),
            style_provider,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

        self.open_window()

    def detach(self):

        # TODO: If we take over deferred saving from the inspector, specify now
        # = True here!
        self.state.save()

    def run(self):

        try:
            Common.Main.MainLoopWrapper(Gtk.main, Gtk.main_quit).run()
        except BaseException:
            raise
        else:
            self.detach()

    def open_window(self):

        self.windows.append(Window(self))

    def close_window(self, window):

        self.windows.remove(window)
        if not self.windows:
            # GtkTreeView takes some time to go down for large files.  Let's block
            # until the window is hidden:
            GObject.idle_add(Gtk.main_quit)
            Gtk.main()

            Gtk.main_quit()
