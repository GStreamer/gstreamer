#!/usr/bin/env python
#
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>
#               2004 Thomas Vander Stichele <thomas at apestaart dot org>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

from gst import *
import gst.interface

def gst_props_debug_entry(entry, level=0):
    name = entry.get_name()
    type = entry.get_props_type()
    indent = ' '*level

    if type == PROPS_INT_TYPE:
        ret, val = entry.get_int()
        assert ret
        print '%s%s: int %d' % (indent, name, val)
    elif type == PROPS_FLOAT_TYPE:
        ret, val = entry.get_float()
        assert ret
        print '%s%s: float %f' % (indent, name, val)
    elif type == PROPS_FOURCC_TYPE:
        ret, val = entry.get_fourcc()
        assert ret
        print '%s%s: fourcc %c%c%c%c' % (indent, name,
                    (val>>0)&0xff,
                    (val>>8)&0xff,
                    (val>>16)&0xff,
                    (val>>24)&0xff)
    elif type == PROPS_BOOLEAN_TYPE:
        ret, val = entry.get_bool()
        assert ret
        print '%s%s: bool %d' % (indent, name, val)
    elif type == PROPS_STRING_TYPE:
        ret, val = entry.get_string()
        assert ret
        print '%s%s: string "%s"' % (indent, name, val)
    elif type == PROPS_INT_RANGE_TYPE:
        ret, min, max = entry.get_int_range()
        assert ret
        print '%s%s: int range %d-%d' % (indent, name, min, max)
    elif type == PROPS_FLOAT_RANGE_TYPE:
        ret, min, max = entry.get_float_range()
        assert ret
        print '%s%s: float range %f-%f' % (indent, name, min, max)
    elif type == PROPS_LIST_TYPE:
        ret, val = entry.get_list()
        assert ret
        print '[list] ('
        for e in val:
            gst_props_debug_entry(e, level+1)
        print ')'
    else:
        print '%sWARNING: %s: unknown property type %d' % (indent, name, type)

def debug_caps(caps):
    props = caps.get_props()
    ret, plist = props.get_list()
    for e in plist:
        gst_props_debug_entry(e, level=1)

def main():
    "example for v4l element"

    # create a new bin to hold the elements
    bin = Pipeline('pipeline')

    # create a v4l reader
    src = Element('v4lsrc', 'src')

    # colorspace
    csp = Element('ffmpegcolorspace', 'csp')

    # displayer
    sink = Element('ximagesink', 'sink')

    #  add and link
    bin.add_many(src, csp, sink)
    src.link(csp)
    csp.link(sink)

    # start playing
    bin.set_state(STATE_PLAYING);
    dir(src)
    src.get_channel()

    while bin.iterate(): pass

    # stop the bin
    bin.set_state(STATE_NULL)

    return 0

if __name__ == '__main__':
    ret = main()
    sys.exit(ret)
