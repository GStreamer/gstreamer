#!/usr/bin/env python
import sys

import pygtk
pygtk.require('2.0')

import gtk

if gtk.pygtk_version < (2,3,91):
   raise SystemExit, "PyGTK 2.3.91 or higher required"

import gst.play
import gst.interfaces

class Player(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self)
        self.connect('delete-event', self.delete_event_cb)
        self.set_size_request(640, 480)
        vbox = gtk.VBox()
        self.add(vbox)

        accelgroup = gtk.AccelGroup()
        self.add_accel_group(accelgroup)

        self.item_factory = gtk.ItemFactory(gtk.MenuBar, '<main>', accelgroup)
        
        menu_items = [
            ('/_File',      None,         None,         0, '<Branch>' ),
            ('/File/_Open', '<control>O', self.file_open_cb, 0, ''),
            ('/File/sep1',  None,         None,         0, '<Separator>'),
            ('/File/_Quit', '<control>Q', self.file_quit_cb, 0, ''),
            ]
        self.item_factory.create_items(menu_items)

        menubar = self.item_factory.get_widget('<main>')
        vbox.pack_start(menubar, expand=False)

        self.player = PlayerWidget(self)
        vbox.pack_start(self.player, expand=True)

        hbox = gtk.HBox()
        vbox.pack_start(hbox, expand=False)

        button = gtk.Button('Play')
        button.connect('clicked', self.play_clicked_cb)
        hbox.pack_start(button, expand=False)
        button.set_size_request(120, -1)
        button.set_border_width(5)
        self.play_button = button
        
        button = gtk.Button('Stop')
        button.connect('clicked', self.stop_clicked_cb)
        hbox.pack_start(button, expand=False)
        button.set_size_request(120, -1)
        button.set_border_width(5)
        self.stop_button = button

    def file_open_cb(self, button, event):
        fs = gtk.FileSelection('Open a file')
        response = fs.run()
        if response == gtk.RESPONSE_OK:
            self.player.set_filename(fs.get_filename())
        fs.destroy()
        self.player.play()
        self.play_button.set_label('Pause')
            
    def file_quit_cb(self, button, event):
        gst.main_quit()
    
    def play_clicked_cb(self, button):
        if self.player.is_playing():
            self.player.pause()
            button.set_label('Play')
        else:
            self.player.play()
            button.set_label('Pause')
            
    def stop_clicked_cb(self, button):
        self.player.stop()
        
    def delete_event_cb(self, *args):
        self.player.stop()
        gst.main_quit()

class PlayerWidget(gtk.DrawingArea):
    def __init__(self, parent):
        self.parentw = parent
        gtk.DrawingArea.__init__(self)
        self.connect('destroy', self.destroy_cb)
        self.connect('expose-event', self.expose_cb)
        self.set_size_request(400, 400)
        self.player = gst.play.Play()
        self.player.connect('eos', lambda p: gst.main_quit())

        self.imagesink = gst.element_factory_make('xvimagesink')
        
        # Setup source and sinks
        self.player.set_data_src(gst.element_factory_make('filesrc'))
        audio_sink = gst.element_factory_make('alsasink')
        audio_sink.set_property('device', 'hw:0')
        self.player.set_audio_sink(audio_sink)
        self.player.set_video_sink(self.imagesink)

    def destroy_cb(self, da):
        self.imagesink.set_xwindow_id(0L)

    def expose_cb(self, window, event):
        self.imagesink.set_xwindow_id(self.window.xid)
        
    def stop(self):
        self.player.set_state(gst.STATE_NULL)

    def play(self):
        self.imagesink.set_xwindow_id(self.window.xid)
        self.player.set_state(gst.STATE_PLAYING)

    def pause(self):
        self.player.set_state(gst.STATE_PAUSED)

    def is_playing(self):
        return self.player.get_state() == gst.STATE_PLAYING
    
    def set_filename(self, filename):
        self.player.set_location(filename)

player = Player()
player.show_all()
gst.main()
