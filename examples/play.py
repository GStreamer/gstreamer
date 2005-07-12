import pygtk
pygtk.require('2.0')

import sys

import gobject
import gst
import gst.interfaces
import gtk

class GstPlayer:
    def __init__(self):
        self.player = gst.element_factory_make("playbin", "player")

    def set_video_sink(self, sink):
        self.player.set_property('video-sink', sink)
        print self.player.get_property('video-sink')
        
    def set_location(self, location):
        self.player.set_property('uri', location)

    def get_length(self):
        return self.player.query(gst.QUERY_TOTAL, gst.FORMAT_TIME)

    def get_position(self):
        return self.player.query(gst.QUERY_POSITION, gst.FORMAT_TIME)

    def seek(self, location):
        print "seek to %ld on element %s" % (location, self.player.get_name())
        event = gst.event_new_seek(gst.FORMAT_TIME |
                                   gst.SEEK_METHOD_SET |
                                   gst.SEEK_FLAG_FLUSH, location)

        self.player.send_event(event)
        self.player.set_state(gst.STATE_PLAYING)

    def pause(self):
        self.player.set_state(gst.STATE_PAUSED)

    def play(self):
        self.player.set_state(gst.STATE_PLAYING)
        
    def stop(self):
        self.player.set_state(gst.STATE_READY)

    is_playing = lambda self: self.player.get_state() == gst.STATE_PLAYING
    is_paused = lambda self: self.player.get_state() == gst.STATE_PAUSED
    is_stopped = lambda self: self.player.get_state() == gst.STATE_READY
    
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
        gtk.idle_add(self.idler)

    def idler(self):
        self.set_window_id(self.window.xid)
        
    def set_window_id(self, xid):
        self.imagesink.set_xwindow_id(xid)
        

class PlayerWindow(gtk.Window):
    UPDATE_INTERVAL = 500
    def __init__(self):
        gtk.Window.__init__(self)
        self.connect('delete-event', gtk.main_quit)
        self.set_default_size(96, 96)

        self.player = GstPlayer()
        
        self.create_ui()

        self.update_id = -1
        
    def load_file(self, location):
        self.player.set_location(location)
                                  
    def create_ui(self):
        vbox = gtk.VBox()

        videowidget = VideoWidget(self.player)
        vbox.pack_start(videowidget)
        
        hbox = gtk.HBox()
        vbox.pack_start(hbox)
        
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

        self.add(vbox)

    def scale_format_value_cb(self, scale, value):
        duration = self.player.get_length()
        if duration == -1:
            real = 0
        else:
            real = value * duration / 100
        
        seconds = real / gst.SECOND

        return "%02d:%02d" % (seconds / 60, seconds % 60)

    def scale_button_press_cb(self, widget, event):
        self.player.pause()
        if self.update_id != -1:
            gtk.timeout_remove(self.update_id)
            self.update_id = -1
            
    def scale_button_release_cb(self, widget, event):
        duration = self.player.get_length()
        real = long(widget.get_value() * duration / 100)
        self.player.seek(real)
        
        self.update_id = gtk.timeout_add(self.UPDATE_INTERVAL,
                                         self.update_scale_cb)

    def update_scale_cb(self):
        length = self.player.get_length()
        if length:
            value = self.player.get_position() * 100.0 / length
            self.adjustment.set_value(value)

        return True

    def play_clicked_cb(self, button):
        if self.player.is_playing():
            return
        
        self.player.play()
        self.update_id = gtk.timeout_add(self.UPDATE_INTERVAL,
                                         self.update_scale_cb)

    def pause_clicked_cb(self, button):
        if self.player.is_paused():
            return
        
        self.player.pause()
        if self.update_id != -1:
            gtk.timeout_remove(self.update_id)
            self.update_id = -1

    def stop_clicked_cb(self, button):
        if self.player.is_stopped():
            return
        
        self.player.stop()
        if self.update_id != -1:
            gtk.timeout_remove(self.update_id)
            self.update_id = -1
        self.adjustment.set_value(0.0)

def main(args):
    w = PlayerWindow()
    w.load_file(args[1])
    w.show_all()

    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
