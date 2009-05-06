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

class SyncPoints(gtk.VBox):
    def __init__(self, window):
        gtk.VBox.__init__(self)
        self.pwindow = window
        self.create_ui()

    def get_time_as_str(self, iter, i):
        value = self.model.get_value(iter, i)
        ret = ''
        for div, sep, mod, pad in ((gst.SECOND*60, '', 0, 0),
                                   (gst.SECOND, ':', 60, 2),
                                   (gst.MSECOND, '.', 1000, 3)):
            n = value // div
            if mod:
                n %= mod
            ret += sep + ('%%0%dd' % pad) % n
        return ret

    def create_ui(self):
        self.model = model = gtk.ListStore(gobject.TYPE_UINT64,
                                           gobject.TYPE_UINT64)
        self.view = view = gtk.TreeView(self.model)

        renderer = gtk.CellRendererText()
        column = gtk.TreeViewColumn("Audio time", renderer)
        def time_to_text(column, cell, method, iter, i):
            cell.set_property('text', self.get_time_as_str(iter, i))
        column.set_cell_data_func(renderer, time_to_text, 0)
        column.set_expand(True)
        column.set_clickable(True)
        view.append_column(column)
        
        renderer = gtk.CellRendererText()
        column = gtk.TreeViewColumn("Video time", renderer)
        column.set_cell_data_func(renderer, time_to_text, 1)
        column.set_expand(True)
        view.append_column(column)
        
        view.show()
        self.pack_start(view, True, True, 6)

        hbox = gtk.HBox(False, 0)
        hbox.show()
        self.pack_start(hbox, False, False, 0)

        add = gtk.Button(stock=gtk.STOCK_ADD)
        add.show()
        def add_and_select(*x):
            iter = model.append()
            self.view.get_selection().select_iter(iter)
            self.changed()
        add.connect("clicked", add_and_select)
        hbox.pack_end(add, False, False, 0)
        
        remove = gtk.Button(stock=gtk.STOCK_REMOVE)
        remove.show()
        def remove_selected(*x):
            model, iter = self.view.get_selection().get_selected()
            model.remove(iter)
            self.changed()
        remove.connect("clicked", remove_selected)
        hbox.pack_end(remove, False, False, 0)
        
        pad = gtk.Label('   ')
        pad.show()
        hbox.pack_end(pad)

        label = gtk.Label("Set: ")
        label.show()
        hbox.pack_start(label)

        a = gtk.Button("A_udio")
        a.show()
        a.connect("clicked", lambda *x: self.set_selected_audio_now())
        hbox.pack_start(a)

        l = gtk.Label(" / ")
        l.show()
        hbox.pack_start(l)

        v = gtk.Button("_Video")
        v.show()
        v.connect("clicked", lambda *x: self.set_selected_video_now())
        hbox.pack_start(v)

    def get_sync_points(self):
        def get_value(row, i):
            return self.model.get_value(row.iter, i)
        pairs = [(get_value(row, 1), get_value(row, 0)) for row in self.model]
        pairs.sort()
        ret = []
        maxdiff = 0
        for pair in pairs:
            maxdiff = max(maxdiff, abs(pair[1] - pair[0]))
            ret.extend(pair)
        return ret, maxdiff

    def changed(self):
        print 'Sync times now:'
        for index, row in enumerate(self.model):
            print 'A/V %d: %s -- %s' % (index,
                                        self.get_time_as_str(row.iter, 0),
                                        self.get_time_as_str(row.iter, 1))
            

    def set_selected_audio(self, time):
        sel = self.view.get_selection()
        model, iter = sel.get_selected()
        if iter:
            model.set_value(iter, 0, time)
        self.changed()

    def set_selected_video(self, time):
        sel = self.view.get_selection()
        model, iter = sel.get_selected()
        if iter:
            model.set_value(iter, 1, time)
        self.changed()

    def set_selected_audio_now(self):
        time, dur = self.pwindow.player.query_position()
        self.set_selected_audio(time)

    def set_selected_video_now(self):
        # pause and preroll first
        if self.pwindow.player.is_playing():
            self.pwindow.play_toggled()
        self.pwindow.player.get_state(timeout=gst.MSECOND * 200)

        time, dur = self.pwindow.player.query_position()
        self.set_selected_video(time)

    def seek_and_pause(self, time):
        if self.pwindow.player.is_playing():
            self.pwindow.play_toggled()
        self.pwindow.player.seek(time)
        if self.pwindow.player.is_playing():
            self.pwindow.play_toggled()
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
    def __init__(self, parent, fromname, toname):
        ProgressDialog.__init__(self,
                                "Writing to disk",
                                ('Writing the newly synchronized <b>%s</b> '
                                 'to <b>%s</b>. This may take some time.'
                                 % (fromname, toname)),
                                'Starting media pipeline',
                                parent,
                                gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                                (gtk.STOCK_CANCEL, CANCELLED,
                                 gtk.STOCK_CLOSE, SUCCESS))
        self.set_completed(False)
        
    def update_position(self, pos, dur):
        remaining = dur - pos
        minutes = remaining // (gst.SECOND * 60)
        seconds = (remaining - minutes * gst.SECOND * 60) // gst.SECOND
        self.progress.set_text('%d:%02d of video remaining' % (minutes, seconds))
        self.progress.set_fraction(1.0 - float(remaining) / dur)

    def set_completed(self, completed):
        self.set_response_sensitive(CANCELLED, not completed)
        self.set_response_sensitive(SUCCESS, completed)

class Resynchronizer(gst.Pipeline):

    __gsignals__ = {'done': (gobject.SIGNAL_RUN_LAST, None, (int,))}

    def __init__(self, fromuri, touri, (syncpoints, maxdiff)):
        # HACK: should do Pipeline.__init__, but that doesn't do what we
        # want; there's a bug open aboooot that
        self.__gobject_init__()

        self.fromuri = fromuri
        self.touri = None
        self.syncpoints = syncpoints
        self.maxdiff = maxdiff

        self.src = self.resyncbin = self.sink = None
        self.resolution = UNKNOWN

        self.window = None
        self.pdialog = None

        self._query_id = -1

    def do_setup_pipeline(self):
        self.src = gst.element_make_from_uri(gst.URI_SRC, self.fromuri)
        self.resyncbin = ResyncBin(self.syncpoints, self.maxdiff)
        self.sink = gst.element_make_from_uri(gst.URI_SINK, self.touri)
        self.resolution = UNKNOWN

        if gobject.signal_lookup('allow-overwrite', self.sink.__class__):
            self.sink.connect('allow-overwrite', lambda *x: True)

        self.add(self.src, self.resyncbin, self.sink)

        self.src.link(self.resyncbin)
        self.resyncbin.link(self.sink)

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
                pad = self.resyncbin.get_pad('src')
                pos, format = pad.query_position(gst.FORMAT_TIME)
                dur, format = pad.query_duration(gst.FORMAT_TIME)
                if pos != gst.CLOCK_TIME_NONE:
                    self.pdialog.update_position(pos, duration)
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
            self.pdialog.update_position(1,1)
            self._stop_queries()
            self.pdialog.set_completed(True)
        elif message.type == gst.MESSAGE_STATE_CHANGED:
            if message.src == self:
                old, new, pending = message.parse_state_changed()
                if ((old, new, pending) ==
                    (gst.STATE_READY, gst.STATE_PAUSED,
                     gst.STATE_VOID_PENDING)):
                    self.pdialog.set_task('Processing file')
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
        self.pdialog = RemuxProgressDialog(main_window, fromname, toname)
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
        
class ResyncBin(gst.Bin):
    def __init__(self, sync_points, maxdiff):
        self.__gobject_init__()

        self.parsefactories = self._find_parsers()
        self.parsers = []

        self.demux = gst.element_factory_make('oggdemux')
        self.mux = gst.element_factory_make('oggmux')

        self.add(self.demux, self.mux)

        self.add_pad(gst.GhostPad('sink', self.demux.get_pad('sink')))
        self.add_pad(gst.GhostPad('src', self.mux.get_pad('src')))

        self.demux.connect('pad-added', self._new_demuxed_pad)

        self.sync_points = sync_points
        self.maxdiff = maxdiff

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

        queue = gst.element_factory_make('queue', 'queue_' + format)
        queue.set_property('max-size-buffers', 0)
        queue.set_property('max-size-bytes', 0)
        print self.maxdiff
        queue.set_property('max-size-time', int(self.maxdiff * 1.5))
        parser = gst.element_factory_make(self.parsefactories[format])
        self.add(queue)
        self.add(parser)
        queue.set_state(gst.STATE_PAUSED)
        parser.set_state(gst.STATE_PAUSED)
        pad.link(queue.get_compatible_pad(pad))
        queue.link(parser)
        parser.link(self.mux)
        self.parsers.append(parser)

        print repr(self.sync_points)

        if 'video' in format:
            parser.set_property('synchronization-points',
                                self.sync_points)

class PlayerWindow(gtk.Window):
    UPDATE_INTERVAL = 500
    def __init__(self):
        gtk.Window.__init__(self)
        self.set_default_size(600, 500)

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

        table = gtk.Table(3,3)
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
        table.attach(bvbox, 0, 1, 1, 3, gtk.FILL, gtk.FILL)
        
        # can't set this property before the button has a window
        button.set_property('has-default', True)
        button.connect('clicked', lambda *args: self.play_toggled())

        self.sync = sync = SyncPoints(self)
        sync.show()
        table.attach(sync, 1, 2, 0, 3, gtk.EXPAND, gtk.EXPAND|gtk.FILL, 12)
        # nasty things to get sizes
        l = gtk.Label('\n\n\n')
        l.show()
        table.attach(l, 0, 1, 0, 1, 0, 0, 0)
        l = gtk.Label('\n\n\n')
        l.show()
        table.attach(l, 2, 3, 0, 1, 0, 0, 0)

        button = gtk.Button("_Open other movie...")
        button.show()
        button.connect('clicked', lambda *x: self.do_choose_file())
        table.attach(button, 2, 3, 1, 2, gtk.FILL, gtk.FILL)

        button = gtk.Button("_Write to disk")
        button.set_property('image',
                            gtk.image_new_from_stock(gtk.STOCK_SAVE_AS,
                                                     gtk.ICON_SIZE_BUTTON))
        button.connect('clicked', lambda *x: self.do_remux())
        button.show()
        table.attach(button, 2, 3, 2, 3, gtk.FILL, gtk.FILL)

    def do_remux(self):
        if self.player.is_playing():
            self.play_toggled()
        in_uri = self.player.get_location()
        out_uri = in_uri[:-4] + '-remuxed.ogg'
        r = Resynchronizer(in_uri, out_uri, self.sync.get_sync_points())
        r.run(self)

    def do_choose_file(self):
        if self.player.is_playing():
            self.play_toggled()
        chooser = gtk.FileChooserDialog('Choose a movie to bork bork bork',
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
        f.add_pattern("*.ogg") # as long as this is the only thing we
                               # support...
        chooser.add_filter(f)
        chooser.set_filter(f)
        
        prev = self.player.get_location()
        if prev:
            chooser.set_uri(prev)

        resp = chooser.run()
        uri = chooser.get_uri()
        chooser.destroy()

        if resp == SUCCESS:
            self.load_file(uri)
            return True
        else:
            return False
        
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
