#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2003 David I. Lehn
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
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import sys
import gst

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

def streaminfo(sender, pspec):
    assert pspec.name == 'streaminfo'
    caps = sender.get_property(pspec.name)
    print 'streaminfo:'
    debug_caps(caps)

def metadata(sender, pspec):
    assert pspec.name == 'metadata'
    caps = sender.get_property(pspec.name)
    print 'metadata:'
    debug_caps(caps)

def decoder_notified(sender, pspec):
    if pspec.name == 'streaminfo':
        streaminfo(sender, pspec)
    elif pspec.name == 'metadata':
        metadata(sender, pspec)
    else:
        print 'notify:', sender, pspec
                                     
def main(args):
    "Basic example to play an Ogg Vorbis stream through OSS"

    if len(args) != 2:
        print 'usage: %s <Ogg Vorbis file>' % args
        return -1
    
    bin = gst.parse_launch('filesrc name=source ! ' + 
                           'oggdemux name=demuxer ! ' + 
                           'vorbisdec name=decoder ! ' + 
                           'audioconvert ! osssink') 
    filesrc = bin.get_by_name('source')
    filesrc.set_property('location', args[1])
    demuxer = bin.get_by_name('demuxer')
    demuxer.connect('notify', decoder_notified)
    decoder = bin.get_by_name('decoder')
    decoder.connect('notify', decoder_notified)

    # start playing
    bin.set_state(gst.STATE_PLAYING);

    try:
        while bin.iterate(): 
	    pass
    except KeyboardInterrupt:
        pass

    # stop the bin
    bin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
