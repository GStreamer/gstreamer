#! /usr/bin/env python
# -*- coding: utf-8 -*-

# Segments.py
# Copyright (C) 2006 Artem Popov <artfwo@gmail.com>
#
# This example demonstrates segment seeking
# and seamless looping within playbin.

import pygst
pygst.require ("0.10")
import gst

import pygtk
pygtk.require ("2.0")
import gobject

class Looper (gobject.GObject):
	__gproperties__ = {
		"loop": (gobject.TYPE_BOOLEAN,
			"loop",
			"Whether to loop the segment",
			False,
			gobject.PARAM_READWRITE),
		"start-pos": (gobject.TYPE_UINT64,
			"start position",
			"The segment start marker",
			0,
			0xfffffffffffffff, # max long possible
			0,
			gobject.PARAM_READWRITE),
		"stop-pos": (gobject.TYPE_UINT64,
			"stop position",
			"The segment stop marker",
			0,
			0xfffffffffffffff, # max long possible
			0,
			gobject.PARAM_READWRITE),
	} # __gproperties__
	
	__gsignals__ = {
		"stopped": (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
		"position-updated": (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, (gobject.TYPE_FLOAT,)),
		"error": (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, (gobject.TYPE_PYOBJECT,))
	} # __gsignals__
	
	def __init__ (self, location = None):
		gobject.GObject.__init__ (self)
		
		self.__playbin = gst.element_factory_make ("playbin")
		self.__playbin.props.video_sink = gst.element_factory_make ("fakesink")
		
		bus = self.__playbin.get_bus ()
		bus.add_watch (self.__on_bus_message)
		
		self.__loop = False
		self.__start_pos = 0
		self.__stop_pos = 0
		
		self.__timeout_id = 0
		
		if location:
			self.load (location)
	
	def load (self, location):
		self.__playbin.props.uri = location
		self.__start_position = 0
		self.__stop_position = 0
	
	def set_segment (self, start, stop):
		self.props.start_pos = start
		self.props.stop_pos = stop
	
	def play (self):
		if not (self.__start_pos or self.__stop_pos):
			raise RuntimeError, "Cannot start playback, segment was not set!"
		
		self.__playbin.set_state (gst.STATE_PLAYING)
	
	def stop (self, silent = False):
		self.__playbin.set_state (gst.STATE_NULL)
		if not silent:
			self.emit ("stopped")
	
	def do_get_property (self, property):
		if property.name == "loop":
			return self.__loop
		elif property.name == "start-pos":
			return self.__start_pos
		elif property.name == "stop-pos":
			return self.__stop_pos
		else:
			raise AttributeError, "Unknown property %s" % property.name
	
	def do_set_property (self, property, value):
		if property.name == "loop":
			self.__loop = value
		elif property.name == "start-pos":
			self.__start_pos = value
		elif property.name == "stop-pos":
			self.__stop_pos = value
		else:
			raise AttributeError, "Unknown property %s" % property.name
	
	def do_stopped (self):
		if self.__timeout_id:
			gobject.source_remove (self.__timeout_id)
			self.__timeout_id = 0
	
	def __seek (self, start, stop, flush):
		flags = gst.SEEK_FLAG_SEGMENT | gst.SEEK_FLAG_ACCURATE
		if flush:
			flags = flags | gst.SEEK_FLAG_FLUSH
		self.__playbin.seek (1.0, gst.FORMAT_TIME, flags,
			gst.SEEK_TYPE_SET, start,
			gst.SEEK_TYPE_SET, stop)
	
	def __on_timeout (self):
		position = self.__playbin.query_position (gst.FORMAT_TIME) [0]
		self.emit ("position-updated", float (position))
		return True
	
	def __on_bus_message (self, bus, message):
		if message.type == gst.MESSAGE_ERROR:
			error, debug = message.parse_error ()
			self.stop () # this looks neccessary here
			self.emit ("error", (error, debug))
		
		elif message.type == gst.MESSAGE_NEW_CLOCK:
			# we connect the timeout handler here to be sure that further queries succeed
			interval = int ((self.__stop_position - self.__start_position) / (2 * gst.SECOND) + 50)
			self.__timeout_id = gobject.timeout_add (interval, self.__on_timeout)
		
		elif message.type == gst.MESSAGE_STATE_CHANGED:
			old_state, new_state, pending = message.parse_state_changed ()
			if old_state == gst.STATE_READY and new_state == gst.STATE_PAUSED and message.src == self.__playbin:
				self.__seek (self.__start_pos, self.__stop_pos, True)
		
		elif message.type == gst.MESSAGE_SEGMENT_DONE:
			if self.__loop:
				self.__seek (self.__start_pos, self.__stop_pos, False)
			else:
				src = self.__playbin.get_property ("source")
				pad = src.get_pad ('src')
				pad.push_event (gst.event_new_eos ())
				
				# this is the good old way:
				#
				# pads = src.src_pads ()
				# while True:
				# 	try:
				# 		pad = pads.next ()
				# 		pad.push_event (gst.event_new_eos ())
				# 	except:
				# 		break
		
		elif message.type == gst.MESSAGE_EOS:
			self.stop ()
		
		return True

mainloop = gobject.MainLoop ()

def on_looper_stopped (looper):
	mainloop.quit ()

def on_looper_pos_updated (looper, position):
	print round (position / gst.SECOND, 2)

def on_looper_error (looper, error_tuple):
	error, debug = error_tuple
	print "\n\n%s\n\n%s\n\n" % (error, debug)
	mainloop.quit ()

if __name__ == "__main__":
	import sys
	if len (sys.argv) != 5:
		print "Usage: %s <filename|uri> <start_seconds> <stop_seconds> <loop = 0|1>" % sys.argv [0]
		sys.exit (1)
	
	if "://" in sys.argv [1]:
		uri = sys.argv [1]
	else:
		import os.path
		uri = "file://" + os.path.abspath (sys.argv [1])
	
	looper = Looper (uri)
	
	looper.props.start_pos = long (sys.argv [2]) * gst.SECOND
	looper.props.stop_pos = long (sys.argv [3]) * gst.SECOND
	looper.props.loop = int (sys.argv [4])
	
	looper.connect ("stopped", on_looper_stopped)
	looper.connect ("position-updated", on_looper_pos_updated)
	looper.connect ("error", on_looper_error)
	
	looper.play ()
	mainloop.run ()
