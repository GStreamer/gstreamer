#!/usr/bin/env python
#
# Copyright (C) 2004 David I. Lehn
#               2004 Johan Dahlin
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

import sys
import gst
import gst.play

def nano2str(nanos):
    ts = nanos / gst.SECOND
    return '%02d:%02d:%02d.%06d' % (ts / 3600,
                                    ts / 60,
                                    ts % 60,
                                    nanos % gst.SECOND)

def stream_length_cb(play, ns):
    print 'stream length: %s' % nano2str(ns)

def have_video_size_cb(play, w, h):
    print 'video size %d %d' % (w, h)

def found_tag_cb(play, src, tags):
    for tag in tags.keys():
        print "%s: %s" % (gst.tag_get_nick(tag), tags[tag])

def main(args):
    if len(args) != 2:
        print 'Usage: %s file' % args[0]
        return -1

    filename = args[1]
    
    play = gst.play.Play()
    play.connect('stream-length', stream_length_cb)
    play.connect('have-video-size', have_video_size_cb)
    play.connect('found-tag', found_tag_cb)
    play.connect('eos', lambda p: gst.main_quit())

    # Setup source and sinks
    play.set_data_src(gst.element_factory_make('filesrc'))
    play.set_audio_sink(gst.element_factory_make('osssink'))
    play.set_video_sink(gst.element_factory_make('fakesink'))

    # Point location to our filename
    play.set_location(filename)

    # Start playing the stream
    play.set_state(gst.STATE_PLAYING)
    gst.main()
    
if __name__ == '__main__':
    sys.exit(main(sys.argv))
