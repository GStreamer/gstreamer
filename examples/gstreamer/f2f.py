#!/usr/bin/env python2.2

from gobject import GObject
from gstreamer import *

def handoff(sender, *args):
   print sender.get_name(), args

def main():
   # create a new bin to hold the elements
   #gst_debug_set_categories(-1)
   bin = gst_pipeline_new ('pipeline')
   assert bin

   src = gst_elementfactory_make ('fakesrc', 'src')
   assert src
   GObject.connect(src, 'handoff', handoff)
   src.set_property('silent', 1)
   src.set_property('num_buffers', 10)

   sink = gst_elementfactory_make ('fakesink', 'sink')
   assert sink
   GObject.connect(sink, 'handoff', handoff)
   src.set_property('silent', 1)

   #  add objects to the main pipeline
   for e in (src, sink):
      bin.add(e)

   # connect the elements
   res = src.connect('src', sink, 'sink')
   assert res

   # start playing
   res = bin.set_state(STATE_PLAYING);
   assert res

   while bin.iterate(): pass

   # stop the bin
   res = bin.set_state(STATE_NULL)
   assert res

if __name__ == '__main__':
   main()
