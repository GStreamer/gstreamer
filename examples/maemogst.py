import gobject
gobject.threads_init()
import gtk
gtk.gdk.threads_init()
import hildon
import gst
import sys

# VideoWidget taken from play.py in gst-python examples
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

class MaemoGstView:

    def __init__(self):
        # hildon has one program instance per app, so get instance
        self.p = hildon.Program.get_instance()
        # set name of application: this shows in titlebar
        gtk.set_application_name("Maemo GStreamer VideoTest")
        # stackable window in case we want more windows in future in app
        self.w = hildon.StackableWindow()
        box = gtk.VBox()
        self.video_widget = VideoWidget()
        # video widget we want to expand to size
        box.pack_start(self.video_widget, True, True, 0)
        # a button finger height to play/pause 
        self.button = hildon.Button(gtk.HILDON_SIZE_FINGER_HEIGHT,
            hildon.BUTTON_ARRANGEMENT_VERTICAL, title="Pause")
        self.button.connect_after("clicked", self.on_button_clicked)
        # don't want button to expand or fill, just stay finger height
        box.pack_start(self.button, False, False, 0)
        self.w.add(box)
        self.w.connect("delete-event", gtk.main_quit)
        self.p.add_window(self.w)
        self.w.show_all()
        self.start_streaming()

    def start_streaming(self):
        # we use ximagesink solely for screenshotting ability
        # less cpu usage would happen with videotestsrc ! xvimagesink
        self.pipeline = \
            gst.parse_launch("videotestsrc ! videoscale ! ximagesink")
        bus = self.pipeline.get_bus()
        # need to connect to sync message handler so we get the sink to be
        # embedded at the right time and not have a temporary new window
        bus.enable_sync_message_emission()
        bus.add_signal_watch()
        bus.connect("sync-message::element", self.on_sync_message)
        bus.connect("message", self.on_message)
        self.pipeline.set_state(gst.STATE_PLAYING)

    def on_sync_message(self, bus, message):
        if message.structure is None:
            return
        if message.structure.get_name() == 'prepare-xwindow-id':
            # all this is needed to sync with the X server before giving the
            # x id to the sink
            gtk.gdk.threads_enter()
            gtk.gdk.display_get_default().sync()
            self.video_widget.set_sink(message.src)
            message.src.set_property("force-aspect-ratio", True)
            gtk.gdk.threads_leave()

    def on_message(self, bus, message):
        if message.type == gst.MESSAGE_ERROR:
            err, debug = message.parse_error()
            hildon.hildon_banner_show_information(self.w, '', 
                "Error: %s" % err)

    def on_button_clicked(self, widget):
        success, state, pending = self.pipeline.get_state(1)
        # do not listen if in middle of state change
        if not pending:
            if state == gst.STATE_PLAYING:
                self.pipeline.set_state(gst.STATE_PAUSED)
                self.button.set_label("Play")
            else:
                self.pipeline.set_state(gst.STATE_PLAYING)
                self.button.set_label("Pause")

def main():
    view = MaemoGstView()
    gtk.main()

if __name__ == '__main__':
    sys.exit(main())
