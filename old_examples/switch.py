#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

import pygtk
pygtk.require('2.0')

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst
import gst.interfaces
import gtk
gtk.gdk.threads_init()

class SwitchTest:
    def __init__(self, videowidget):
        self.playing = False
        pipestr = ('videotestsrc pattern=0 ! queue ! s.sink0'
                   ' videotestsrc pattern=1 ! queue ! s.sink1'
                   ' input-selector name=s ! autovideosink')
        self.pipeline = gst.parse_launch(pipestr)
        self.videowidget = videowidget

        bus = self.pipeline.get_bus()
        bus.enable_sync_message_emission()
        bus.add_signal_watch()
        bus.connect('sync-message::element', self.on_sync_message)
        bus.connect('message', self.on_message)

    def on_sync_message(self, bus, message):
        if message.structure is None:
            return
        if message.structure.get_name() == 'prepare-xwindow-id':
            # Sync with the X server before giving the X-id to the sink
            gtk.gdk.threads_enter()
            gtk.gdk.display_get_default().sync()
            self.videowidget.set_sink(message.src)
            message.src.set_property('force-aspect-ratio', True)
            gtk.gdk.threads_leave()
            
    def on_message(self, bus, message):
        t = message.type
        if t == gst.MESSAGE_ERROR:
            err, debug = message.parse_error()
            print "Error: %s" % err, debug
            if self.on_eos:
                self.on_eos()
            self.playing = False
        elif t == gst.MESSAGE_EOS:
            if self.on_eos:
                self.on_eos()
            self.playing = False

    def play(self):
        self.playing = True
        gst.info("playing player")
        self.pipeline.set_state(gst.STATE_PLAYING)
        
    def stop(self):
        self.pipeline.set_state(gst.STATE_NULL)
        gst.info("stopped player")
        self.playing = False

    def get_state(self, timeout=1):
        return self.pipeline.get_state(timeout=timeout)

    def is_playing(self):
        return self.playing
    
    def switch(self, padname):
        switch = self.pipeline.get_by_name('s')
        stop_time = switch.emit('block')
        newpad = switch.get_static_pad(padname)
        start_time = newpad.get_property('running-time')
        
        gst.warning('stop time = %d' % (stop_time,))
        gst.warning('stop time = %s' % (gst.TIME_ARGS(stop_time),))

        gst.warning('start time = %d' % (start_time,))
        gst.warning('start time = %s' % (gst.TIME_ARGS(start_time),))

        gst.warning('switching from %r to %r'
                    % (switch.get_property('active-pad'), padname))
        switch.emit('switch', newpad, stop_time, start_time)

class VideoWidget(gtk.DrawingArea):
    def __init__(self):
        gtk.DrawingArea.__init__(self)
        self.imagesink = None
        self.unset_flags(gtk.DOUBLE_BUFFERED)

    def do_expose_event(self, event):
        if self.imagesink:
            self.imagesink.expose()
            return False
        else:
            return True

    def set_sink(self, sink):
        assert self.window.xid
        self.imagesink = sink
        self.imagesink.set_xwindow_id(self.window.xid)

class SwitchWindow(gtk.Window):
    UPDATE_INTERVAL = 500
    def __init__(self):
        gtk.Window.__init__(self)
        self.set_default_size(410, 325)

        self.create_ui()
        self.player = SwitchTest(self.videowidget)
        self.populate_combobox()

        self.update_id = -1
        self.changed_id = -1
        self.seek_timeout_id = -1

        self.p_position = gst.CLOCK_TIME_NONE
        self.p_duration = gst.CLOCK_TIME_NONE

        def on_delete_event():
            self.player.stop()
            gtk.main_quit()
        self.connect('delete-event', lambda *x: on_delete_event())

    def load_file(self, location):
        self.player.set_location(location)
                                  
    def play(self):
        self.player.play()
        
    def populate_combobox(self):
        switch = self.player.pipeline.get_by_name('s')
        for i, pad in enumerate([p for p in switch.pads()
                                 if p.get_direction() == gst.PAD_SINK]):
            self.combobox.append_text(pad.get_name())
            if switch.get_property('active-pad') == pad.get_name():
                self.combobox.set_active(i)
        if self.combobox.get_active() == -1:
            self.combobox.set_active(0)

    def combobox_changed(self):
        model = self.combobox.get_model()
        row = model[self.combobox.get_active()]
        padname, = row
        self.player.switch(padname)

    def create_ui(self):
        vbox = gtk.VBox()
        self.add(vbox)

        self.videowidget = VideoWidget()
        vbox.pack_start(self.videowidget)
        
        hbox = gtk.HBox()
        vbox.pack_start(hbox, fill=False, expand=False)
        
        self.combobox = combobox = gtk.combo_box_new_text()
        combobox.show()
        hbox.pack_start(combobox)

        self.combobox.connect('changed',
                              lambda *x: self.combobox_changed())

        self.videowidget.connect_after('realize',
                                       lambda *x: self.play())

def main(args):
    def usage():
        sys.stderr.write("usage: %s\n" % args[0])
        return 1

    # Need to register our derived widget types for implicit event
    # handlers to get called.
    gobject.type_register(SwitchWindow)
    gobject.type_register(VideoWidget)

    if len(args) != 1:
        return usage()

    w = SwitchWindow()
    w.show_all()
    gtk.main()
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
