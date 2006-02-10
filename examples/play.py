#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import pygtk
pygtk.require('2.0')

import sys

import gobject

import pygst
pygst.require('0.10')
import gst
import gst.interfaces
import gtk

class GstPlayer:
    def __init__(self):
        self.player = gst.element_factory_make("playbin", "player")

    def set_video_sink(self, sink):
        self.player.set_property('video-sink', sink)
        gst.debug('using videosink %r' % self.player.get_property('video-sink'))
        
    def set_location(self, location):
        self.player.set_property('uri', location)

    def query_position(self):
        "Returns a (position, duration) tuple"
        try:
            position, format = self.player.query_position(gst.FORMAT_TIME)
        except:
            position = gst.CLOCK_TIME_NONE

        try:
            duration, format = self.player.query_duration(gst.FORMAT_TIME)
        except:
            duration = gst.CLOCK_TIME_NONE

        return (position, duration)

    def seek(self, location):
        """
        @param location: time to seek to, in nanoseconds
        """
        gst.debug("seeking to %r" % location)
        event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
            gst.SEEK_FLAG_FLUSH,
            gst.SEEK_TYPE_SET, location,
            gst.SEEK_TYPE_NONE, 0)

        res = self.player.send_event(event)
        if res:
            gst.info("setting new stream time to 0")
            self.player.set_new_stream_time(0L)
        else:
            gst.error("seek to %r failed" % location)

    def pause(self):
        gst.info("pausing player")
        self.player.set_state(gst.STATE_PAUSED)

    def play(self):
        gst.info("playing player")
        self.player.set_state(gst.STATE_PLAYING)
        
    def stop(self):
        gst.info("stopping player")
        self.player.set_state(gst.STATE_READY)
        gst.info("stopped player")

    def get_state(self, timeout=1):
        return self.player.get_state(timeout=timeout)

    def is_in_state(self, state):
        gst.debug("checking if player is in state %r" % state)
        cur, pen, final = self.get_state(timeout=0)
        gst.debug("checked if player is in state %r" % state)
        if pen == gst.STATE_VOID_PENDING and cure == state:
            return True
        return False
    is_playing = lambda self: self.is_in_state(gst.STATE_PLAYING)
    is_paused = lambda self: self.is_in_state(gst.STATE_PAUSED)
    is_stopped = lambda self: self.is_in_state(gst.STATE_READY)
    
class VideoWidget(gtk.DrawingArea):
    def __init__(self, player):
        gtk.DrawingArea.__init__(self)
        self.connect('destroy', self.destroy_cb)
        self.connect_after('realize', self.after_realize_cb)
        self.set_size_request(400, 400)
        
        self.player = player
        self.imagesink = gst.element_factory_make('xvimagesink')
        self.player.set_video_sink(self.imagesink)

    def destroy_cb(self, da):
        self.set_window_id(0L)

    # Sort of a hack, but it works for now.
    def after_realize_cb(self, window):
        gobject.idle_add(self.frame_video_sink)

    def frame_video_sink(self):
        self.set_window_id(self.window.xid)
        
    def set_window_id(self, xid):
        self.imagesink.set_xwindow_id(xid)

    def unframe_video_sink(self):
        self.set_window_id(0L)
        

class PlayerWindow(gtk.Window):
    UPDATE_INTERVAL = 500
    def __init__(self):
        gtk.Window.__init__(self)
        self.connect('delete-event', gtk.main_quit)
        self.set_default_size(96, 96)

        self.player = GstPlayer()
        
        self.create_ui()

        self.update_id = -1
        self.changed_id = -1
        self.seek_timeout_id = -1

        self.p_position = gst.CLOCK_TIME_NONE
        self.p_duration = gst.CLOCK_TIME_NONE
        
    def load_file(self, location):
        self.player.set_location(location)
                                  
    def create_ui(self):
        vbox = gtk.VBox()

        self.videowidget = VideoWidget(self.player)
        vbox.pack_start(self.videowidget)
        
        hbox = gtk.HBox()
        vbox.pack_start(hbox, fill=False, expand=False)
        
        button = gtk.Button('play')
        button.connect('clicked', self.play_clicked_cb)
        hbox.pack_start(button, False)
        
        button = gtk.Button("pause");
        button.connect('clicked', self.pause_clicked_cb)
        hbox.pack_start(button, False)
        
        button = gtk.Button("stop");
        button.connect('clicked', self.stop_clicked_cb)
        hbox.pack_start(button, False)

        self.adjustment = gtk.Adjustment(0.0, 0.00, 100.0, 0.1, 1.0, 1.0)
        hscale = gtk.HScale(self.adjustment)
        hscale.set_digits(2)
        hscale.set_update_policy(gtk.UPDATE_CONTINUOUS)
        hscale.connect('button-press-event', self.scale_button_press_cb)
        hscale.connect('button-release-event', self.scale_button_release_cb)
        hscale.connect('format-value', self.scale_format_value_cb)
        hbox.pack_start(hscale)
        self.hscale = hscale

        self.add(vbox)

    def scale_format_value_cb(self, scale, value):
        if self.p_duration == -1:
            real = 0
        else:
            real = value * self.p_duration / 100
        
        seconds = real / gst.SECOND

        return "%02d:%02d" % (seconds / 60, seconds % 60)

    def scale_button_press_cb(self, widget, event):
        # see seek.c:start_seek
        gst.debug('starting seek')
        self.player.pause()

        # don't timeout-update position during seek
        if self.update_id != -1:
            gobject.source_remove(self.update_id)
            self.update_id = -1

        # make sure we get changed notifies
        if self.changed_id == -1:
            self.changed_id = self.hscale.connect('value-changed',
                self.scale_value_changed_cb)
            
    def scale_value_changed_cb(self, scale):
        # see seek.c:seek_cb
        real = long(scale.get_value() * self.p_duration / 100) # in ns
        gst.debug('value changed, perform seek to %r' % real)
        self.player.seek(real)
        # allow for a preroll
        self.player.get_state(timeout=50) # 50 ms

    def scale_button_release_cb(self, widget, event):
        # see seek.cstop_seek
        widget.disconnect(self.changed_id)
        self.changed_id = -1

        if self.seek_timeout_id != -1:
            gobject.source_remove(self.seek_timeout_id)
            self.seek_timeout_id = -1
        else:
            gst.debug('released slider, setting back to playing')
            self.player.play()

        if self.update_id != -1:
            self.error('Had a previous update timeout id')
        else:
            self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                self.update_scale_cb)

    def update_scale_cb(self):
        self.p_position, self.p_duration = self.player.query_position()
        if self.p_position != gst.CLOCK_TIME_NONE:
            value = self.p_position * 100.0 / self.p_duration
            self.adjustment.set_value(value)

        return True

    def play_clicked_cb(self, button):
        if self.player.is_playing():
            return
        
        self.videowidget.frame_video_sink()
        self.player.play()
        # keep the time display updated
        self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                                         self.update_scale_cb)

    def pause_clicked_cb(self, button):
        if self.player.is_paused():
            return
        
        self.player.pause()
        if self.update_id != -1:
            gobject.source_remove(self.update_id)
            self.update_id = -1

    def stop_clicked_cb(self, button):
        if self.player.is_stopped():
            return
        
        self.player.stop()
        self.videowidget.unframe_video_sink()
        if self.update_id != -1:
            gobject.source_remove(self.update_id)
            self.update_id = -1
        self.adjustment.set_value(0.0)

def main(args):
    def usage():
        sys.stderr.write("usage: %s URI-OF-MEDIA-FILE\n" % args[0])
        sys.exit(1)

    w = PlayerWindow()

    if len(args) != 2:
        usage()

    if not gst.uri_is_valid(args[1]):
        sys.stderr.write("Error: Invalid URI: %s\n" % args[1])
        sys.exit(1)

    w.load_file(args[1])
    w.show_all()

    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
