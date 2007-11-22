
import logging

from GstDebugViewer import Common, Data, GUI
from GstDebugViewer.Plugins import *

import cairo
import gtk

def iter_model_reversed (model):

    count = model.iter_n_children (None)
    for i in xrange (count - 1, 0, -1):
        yield model[i]

class LineFrequencySentinel (object):

    def __init__ (self, model):

        self.model = model
        self.clear ()

    def clear (self):

        self.data = None
        self.n_partitions = None
        self.partitions = None
        self.step = None
        self.ts_range = (None, None,)        

    def _search_ts (self, target_ts, first_index, last_index):

        model_get = self.model.get_value
        model_iter_nth_child = self.model.iter_nth_child
        col_id = self.model.COL_TIME

        while True:
            middle = (last_index - first_index) // 2 + first_index
            if middle == first_index:
                return last_index
            ts = model_get (model_iter_nth_child (None, middle), col_id)
            if ts < target_ts:
                first_index = middle + 1
            elif ts > target_ts:
                last_index = middle - 1
            else:
                return middle

    def run_for (self, n):

        if n == 0:
            raise ValueError ("illegal value for n")

        self.n_partitions = n

    def process (self):

        model = self.model
        result = []
        partitions = []

        first_ts = None
        for row in self.model:
            first_ts = row[model.COL_TIME]
            if first_ts is not None:
                break

        if first_ts is None:
            return

        last_ts = None
        for row in iter_model_reversed (self.model):
            last_ts = row[model.COL_TIME]
            # FIXME: We ignore 0 here (unparsable lines!), this should be
            # handled differently!
            if last_ts:
                last_index = row.path[0]
                break

        if last_ts is None:
            return

        step = int (float (last_ts - first_ts) / float (self.n_partitions))

        YIELD_LIMIT = 100
        limit = YIELD_LIMIT

        first_index = 0
        target_ts = first_ts + step
        old_found = 0
        while target_ts < last_ts:
            limit -= 1
            if limit == 0:
                limit = YIELD_LIMIT
                yield True
            found = self._search_ts (target_ts, first_index, last_index)
            result.append (found - old_found)
            partitions.append (found)
            old_found = found
            first_index = found
            target_ts += step

        self.step = step
        self.data = result
        self.partitions = partitions
        self.ts_range = (first_ts, last_ts,)

class LevelDistributionSentinel (object):

    def __init__ (self, freq_sentinel, model):

        self.freq_sentinel = freq_sentinel
        self.model = model
        self.data = []

    def clear (self):

        del self.data[:]

    def process (self):

        YIELD_LIMIT = 10000
        y = YIELD_LIMIT

        model_get = self.model.get_value
        model_next = self.model.iter_next
        id_time = self.model.COL_TIME
        id_level = self.model.COL_LEVEL
        del self.data[:]
        data = self.data
        i = 0
        partitions_i = 0
        partitions = self.freq_sentinel.partitions
        counts = [0] * 6
        tree_iter = self.model.get_iter_first ()
        while tree_iter:
            y -= 1
            if y == 0:
                y = YIELD_LIMIT
                yield True
            level = model_get (tree_iter, id_level)
            if i > partitions[partitions_i]:
                data.append (tuple (counts))
                counts = [0] * 6
                partitions_i += 1
                if partitions_i == len (partitions):
                    # FIXME?
                    break
            i += 1
            counts[level] += 1
            tree_iter = model_next (tree_iter)

        # FIXME: We lose the last partition here!

        yield False

class UpdateProcess (object):

    def __init__ (self, freq_sentinel, dist_sentinel):

        self.freq_sentinel = freq_sentinel
        self.dist_sentinel = dist_sentinel
        self.is_running = False
        self.dispatcher = Common.Data.GSourceDispatcher ()

    def __process (self):

        self.is_running = True

        for x in self.freq_sentinel.process ():
            yield True

        self.handle_sentinel_finished (self.freq_sentinel)

        for x in self.dist_sentinel.process ():
            yield True
            self.handle_sentinel_progress (self.dist_sentinel)

        self.is_running = False

        self.handle_sentinel_finished (self.dist_sentinel)
        self.handle_process_finished ()

        yield False

    def run (self):

        if self.is_running:
            return

        self.dispatcher (self.__process ())

    def abort (self):

        if not self.is_running:
            return

        self.dispatcher.cancel ()
        self.is_running = False

    def handle_sentinel_progress (self, sentinel):

        pass

    def handle_sentinel_finished (self, sentinel):

        pass

    def handle_process_finished (self):

        pass

class TimelineWidget (gtk.DrawingArea):

    __gtype_name__ = "GstDebugViewerTimelineWidget"

    def __init__ (self, log_model):

        gtk.DrawingArea.__init__ (self)

        self.logger = logging.getLogger ("ui.timeline")

        self.freq_sentinel = LineFrequencySentinel (log_model)
        self.dist_sentinel = LevelDistributionSentinel (self.freq_sentinel, log_model)
        self.process = UpdateProcess (self.freq_sentinel, self.dist_sentinel)
        self.connect ("expose-event", self.__handle_expose_event)
        self.connect ("configure-event", self.__handle_configure_event)
        self.connect ("size-request", self.__handle_size_request)
        self.process.handle_sentinel_progress = self.handle_sentinel_progress
        self.process.handle_sentinel_finished = self.handle_sentinel_finished
        self.process.handle_process_finished = self.handle_process_finished

        self.__offscreen = None

    def handle_sentinel_progress (self, sentinel):

        self.__redraw ()

    def handle_sentinel_finished (self, sentinel):

        if sentinel == self.freq_sentinel:
            self.__redraw ()

    def handle_process_finished (self):

        self.__redraw ()

    def __redraw (self):

        if not self.props.visible:
            return

        x, y, w, h = self.get_allocation ()
        self.__offscreen = gtk.gdk.Pixmap (self.window, w, h, -1)

        self.__draw (self.__offscreen)

        self.__update ()

    def __update (self):

        if not self.props.visible:
            return

        if self.__offscreen is None:
            self.__redraw ()

        gc = gtk.gdk.GC (self.window)
        self.window.draw_drawable (gc, self.__offscreen, 0, 0, 0, 0, -1, -1)

    def update (self):

        width = self.get_allocation ()[2]

        self.process.abort ()
        self.freq_sentinel.clear ()
        self.dist_sentinel.clear ()
        self.freq_sentinel.run_for (width)
        self.process.run ()

    def update_position (self, start_ts, end_ts):

        if not self.freq_sentinel.data:
            return

        self.__update ()

        first_ts, last_ts = self.freq_sentinel.ts_range
        step = self.freq_sentinel.step

        position1 = int (float (start_ts - first_ts) / step)
        position2 = int (float (end_ts - first_ts) / step)

        ctx = self.window.cairo_create ()
        x, y, w, h = self.get_allocation ()

        line_width = position2 - position1
        if line_width <= 1:
            ctx.set_source_rgb (1., 0., 0.)
            ctx.set_line_width (1.)
            ctx.move_to (position1 + .5, 0)
            ctx.line_to (position1 + .5, h)
            ctx.stroke ()
        else:
            ctx.set_source_rgba (1., 0., 0., .5)
            ctx.rectangle (position1, 0, line_width, h)
            ctx.fill ()

    def find_indicative_time_step (self):

        MINIMUM_PIXEL_STEP = 32
        time_per_pixel = self.freq_sentinel.step
        return 32 # FIXME use self.freq_sentinel.step and len (self.freq_sentinel.data)

    def __draw (self, drawable):

        ctx = drawable.cairo_create ()
        x, y, w, h = self.get_allocation ()

        # White background rectangle.
        ctx.set_line_width (0.)
        ctx.rectangle (0, 0, w, h)
        ctx.set_source_rgb (1., 1., 1.)
        ctx.fill ()
        ctx.new_path ()

        # Horizontal reference lines.
        ctx.set_line_width (1.)
        ctx.set_source_rgb (.95, .95, .95)
        for i in range (h // 16):
            y = i * 16 - .5
            ctx.move_to (0, y)
            ctx.line_to (w, y)
            ctx.stroke ()

        # Vertical reference lines.
        pixel_step = self.find_indicative_time_step ()
        ctx.set_source_rgb (.9, .9, .9)
        for i in range (1, w // pixel_step + 1):
            x = i * pixel_step - .5
            ctx.move_to (x, 0)
            ctx.line_to (x, h)
            ctx.stroke ()

        if not self.freq_sentinel.data:
            self.logger.debug ("frequency sentinel has no data yet")
            return

        maximum = max (self.freq_sentinel.data)

        ctx.set_source_rgb (0., 0., 0.)
        self.__draw_graph (ctx, w, h, maximum, self.freq_sentinel.data)

        if not self.dist_sentinel.data:
            self.logger.debug ("level distribution sentinel has no data yet")
            return

        theme = GUI.LevelColorThemeTango ()
        dist_data = self.dist_sentinel.data

        def cumulative_level_counts (*levels):
            for level_counts in dist_data:
                yield sum ((level_counts[level] for level in levels))

        level = Data.debug_level_info
        levels_prev = (Data.debug_level_log, Data.debug_level_debug,)
        ctx.set_source_rgb (*(theme.colors_float (level)[1]))
        self.__draw_graph (ctx, w, h, maximum,
                           list (cumulative_level_counts (level, *levels_prev)))

        level = Data.debug_level_debug
        levels_prev = (Data.debug_level_log,)
        ctx.set_source_rgb (*(theme.colors_float (level)[1]))
        self.__draw_graph (ctx, w, h, maximum,
                           list (cumulative_level_counts (level, *levels_prev)))

        level = Data.debug_level_log
        ctx.set_source_rgb (*(theme.colors_float (level)[1]))
        self.__draw_graph (ctx, w, h, maximum, [counts[level] for counts in dist_data])

        # Draw error and warning triangle indicators:

        for level in (Data.debug_level_warning, Data.debug_level_error,):
            ctx.set_source_rgb (*(theme.colors_float (level)[1]))
            for i, counts in enumerate (dist_data):
                if counts[level] == 0:
                    continue
                SIZE = 8
                ctx.move_to (i - SIZE // 2, 0)
                ctx.line_to (i + SIZE // 2, 0)
                ctx.line_to (i, SIZE / 1.41)
                ctx.close_path ()
                ctx.fill ()

    def __draw_graph (self, ctx, w, h, maximum, data):

        if not data:
            return

        from operator import add
        heights = [h * float (d) / maximum for d in data]
        ctx.move_to (0, h)
        for i in range (len (heights)):
            ctx.line_to (i - .5, h - heights[i] + .5)

        ctx.line_to (i, h)
        ctx.close_path ()
        
        ctx.fill ()

    def __handle_expose_event (self, self_, event):

        self.__redraw ()
        return True

    def __handle_configure_event (self, self_, event):

        self.logger.debug ("widget size configured to %ix%i",
                           event.width, event.height)

        if event.width < 16:
            return False

        self.update ()

        return False

    def __handle_size_request (self, self_, req):

        # FIXME:
        req.height = 64

class TimelineFeature (FeatureBase):

    state_section_name = "timeline"

    def __init__ (self):

        self.action_group = gtk.ActionGroup ("TimelineActions")
        self.action_group.add_toggle_actions ([("show-timeline",
                                                None, _("_Timeline"),)])

    def attach (self, window):

        self.log_model = window.log_model
        self.log_filter = window.log_filter
        self.log_view = window.log_view

        ui = window.ui_manager

        ui.insert_action_group (self.action_group, 0)
        
        self.merge_id = ui.new_merge_id ()
        ui.add_ui (self.merge_id, "/menubar/ViewMenu/ViewMenuAdditions",
                   "ViewTimeline", "show-timeline",
                   gtk.UI_MANAGER_MENUITEM, False)

        box = window.get_top_attach_point ()

        self.timeline = TimelineWidget (self.log_model)
        self.timeline.add_events (gtk.gdk.ALL_EVENTS_MASK) # FIXME
        self.timeline.connect ("button-press-event", self.handle_timeline_button_press_event)
        self.timeline.connect ("motion-notify-event", self.handle_timeline_motion_notify_event)
        box.pack_start (self.timeline, False, False, 0)
        self.timeline.hide ()

        window.widgets.log_view_scrolled_window.props.vadjustment.connect ("value-changed",
                                                                           self.handle_log_view_adjustment_value_changed)

        handler = self.handle_show_action_toggled
        action = self.action_group.get_action ("show-timeline")
        action.connect ("toggled", handler)
        action.activate ()

    def detach (self, window):

        window.ui_manager.remove_ui (self.merge_id)
        self.merge_id = None

        window.ui_manager.remove_action_group (self.action_group)

        self.timeline.destroy ()
        self.timeline = None

    def handle_log_file_changed (self):

        self.timeline.update ()

    def handle_log_view_adjustment_value_changed (self, adj):

        # FIXME: If not visible, disconnect this handler!
        if not self.timeline.props.visible:
            return

        start_path, end_path = self.log_view.get_visible_range ()
        ts1 = self.log_model.get (self.log_model.get_iter (start_path),
                                  self.log_model.COL_TIME)[0]
        ts2 = self.log_model.get (self.log_model.get_iter (end_path),
                                  self.log_model.COL_TIME)[0]
        self.timeline.update_position (ts1, ts2)

    def handle_show_action_toggled (self, action):

        show = action.props.active

        if show:
            self.timeline.show ()
        else:
            self.timeline.hide ()

    def handle_timeline_button_press_event (self, widget, event):

        if event.button != 1:
            return True

        pos = int (event.x)
        self.goto_time_position (pos)
        return False

    def handle_timeline_motion_notify_event (self, widget, event):

        if not event.state & gtk.gdk.BUTTON1_MASK:
            return True

        pos = int (event.x)
        self.goto_time_position (pos)
        return False

    def goto_time_position (self, pos):

        data = self.timeline.freq_sentinel.data
        if not data:
            return True
        count = sum (data[:pos + 1])

        row = self.log_model[count]
        self.log_view.scroll_to_cell ((count,), use_align = True, row_align = .5)
        
        return False

class Plugin (PluginBase):

    features = [TimelineFeature]
