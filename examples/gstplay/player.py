#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2004 David I. Lehn
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

#
# GstPlay wrapper demo
#

import sys
import gobject
from gstreamer import *
from gstplay import Play

try:
    threads_init()
except Exception, e:
    print e

def nano_time_string(nanos):
    ts = nanos / 1000000000
    h = ts / 3600
    m = ts / 60
    s = ts % 60
    us = nanos % 1000000000
    return '%02d:%02d:%02d.%06d' % (h, m, s, us)

def got_time_tick(sender, nanos):
    print 'time tick %s (%d)' % (nano_time_string(nanos), nanos)

def got_stream_length(sender, nanos):
    print 'stream length %s (%d)' % (nano_time_string(nanos), nanos)

def got_have_video_size(sender, w, h):
    print 'video size %d %d' % (w, h)

def got_found_tag(sender, src, tags, *args):
    def fe(tl, tag):
        c = tl.get_tag_size(tag)
        #print tl, tag, c
        for i in range(c):
            v = tl.get_value_index(tag, i)
            #print tag, type(v)
            if i == 0:
                s = gst_tag_get_nick(tag)
            else:
                s = ' '
            print "%15s: %s" % (s, v)
    print 'found tag', src, tags, args
    tags.foreach(fe)

def got_eos(sender, loop):
    print 'eos', args
    loop.quit()

def idle_iterate(sender):
    #threads_enter()
    return sender.iterate()
    #threads_leave()
    #return sender.get_state() == STATE_PLAYING

def main():
    "Basic example to play a media stream with GstPlay"
    #gst_debug_set_default_threshold(LEVEL_INFO)

    if len(sys.argv) != 2:
        print 'usage: %s <media file>' % (sys.argv[0])
        return -1

    #threads_enter()

    loop = gobject.MainLoop()

    # the player
    play = Play ()
    play.connect('time_tick', got_time_tick)
    play.connect('stream_length', got_stream_length)
    play.connect('have_video_size', got_have_video_size)
    play.connect('found_tag', got_found_tag)
    play.connect('eos', got_eos, loop)

    data_src = Element ('gnomevfssrc', 'data_src')
    #audio_sink = Element ('osssink', 'audio_sink')
    audio_sink = Element ('fakesink', 'audio_sink')
    video_sink = Element ('fakesink', 'video_sink')
    #video_sink = Element ('aasink', 'video_sink')
    #video_sink.set_property('driver', 4)
    #vis_sink = Element ('fakesink', 'vis_sink')

    # setup the player
    play.set_data_src(data_src)
    play.set_audio_sink(audio_sink)
    play.set_video_sink(video_sink)
    #play.set_visualization(vis_sink)
    play.set_location(sys.argv[1])

    # start playing
    play.set_state(STATE_PLAYING);

    #while play.iterate(): pass
    #while play.iterate(): print '.'
    gobject.idle_add(idle_iterate, play)
    #iterid = add_iterate_bin(play)

    #import gtk
    #gtk.threads_enter()
    loop.run()
    #gtk.threads_leave()

    #threads_leave()

    # stop the bin
    play.set_state(STATE_NULL)

    return 0

if __name__ == '__main__':
    ret = main()
    sys.exit(ret)
