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

class GstPlayer:
    def __init__(self, videowidget):
        self.playing = False
        self.player = gst.element_factory_make("playbin", "player")
        self.videowidget = videowidget

        bus = self.player.get_bus()
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

    def set_location(self, location):
        self.player.set_state(gst.STATE_NULL)
        self.player.set_property('uri', location)

    def get_location(self):
        return self.player.get_property('uri')

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
        self.playing = False

    def play(self):
        gst.info("playing player")
        self.player.set_state(gst.STATE_PLAYING)
        self.playing = True
        
    def stop(self):
        self.player.set_state(gst.STATE_NULL)
        gst.info("stopped player")

    def get_state(self, timeout=1):
        return self.player.get_state(timeout=timeout)

    def is_playing(self):
        return self.playing
    
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

class TimeControl(gtk.HBox):
    # all labels same size
    sizegroup = gtk.SizeGroup(gtk.SIZE_GROUP_HORIZONTAL)
    __gproperties__ = {'time': (gobject.TYPE_UINT64, 'Time', 'Time',
                                # not actually usable: see #335854
                                # kept for .notify() usage
                                0L, (1<<63)-1, 0L,
                                gobject.PARAM_READABLE)}

    def __init__(self, window, label):
        gtk.HBox.__init__(self)
        self.pwindow = window
        self.label = label
        self.create_ui()

    def get_property(self, param, pspec):
        if param == 'time':
            return self.get_time()
        else:
            assert param in self.__gproperties__, \
                   'Unknown property: %s' % param

    def create_ui(self):
        label = gtk.Label(self.label + ": ")
        label.show()
        a = gtk.Alignment(1.0, 0.5)
        a.add(label)
        a.set_padding(0, 0, 12, 0)
        a.show()
        self.sizegroup.add_widget(a)
        self.pack_start(a, True, False, 0)

        self.minutes = minutes = gtk.Entry(5)
        minutes.set_width_chars(5)
        minutes.set_alignment(1.0)
        minutes.connect('changed', lambda *x: self.notify('time'))
        minutes.connect_after('activate', lambda *x: self.activated())
        label2 = gtk.Label(":")
        self.seconds = seconds = gtk.Entry(2)
        seconds.set_width_chars(2)
        seconds.set_alignment(1.0)
        seconds.connect('changed', lambda *x: self.notify('time'))
        seconds.connect_after('activate', lambda *x: self.activated())
        label3 = gtk.Label(".")
        self.milliseconds = milliseconds = gtk.Entry(3)
        milliseconds.set_width_chars(3)
        milliseconds.set_alignment(0.0)
        milliseconds.connect('changed', lambda *x: self.notify('time'))
        milliseconds.connect_after('activate', lambda *x: self.activated())
        set = gtk.Button('Set')
        goto = gtk.Button('Go')
        goto.set_property('image',
                          gtk.image_new_from_stock(gtk.STOCK_JUMP_TO,
                                                   gtk.ICON_SIZE_BUTTON))
        for w in minutes, label2, seconds, label3, milliseconds:
            w.show()
            self.pack_start(w, False)
        set.show()
        self.pack_start(set, False, False, 6)
        goto.show()
        self.pack_start(goto, False, False, 0)
        set.connect('clicked', lambda *x: self.set_now())
        goto.connect('clicked', lambda *x: self.activated())
        pad = gtk.Label("")
        pad.show()
        self.pack_start(pad, True, False, 0)

    def get_time(self):
        time = 0
        for w, multiplier in ((self.minutes, gst.SECOND*60),
                              (self.seconds, gst.SECOND),
                              (self.milliseconds, gst.MSECOND)):
            text = w.get_text()
            try:
                val = int(text)
            except ValueError:
                val = 0
            w.set_text(val and str(val) or '0')
            time += val * multiplier
        return time

    def set_time(self, time):
        if time == gst.CLOCK_TIME_NONE:
            print "Can't set '%s' (invalid time)" % self.label
            return
        self.freeze_notify()
        for w, multiplier in ((self.minutes, gst.SECOND*60),
                              (self.seconds, gst.SECOND),
                              (self.milliseconds, gst.MSECOND)):
            val = time // multiplier
            w.set_text(str(val))
            time -= val * multiplier
        self.thaw_notify()

    def set_now(self):
        time, dur = self.pwindow.player.query_position()
        self.set_time(time)

    def activated(self):
        time = self.get_time()
        if self.pwindow.player.is_playing():
            self.pwindow.play_toggled()
        self.pwindow.player.seek(time)
        self.pwindow.player.get_state(timeout=gst.MSECOND * 200)

class ProgressDialog(gtk.Dialog):
    def __init__(self, title, description, task, parent, flags, buttons):
        gtk.Dialog.__init__(self, title, parent, flags, buttons)
        self._create_ui(title, description, task)

    def _create_ui(self, title, description, task):
        self.set_border_width(6)
        self.set_resizable(False)
        self.set_has_separator(False)

        vbox = gtk.VBox()
        vbox.set_border_width(6)
        vbox.show()
        self.vbox.pack_start(vbox, False)

        label = gtk.Label('<big><b>%s</b></big>' % title)
        label.set_use_markup(True)
        label.set_alignment(0.0, 0.0)
        label.show()
        vbox.pack_start(label, False)
        
        label = gtk.Label(description)
        label.set_use_markup(True)
        label.set_alignment(0.0, 0.0)
        label.set_line_wrap(True)
        label.set_padding(0, 12)
        label.show()
        vbox.pack_start(label, False)

        self.progress = progress = gtk.ProgressBar()
        progress.show()
        vbox.pack_start(progress, False)

        self.progresstext = label = gtk.Label('')
        label.set_line_wrap(True)
        label.set_use_markup(True)
        label.set_alignment(0.0, 0.0)
        label.show()
        vbox.pack_start(label)
        self.set_task(task)

    def set_task(self, task):
        self.progresstext.set_markup('<i>%s</i>' % task)

UNKNOWN = 0
SUCCESS = 1
FAILURE = 2
CANCELLED = 3

class RemuxProgressDialog(ProgressDialog):
    def __init__(self, parent, start, stop, fromname, toname):
        ProgressDialog.__init__(self,
                                "Writing to disk",
                                ('Writing the selected segment of <b>%s</b> '
                                 'to <b>%s</b>. This may take some time.'
                                 % (fromname, toname)),
                                'Starting media pipeline',
                                parent,
                                gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                                (gtk.STOCK_CANCEL, CANCELLED,
                                 gtk.STOCK_CLOSE, SUCCESS))
        self.start = start
        self.stop = stop
        self.update_position(start)
        self.set_completed(False)
        
    def update_position(self, pos):
        pos = min(max(pos, self.start), self.stop)
        remaining = self.stop - pos
        minutes = remaining // (gst.SECOND * 60)
        seconds = (remaining - minutes * gst.SECOND * 60) // gst.SECOND
        self.progress.set_text('%d:%02d of video remaining' % (minutes, seconds))
        self.progress.set_fraction(1.0 - float(remaining) / (self.stop - self.start))

    def set_completed(self, completed):
        self.set_response_sensitive(CANCELLED, not completed)
        self.set_response_sensitive(SUCCESS, completed)

def set_connection_blocked_async_marshalled(pads, proc, *args, **kwargs):
    def clear_list(l):
        while l:
            l.pop()

    to_block = list(pads)
    to_relink = [(x, x.get_peer()) for x in pads]

    def on_pad_blocked_sync(pad, is_blocked):
        if pad not in to_block:
            # can happen after the seek and before unblocking -- racy,
            # but no prob, bob.
            return
        to_block.remove(pad)
        if not to_block:
            # marshal to main thread
            gobject.idle_add(on_pads_blocked)

    def on_pads_blocked():
        for src, sink in to_relink:
            src.link(sink)
        proc(*args, **kwargs)
        for src, sink in to_relink:
            src.set_blocked_async(False, lambda *x: None)
        clear_list(to_relink)

    for src, sink in to_relink:
        src.unlink(sink)
        src.set_blocked_async(True, on_pad_blocked_sync)

class Remuxer(gst.Pipeline):

    __gsignals__ = {'done': (gobject.SIGNAL_RUN_LAST, None, (int,))}

    def __init__(self, fromuri, touri, start, stop):
        # HACK: should do Pipeline.__init__, but that doesn't do what we
        # want; there's a bug open aboooot that
        self.__gobject_init__()

        assert start >= 0
        assert stop > start

        self.fromuri = fromuri
        self.touri = None
        self.start_time = start
        self.stop_time = stop

        self.src = self.remuxbin = self.sink = None
        self.resolution = UNKNOWN

        self.window = None
        self.pdialog = None

        self._query_id = -1

    def do_setup_pipeline(self):
        self.src = gst.element_make_from_uri(gst.URI_SRC, self.fromuri)
        self.remuxbin = RemuxBin(self.start_time, self.stop_time)
        self.sink = gst.element_make_from_uri(gst.URI_SINK, self.touri)
        self.resolution = UNKNOWN

        if gobject.signal_lookup('allow-overwrite', self.sink.__class__):
            self.sink.connect('allow-overwrite', lambda *x: True)

        self.add(self.src, self.remuxbin, self.sink)

        self.src.link(self.remuxbin)
        self.remuxbin.link(self.sink)

    def do_get_touri(self):
        chooser = gtk.FileChooserDialog('Save as...',
                                        self.window,
                                        action=gtk.FILE_CHOOSER_ACTION_SAVE,
                                        buttons=(gtk.STOCK_CANCEL,
                                                 CANCELLED,
                                                 gtk.STOCK_SAVE,
                                                 SUCCESS))
        chooser.set_uri(self.fromuri) # to select the folder
        chooser.unselect_all()
        chooser.set_do_overwrite_confirmation(True)
        name = self.fromuri.split('/')[-1][:-4] + '-remuxed.ogg'
        chooser.set_current_name(name)
        resp = chooser.run()
        uri = chooser.get_uri()
        chooser.destroy()

        if resp == SUCCESS:
            return uri
        else:
            return None

    def _start_queries(self):
        def do_query():
            try:
                # HACK: self.remuxbin.query() should do the same
                # (requires implementing a vmethod, dunno how to do that
                # although i think it's possible)
                # HACK: why does self.query_position(..) not give useful
                # answers? 
                pad = self.remuxbin.get_pad('src')
                pos, duration = pad.query_position(gst.FORMAT_TIME)
                if pos != gst.CLOCK_TIME_NONE:
                    self.pdialog.update_position(pos)
            except:
                # print 'query failed'
                pass
            return True
        if self._query_id == -1:
            self._query_id = gobject.timeout_add(100, # 10 Hz
                                                 do_query)

    def _stop_queries(self):
        if self._query_id != -1:
            gobject.source_remove(self._query_id)
            self._query_id = -1

    def _bus_watch(self, bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print 'error', message
            self._stop_queries()
            m = gtk.MessageDialog(self.window,
                                  gtk.DIALOG_MODAL|gtk.DIALOG_DESTROY_WITH_PARENT,
                                  gtk.MESSAGE_ERROR,
                                  gtk.BUTTONS_CLOSE,
                                  "Error processing file")
            gerror, debug = message.parse_error()
            txt = ('There was an error processing your file: %s\n\n'
                   'Debug information:\n%s' % (gerror, debug))
            m.format_secondary_text(txt)
            m.run()
            m.destroy()
            self.response(FAILURE)
        elif message.type == gst.MESSAGE_WARNING:
            print 'warning', message
        elif message.type == gst.MESSAGE_EOS:
            # print 'eos, woot', message.src
            name = self.touri
            if name.startswith('file://'):
                name = name[7:]
            self.pdialog.set_task('Finished writing %s' % name)
            self.pdialog.update_position(self.stop_time)
            self._stop_queries()
            self.pdialog.set_completed(True)
        elif message.type == gst.MESSAGE_STATE_CHANGED:
            if message.src == self:
                old, new, pending = message.parse_state_changed()
                if ((old, new, pending) ==
                    (gst.STATE_READY, gst.STATE_PAUSED,
                     gst.STATE_VOID_PENDING)):
                    self.pdialog.set_task('Processing file')
                    self.pdialog.update_position(self.start_time)
                    self._start_queries()
                    self.set_state(gst.STATE_PLAYING)

    def response(self, response):
        assert self.resolution == UNKNOWN
        self.resolution = response
        self.set_state(gst.STATE_NULL)
        self.pdialog.destroy()
        self.pdialog = None
        self.window.set_sensitive(True)
        self.emit('done', response)

    def start(self, main_window):
        self.window = main_window
        self.touri = self.do_get_touri()
        if not self.touri:
            return False
        self.do_setup_pipeline()
        bus = self.get_bus()
        bus.add_signal_watch()
        bus.connect('message', self._bus_watch)
        if self.window:
            # can be None if we are debugging...
            self.window.set_sensitive(False)
        fromname = self.fromuri.split('/')[-1]
        toname = self.touri.split('/')[-1]
        self.pdialog = RemuxProgressDialog(main_window, self.start_time,
                                           self.stop_time, fromname, toname)
        self.pdialog.show()
        self.pdialog.connect('response', lambda w, r: self.response(r))

        self.set_state(gst.STATE_PAUSED)
        return True
        
    def run(self, main_window):
        if self.start(main_window):
            loop = gobject.MainLoop()
            self.connect('done', lambda *x: gobject.idle_add(loop.quit))
            loop.run()
        else:
            self.resolution = CANCELLED
        return self.resolution
        
class RemuxBin(gst.Bin):
    def __init__(self, start_time, stop_time):
        self.__gobject_init__()

        self.parsefactories = self._find_parsers()
        self.parsers = []

        self.demux = gst.element_factory_make('oggdemux')
        self.mux = gst.element_factory_make('oggmux')

        self.add(self.demux, self.mux)

        self.add_pad(gst.GhostPad('sink', self.demux.get_pad('sink')))
        self.add_pad(gst.GhostPad('src', self.mux.get_pad('src')))

        self.demux.connect('pad-added', self._new_demuxed_pad)
        self.demux.connect('no-more-pads', self._no_more_pads)

        self.start_time = start_time
        self.stop_time = stop_time

    def _find_parsers(self):
        registry = gst.registry_get_default()
        ret = {}
        for f in registry.get_feature_list(gst.ElementFactory):
            if f.get_klass().find('Parser') >= 0:
                for t in f.get_static_pad_templates():
                    if t.direction == gst.PAD_SINK:
                        for s in t.get_caps():
                            ret[s.get_name()] = f.get_name()
                        break
        return ret

    def _new_demuxed_pad(self, element, pad):
        format = pad.get_caps()[0].get_name()

        if format not in self.parsefactories:
            self.async_error("Unsupported media type: %s", format)
            return

        queue = gst.element_factory_make('queue', None);
        queue.set_property('max-size-buffers', 1000)
        parser = gst.element_factory_make(self.parsefactories[format])
        self.add(queue)
        self.add(parser)
        queue.set_state(gst.STATE_PAUSED)
        parser.set_state(gst.STATE_PAUSED)
        pad.link(queue.get_compatible_pad(pad))
        queue.link(parser)
        parser.link(self.mux)
        self.parsers.append(parser)

    def _do_seek(self):
        flags = gst.SEEK_FLAG_FLUSH
        # HACK: self.seek should work, should try that at some point
        return self.demux.seek(1.0, gst.FORMAT_TIME, flags,
                               gst.SEEK_TYPE_SET, self.start_time,
                               gst.SEEK_TYPE_SET, self.stop_time)

    def _no_more_pads(self, element):
        pads = [x.get_pad('src') for x in self.parsers]
        set_connection_blocked_async_marshalled(pads,
                                                self._do_seek)


class PlayerWindow(gtk.Window):
    UPDATE_INTERVAL = 500
    def __init__(self):
        gtk.Window.__init__(self)
        self.set_default_size(600, 425)

        self.create_ui()

        self.player = GstPlayer(self.videowidget)

        def on_eos():
            self.player.seek(0L)
            self.play_toggled()
        self.player.on_eos = lambda *x: on_eos()
        
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
        filename = location.split('/')[-1]
        self.set_title('%s munger' % filename)
        self.player.set_location(location)
        if self.videowidget.flags() & gtk.REALIZED:
            self.play_toggled()
        else:
            self.videowidget.connect_after('realize',
                                           lambda *x: self.play_toggled())
                                  
    def create_ui(self):
        vbox = gtk.VBox()
        vbox.show()
        self.add(vbox)

        self.videowidget = VideoWidget()
        self.videowidget.show()
        vbox.pack_start(self.videowidget)
        
        hbox = gtk.HBox()
        hbox.show()
        vbox.pack_start(hbox, fill=False, expand=False)
        
        self.adjustment = gtk.Adjustment(0.0, 0.00, 100.0, 0.1, 1.0, 1.0)
        hscale = gtk.HScale(self.adjustment)
        hscale.set_digits(2)
        hscale.set_update_policy(gtk.UPDATE_CONTINUOUS)
        hscale.connect('button-press-event', self.scale_button_press_cb)
        hscale.connect('button-release-event', self.scale_button_release_cb)
        hscale.connect('format-value', self.scale_format_value_cb)
        hbox.pack_start(hscale)
        hscale.show()
        self.hscale = hscale

        table = gtk.Table(2,3)
        table.show()
        vbox.pack_start(table, fill=False, expand=False, padding=6)

        self.button = button = gtk.Button(stock=gtk.STOCK_MEDIA_PLAY)
        button.set_property('can-default', True)
        button.set_focus_on_click(False)
        button.show()

        # problem: play and paused are of different widths and cause the
        # window to re-layout
        # "solution": add more buttons to a vbox so that the horizontal
        # width is enough
        bvbox = gtk.VBox()
        bvbox.add(button)
        bvbox.add(gtk.Button(stock=gtk.STOCK_MEDIA_PLAY))
        bvbox.add(gtk.Button(stock=gtk.STOCK_MEDIA_PAUSE))
        sizegroup = gtk.SizeGroup(gtk.SIZE_GROUP_HORIZONTAL)
        for kid in bvbox.get_children():
            sizegroup.add_widget(kid)
        bvbox.show()
        table.attach(bvbox, 0, 1, 0, 2, gtk.FILL, gtk.FILL)
        
        # can't set this property before the button has a window
        button.set_property('has-default', True)
        button.connect('clicked', lambda *args: self.play_toggled())

        self.cutin = cut = TimeControl(self, "Cut in time")
        cut.show()
        table.attach(cut, 1, 2, 0, 1, gtk.EXPAND, 0, 12)

        self.cutout = cut = TimeControl(self, "Cut out time")
        cut.show()
        table.attach(cut, 1, 2, 1, 2, gtk.EXPAND, 0, 12)

        button = gtk.Button("_Open other movie...")
        button.show()
        button.connect('clicked', lambda *x: self.do_choose_file())
        table.attach(button, 2, 3, 0, 1, gtk.FILL, gtk.FILL)

        button = gtk.Button("_Write to disk")
        button.set_property('image',
                            gtk.image_new_from_stock(gtk.STOCK_SAVE_AS,
                                                     gtk.ICON_SIZE_BUTTON))
        button.connect('clicked', lambda *x: self.do_remux())
        button.show()
        table.attach(button, 2, 3, 1, 2, gtk.FILL, gtk.FILL)

        #self.cutin.connect('notify::time', lambda *x: self.check_cutout())
        #self.cutout.connect('notify::time', lambda *x: self.check_cutin())

    def do_remux(self):
        if self.player.is_playing():
            self.play_toggled()
        in_uri = self.player.get_location()
        out_uri = in_uri[:-4] + '-remuxed.ogg'
        r = Remuxer(in_uri, out_uri,
                    self.cutin.get_time(), self.cutout.get_time())
        r.run(self)

    def do_choose_file(self):
        if self.player.is_playing():
            self.play_toggled()
        chooser = gtk.FileChooserDialog('Choose a movie to cut cut cut',
                                        self,
                                        buttons=(gtk.STOCK_CANCEL,
                                                 CANCELLED,
                                                 gtk.STOCK_OPEN,
                                                 SUCCESS))
        chooser.set_local_only(False)
        chooser.set_select_multiple(False)
        f = gtk.FileFilter()
        f.set_name("All files")
        f.add_pattern("*")
        chooser.add_filter(f)
        f = gtk.FileFilter()
        f.set_name("Ogg files")
        f.add_pattern("*.og[gvax]") # as long as this is the only thing we
                               # support...
        chooser.add_filter(f)
        chooser.set_filter(f)
        
        prev = self.player.get_location()
        if prev:
            chooser.set_uri(prev)

        resp = chooser.run()
        uri = chooser.get_uri()
        chooser.destroy()

        if resp == SUCCESS and uri != None:
            self.load_file(uri)
            return True
        else:
            return False
        
    def check_cutout(self):
        if self.cutout.get_time() <= self.cutin.get_time():
            pos, dur = self.player.query_position()
            self.cutout.set_time(dur)

    def check_cutin(self):
        if self.cutin.get_time() >= self.cutout.get_time():
            self.cutin.set_time(0)

    def play_toggled(self):
        if self.player.is_playing():
            self.player.pause()
            self.button.set_label(gtk.STOCK_MEDIA_PLAY)
        else:
            self.player.play()
            if self.update_id == -1:
                self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                                                     self.update_scale_cb)
            self.button.set_label(gtk.STOCK_MEDIA_PAUSE)

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
        
        self.button.set_sensitive(False)
        self.was_playing = self.player.is_playing()
        if self.was_playing:
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
        self.player.get_state(timeout=50*gst.MSECOND) # 50 ms

    def scale_button_release_cb(self, widget, event):
        # see seek.cstop_seek
        widget.disconnect(self.changed_id)
        self.changed_id = -1

        self.button.set_sensitive(True)
        if self.seek_timeout_id != -1:
            gobject.source_remove(self.seek_timeout_id)
            self.seek_timeout_id = -1
        else:
            gst.debug('released slider, setting back to playing')
            if self.was_playing:
                self.player.play()

        if self.update_id != -1:
            self.error('Had a previous update timeout id')
        else:
            self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                self.update_scale_cb)

    def update_scale_cb(self):
        had_duration = self.p_duration != gst.CLOCK_TIME_NONE
        self.p_position, self.p_duration = self.player.query_position()
        if self.p_position != gst.CLOCK_TIME_NONE:
            value = self.p_position * 100.0 / self.p_duration
            self.adjustment.set_value(value)
            if not had_duration:
                self.cutin.set_time(0)
        return True

def main(args):
    def usage():
        sys.stderr.write("usage: %s [URI-OF-MEDIA-FILE]\n" % args[0])
        return 1

    w = PlayerWindow()
    w.show()

    if len(args) == 1:
        if not w.do_choose_file():
            return 1
    elif len(args) == 2:
        if not gst.uri_is_valid(args[1]):
            sys.stderr.write("Error: Invalid URI: %s\n" % args[1])
            return 1
        w.load_file(args[1])
    else:
        return usage()

    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
