#!/usr/bin/env python2.2
#
# gst-python
# Copyright (C) 2002 David I. Lehn
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
# Author: David I. Lehn <dlehn@vt.edu>
#

import sys
#from gnome import *
from gstreamer import *
from gobject import GObject
import gtk

class DVDPlay(object):
   def __init__(self):
      pass

   def idle(self, pipeline):
      pipeline.iterate()
      return 1

   def eof(self, sender):
      print 'EOS, quiting'
      sys.exit(0)
      
   def mpegparse_newpad(self, parser, pad, pipeline):
      print '***** a new pad %s was created' % pad.get_name()
      if pad.get_name()[:6] == 'video_':
         pad.connect(self.v_queue.get_pad('sink'))
         self.pipeline.add(self.v_thread)
         self.v_thread.set_state(STATE_PLAYING)
      elif pad.get_name() == 'private_stream_1.0':
         pad.connect(self.a_queue.get_pad('sink'))
         self.pipeline.add(self.a_thread)
         self.a_thread.set_state(STATE_PLAYING);
      else:
         print 'unknown pad: %s' % pad.get_name()

   def mpegparse_have_size(self, videosink, width, height):
      self.gtk_socket.set_usize(width,height)
      self.appwindow.show_all()

   def main(self):
      if len(sys.argv) < 5:
         print 'usage: %s dvdlocation title chapter angle' % argv[0]
         return -1

      self.location = sys.argv[1]
      self.title = int(sys.argv[2])
      self.chapter = int(sys.argv[3])
      self.angle = int(sys.argv[4])

      #gst_init(&argc,&argv);
      #gnome_init('MPEG2 Video player','0.0.1',argc,argv);

      ret = self.build()
      if ret:
         return ret

      ret = self.run()
      return ret

   def build_video_thread(self):
      # ***** pre-construct the video thread *****
      self.v_thread = gst_thread_new('v_thread')
      assert self.v_thread

      self.v_queue = gst_elementfactory_make('queue','v_queue')
      assert self.v_queue

      self.v_decode = gst_elementfactory_make('mpeg2dec','decode_video')
      assert self.v_decode

      self.color = gst_elementfactory_make('colorspace','color')
      assert self.color

      self.show = gst_elementfactory_make('videosink','show')
      assert self.show

      for e in (self.v_queue, self.v_decode, self.color, self.show):
         self.v_thread.add(e)

      self.v_queue.connect('src',self.v_decode,'sink')
      self.v_decode.connect('src',self.color,'sink')
      self.color.connect('src',self.show,'sink')

   def build_audio_thread(self):
      # ***** pre-construct the audio thread *****
      self.a_thread = gst_thread_new('a_thread')
      assert self.a_thread

      self.a_queue = gst_elementfactory_make('queue','a_queue')
      assert self.a_queue

      self.a_decode = gst_elementfactory_make('a52dec','decode_audio')
      assert self.a_decode

      self.osssink = gst_elementfactory_make('osssink','osssink')
      assert self.osssink

      for e in (self.a_queue, self.a_decode, self.osssink):
         self.a_thread.add(e)

      self.a_queue.connect('src',self.a_decode,'sink')
      self.a_decode.connect('src',self.osssink,'sink')

   def build(self):
      # ***** construct the main pipeline *****
      self.pipeline = gst_pipeline_new('pipeline')
      assert self.pipeline

      self.src = gst_elementfactory_make('dvdsrc','src');
      assert self.src

      self.src.set_property('location', self.location)
      self.src.set_property('title', self.title)
      self.src.set_property('chapter', self.chapter)
      self.src.set_property('angle', self.angle)

      self.parse = gst_elementfactory_make('mpegdemux','parse')
      assert self.parse

      self.pipeline.add(self.src)
      self.pipeline.add(self.parse)

      self.src.connect('src',self.parse,'sink')

      # pre-construct the audio/video threads
      self.build_video_thread()
      self.build_audio_thread()

      # ***** construct the GUI *****
      #self.appwindow = gnome_app_new('DVD Player','DVD Player')

      #self.gtk_socket = gtk_socket_new ()
      #gtk_socket.show()

      #gnome_app_set_contents(GNOME_APP(appwindow),
            #GTK_WIDGET(gtk_socket));

      #gtk_widget_realize (gtk_socket);
      #gtk_socket_steal (GTK_SOCKET (gtk_socket), 
            #gst_util_get_int_arg (GTK_OBJECT(show), 'xid'));

      GObject.connect(self.parse,'new_pad',self.mpegparse_newpad, self.pipeline)
      GObject.connect(self.src,'eos',self.eof)
      #GObject.connect(show,'have_size',self.mpegparse_have_size, self.pipeline)

      return 0

   def run(self):
      print 'setting to PLAYING state'

      self.pipeline.set_state(STATE_PLAYING)

      gtk.idle_add(self.idle,self.pipeline)

      #gtk.threads_enter()
      gtk.main()
      #gtk.threads_leave()

      self.pipeline.set_state(STATE_NULL)

      return 0

if __name__ == '__main__':
   #gst_debug_set_categories(-1)
   player = DVDPlay()
   ret = player.main()
   sys.exit(ret)
