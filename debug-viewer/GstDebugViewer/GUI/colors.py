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

from gi.repository import Gtk
from gi.repository import Gdk

from GstDebugViewer import Data


class Color (object):

    def __init__(self, hex_24):

        if hex_24.startswith("#"):
            s = hex_24[1:]
        else:
            s = hex_24

        self._fields = tuple((int(hs, 16) for hs in (s[:2], s[2:4], s[4:],)))

    def gdk_color(self):

        return Gdk.color_parse(self.hex_string())

    def hex_string(self):

        return "#%02x%02x%02x" % self._fields

    def float_tuple(self):

        return tuple((float(x) / 255 for x in self._fields))

    def byte_tuple(self):

        return self._fields

    def short_tuple(self):

        return tuple((x << 8 for x in self._fields))


class ColorPalette (object):

    @classmethod
    def get(cls):

        try:
            return cls._instance
        except AttributeError:
            cls._instance = cls()
            return cls._instance


class TangoPalette (ColorPalette):

    def __init__(self):

        for name, r, g, b in [("black", 0, 0, 0,),
                              ("white", 255, 255, 255,),
                              ("butter1", 252, 233, 79),
                              ("butter2", 237, 212, 0),
                              ("butter3", 196, 160, 0),
                              ("chameleon1", 138, 226, 52),
                              ("chameleon2", 115, 210, 22),
                              ("chameleon3", 78, 154, 6),
                              ("orange1", 252, 175, 62),
                              ("orange2", 245, 121, 0),
                              ("orange3", 206, 92, 0),
                              ("skyblue1", 114, 159, 207),
                              ("skyblue2", 52, 101, 164),
                              ("skyblue3", 32, 74, 135),
                              ("plum1", 173, 127, 168),
                              ("plum2", 117, 80, 123),
                              ("plum3", 92, 53, 102),
                              ("chocolate1", 233, 185, 110),
                              ("chocolate2", 193, 125, 17),
                              ("chocolate3", 143, 89, 2),
                              ("scarletred1", 239, 41, 41),
                              ("scarletred2", 204, 0, 0),
                              ("scarletred3", 164, 0, 0),
                              ("aluminium1", 238, 238, 236),
                              ("aluminium2", 211, 215, 207),
                              ("aluminium3", 186, 189, 182),
                              ("aluminium4", 136, 138, 133),
                              ("aluminium5", 85, 87, 83),
                              ("aluminium6", 46, 52, 54)]:
            setattr(self, name, Color("%02x%02x%02x" % (r, g, b,)))


class ColorTheme (object):

    def __init__(self):

        self.colors = {}

    def add_color(self, key, *colors):

        self.colors[key] = colors


class LevelColorTheme (ColorTheme):

    pass


class LevelColorThemeTango (LevelColorTheme):

    def __init__(self):

        LevelColorTheme.__init__(self)

        p = TangoPalette.get()
        self.add_color(Data.debug_level_none, None, None, None)
        self.add_color(Data.debug_level_trace, p.black, p.aluminium2)
        self.add_color(Data.debug_level_fixme, p.black, p.butter3)
        self.add_color(Data.debug_level_log, p.black, p.plum1)
        self.add_color(Data.debug_level_debug, p.black, p.skyblue1)
        self.add_color(Data.debug_level_info, p.black, p.chameleon1)
        self.add_color(Data.debug_level_warning, p.black, p.orange1)
        self.add_color(Data.debug_level_error, p.white, p.scarletred1)
        self.add_color(Data.debug_level_memdump, p.white, p.aluminium3)


class ThreadColorTheme (ColorTheme):

    pass


class ThreadColorThemeTango (ThreadColorTheme):

    def __init__(self):

        ThreadColorTheme.__init__(self)

        t = TangoPalette.get()
        for i, color in enumerate([t.butter2,
                                   t.orange2,
                                   t.chocolate3,
                                   t.chameleon2,
                                   t.skyblue1,
                                   t.plum1,
                                   t.scarletred1,
                                   t.aluminium6]):
            self.add_color(i, color)
