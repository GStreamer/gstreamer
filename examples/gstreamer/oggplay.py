#!/usr/bin/env python2.2

from gstreamer import *

def main():
   "Basic example to play an Ogg Vorbis stream through OSS"
   #gst_debug_set_categories(-1)

   if len(sys.argv) != 2:
      print 'usage: %s <Ogg Vorbis file>' % (sys.argv[0])
      return -1

   # create a new bin to hold the elements
   bin = gst_pipeline_new ('pipeline')

   # create a disk reader
   filesrc = gst_elementfactory_make ('filesrc', 'disk_source');
   if not filesrc:
      print 'could not find plugin \"filesrc\"'
      return -1
   filesrc.set_property('location', sys.argv[1])

   # now get the decoder
   decoder = gst_elementfactory_make ('vorbisdec', 'parse');
   if not decoder:
      print 'could not find plugin \"vorbisdec\"'
      return -1

   # and an audio sink
   osssink = gst_elementfactory_make ('osssink', 'play_audio')
   if not osssink:
      print 'could not find plugin \"osssink\"'
      return -1

   #  add objects to the main pipeline
   for e in (filesrc, decoder, osssink):
      bin.add(e)

   # connect the elements
   previous = None
   for e in (filesrc, decoder, osssink):
      if previous:
         previous.connect('src', e, 'sink')
      previous = e

   # start playing
   bin.set_state(STATE_PLAYING);

   while bin.iterate(): pass

   # stop the bin
   bin.set_state(STATE_NULL)

   return 0

if __name__ == '__main__':
   ret = main()
   sys.exit(ret)
