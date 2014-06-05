#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# audioconcat.py - Concatenates multiple audio files to single ogg/vorbis file
# Uses the gnonlin elements (http://gnonlin.sf.net/)
# Copyright (C) 2005 Edward Hervey <edward@fluendo.com>
#               2006 Jason Gerard DeRose <jderose@jasonderose.org>
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
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst
from gst.extend.discoverer import Discoverer



class AudioDec(gst.Bin):
	'''Decodes audio file, outputs at specified caps'''

	def __init__(self, location, caps):
		gst.Bin.__init__(self)

		# Create elements
		src = gst.element_factory_make('filesrc')
		dec = gst.element_factory_make('decodebin')
		conv = gst.element_factory_make('audioconvert')
		rsmpl = gst.element_factory_make('audioresample')
		ident = gst.element_factory_make('identity')

		# Set 'location' property on filesrc
		src.set_property('location', location)

		# Connect handler for 'new-decoded-pad' signal 
		dec.connect('new-decoded-pad', self.__on_new_decoded_pad)

		# Add elements to bin
		self.add(src, dec, conv, rsmpl, ident)

		# Link *some* elements 
		# This is completed in self.__on_new_decoded_pad()
		src.link(dec)
		conv.link(rsmpl)
		rsmpl.link(ident, caps)

		# Reference used in self.__on_new_decoded_pad()
		self.__apad = conv.get_pad('sink')

		# Add ghost pad
		self.add_pad(gst.GhostPad('src', ident.get_pad('src')))


	def __on_new_decoded_pad(self, element, pad, last):
		caps = pad.get_caps()
		name = caps[0].get_name()
		print '\n__on_new_decoded_pad:', name
		if 'audio' in name:
			if not self.__apad.is_linked(): # Only link once
				pad.link(self.__apad)




class AudioConcat:
	'''Concatenates multiple audio files to single ogg/vorbis file'''

	caps = gst.caps_from_string('audio/x-raw-float, rate=44100, channels=2, endianness=1234, width=32')
	
	def __init__(self, infiles, outfile):
		# These are used in iteration through infiles	
		self.infiles = infiles
		self.i = 0
		self.start = 0L

		# The pipeline
		self.pipeline = gst.Pipeline()

		# Create bus and connect 'eos' and 'error' handlers
		self.bus = self.pipeline.get_bus()
		self.bus.add_signal_watch()
		self.bus.connect('message::eos', self.on_eos)
		self.bus.connect('message::error', self.on_error)

		# Create elements
		self.comp = gst.element_factory_make('gnlcomposition')
		self.enc = gst.element_factory_make('vorbisenc')
		self.mux = gst.element_factory_make('oggmux')
		self.sink = gst.element_factory_make('filesink')

		# Connect handler for 'pad-added' signal 
		self.comp.connect('pad-added', self.on_pad_added)	

		# Set 'location' property on filesink
		self.sink.set_property('location', outfile)

		# Add elements to pipeline
		self.pipeline.add(self.comp, self.enc, self.mux, self.sink)

		# Link *some* elements
		# This in completed in self.on_pad_added()
		gst.element_link_many(self.enc, self.mux, self.sink)

		# Reference used in self.on_pad_added()
		self.apad = self.enc.get_pad('sink')

		# The MainLoop
		self.mainloop = gobject.MainLoop()

		# Iterate through infiles
		gobject.idle_add(self.discover)
		self.mainloop.run()


	def discover(self):
		infile = self.infiles[self.i]
		discoverer = Discoverer(infile)
		discoverer.connect('discovered', self.on_discovered, infile)
		discoverer.discover()
		return False # Don't repeat idle call


	def on_discovered(self, discoverer, ismedia, infile):
		print '\non_discovered:', infile
		discoverer.print_info()
		if discoverer.is_audio:
			dec = AudioDec(infile, self.caps)
			src = gst.element_factory_make('gnlsource')
			src.add(dec)
			src.set_property('media-start', 0L)
			src.set_property('media-duration', discoverer.audiolength)
			src.set_property('start', self.start)
			src.set_property('duration', discoverer.audiolength)
			self.comp.add(src)
			self.start += discoverer.audiolength
		self.i += 1
		if self.i < len(self.infiles):
			gobject.idle_add(self.discover)
		else:
			if self.start > 0: # At least 1 infile is_audio and audiolength > 0
				self.pipeline.set_state(gst.STATE_PLAYING)
			else:
				self.mainloop.quit()


	def on_pad_added(self, element, pad):
		caps = pad.get_caps()
		name = caps[0].get_name()
		print '\non_pad_added:', name
		if name == 'audio/x-raw-float':
			if not self.apad.is_linked(): # Only link once
				pad.link(self.apad)


	def on_eos(self, bus, msg):
		print '\non_eos'
		self.mainloop.quit()


	def on_error(self, bus, msg):
		error = msg.parse_error()
		print '\non_error:', error[1]
		self.mainloop.quit()




if __name__ == '__main__':
	if len(sys.argv) >= 3:
		AudioConcat(sys.argv[1:-1], sys.argv[-1])
	else:
		print 'Usage: %s <input_file(s)> <output_file>' % sys.argv[0]
		print 'Example: %s song1.mp3 song2.ogg output.ogg' % sys.argv[0]
