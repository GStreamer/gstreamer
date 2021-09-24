#!/usr/bin/env python

# decodebin.py - Audio autopluging example using 'decodebin' element
# Copyright (C) 2006 Jason Gerard DeRose <jderose@jasonderose.org>

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

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst


class Decodebin:
	def __init__(self, location):
		# The pipeline
		self.pipeline = gst.Pipeline()
		
		# Create bus and connect several handlers
		self.bus = self.pipeline.get_bus()
		self.bus.add_signal_watch()
		self.bus.connect('message::eos', self.on_eos)
		self.bus.connect('message::tag', self.on_tag)
		self.bus.connect('message::error', self.on_error)

		# Create elements
		self.src = gst.element_factory_make('filesrc')
		self.dec = gst.element_factory_make('decodebin')
		self.conv = gst.element_factory_make('audioconvert')
		self.rsmpl = gst.element_factory_make('audioresample')
		self.sink = gst.element_factory_make('alsasink')
		
		# Set 'location' property on filesrc
		self.src.set_property('location', location)
		
		# Connect handler for 'new-decoded-pad' signal 
		self.dec.connect('new-decoded-pad', self.on_new_decoded_pad)
		
		# Add elements to pipeline
		self.pipeline.add(self.src, self.dec, self.conv, self.rsmpl, self.sink)
		
		# Link *some* elements 
		# This is completed in self.on_new_decoded_pad()
		self.src.link(self.dec)
		gst.element_link_many(self.conv, self.rsmpl, self.sink)
		
		# Reference used in self.on_new_decoded_pad()
		self.apad = self.conv.get_pad('sink')

		# The MainLoop
		self.mainloop = gobject.MainLoop()

		# And off we go!
		self.pipeline.set_state(gst.STATE_PLAYING)
		self.mainloop.run()
		
		
	def on_new_decoded_pad(self, element, pad, last):
		caps = pad.get_caps()
		name = caps[0].get_name()
		print 'on_new_decoded_pad:', name
		if name == 'audio/x-raw-float' or name == 'audio/x-raw-int':
			if not self.apad.is_linked(): # Only link once
				pad.link(self.apad)
			
			
	def on_eos(self, bus, msg):
		print 'on_eos'
		self.mainloop.quit()
		
		
	def on_tag(self, bus, msg):
		taglist = msg.parse_tag()
		print 'on_tag:'
		for key in taglist.keys():
			print '\t%s = %s' % (key, taglist[key])
			
			
	def on_error(self, bus, msg):
		error = msg.parse_error()
		print 'on_error:', error[1]
		self.mainloop.quit()





if __name__ == '__main__':
	if len(sys.argv) == 2:
		Decodebin(sys.argv[1])
	else:
		print 'Usage: %s /path/to/media/file' % sys.argv[0]
