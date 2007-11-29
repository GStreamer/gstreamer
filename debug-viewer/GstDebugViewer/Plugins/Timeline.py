# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Debug Viewer - View and analyze GStreamer debug log files
#
#  Copyright (C) 2007 René Stadler <mail@renestadler.de>
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 3 of the License, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program.  If not, see <http://www.gnu.org/licenses/>.

"""GStreamer Debug Viewer timeline widget plugin."""

import logging

from GstDebugViewer import Common, Data, GUI
from GstDebugViewer.Plugins import *

import gobject
import gtk
import cairo

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
        self.ts_range = None

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

        if not partitions:
            return
        
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

        # Now handle the last one:
        data.append (tuple (counts))

        yield False

class UpdateProcess (object):

    def __init__ (self, freq_sentinel, dist_sentinel):

        self.freq_sentinel = freq_sentinel
        self.dist_sentinel = dist_sentinel
        self.is_running = False
        self.dispatcher = Common.Data.GSourceDispatcher ()

    def __process (self):

        if self.freq_sentinel is None or self.dist_sentinel is None:
            return

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

class VerticalTimelineWidget (gtk.DrawingArea):

    __gtype_name__ = "GstDebugViewerVerticalTimelineWidget"

    def __init__ (self):

        gtk.DrawingArea.__init__ (self)

        self.logger = logging.getLogger ("ui.vtimeline")

        self.theme = GUI.ThreadColorThemeTango ()
        self.params = None
        self.thread_colors = {}
        self.next_thread_color = 0

        self.connect ("expose-event", self.__handle_expose_event)
        self.connect ("size-request", self.__handle_size_request)

    def __handle_expose_event (self, self_, event):

        self.__draw (self.window)

    def __draw (self, drawable):

        ctx = drawable.cairo_create ()
        x, y, w, h = self.get_allocation ()

        # White background rectangle.
        ctx.set_line_width (0.)
        ctx.rectangle (0, 0, w, h)
        ctx.set_source_rgb (1., 1., 1.)
        ctx.fill ()
        ctx.new_path ()

        if self.params is None:
            return

        first_y, cell_height, data = self.params
        first_ts, last_ts = data[0][0], data[-1][0]
        ts_range = last_ts - first_ts

        if ts_range == 0:
            return

        ctx.set_line_width (1.)
        ctx.set_source_rgb (0., 0., 0.)

        half_height = cell_height // 2 - .5
        quarter_height = cell_height // 4 - .5
        first_y += half_height
        for i, i_data in enumerate (data):
            ts, thread = i_data
            if thread in self.thread_colors:
                ctx.set_source_rgb (*self.thread_colors[thread])
            else:
                self.next_thread_color += 1
                if self.next_thread_color == len (self.theme.colors):
                    self.next_thread_color = 0
                color = self.theme.colors[self.next_thread_color][0].float_tuple ()
                self.thread_colors[thread] = color
                ctx.set_source_rgb (*color)
            ts_fraction = float (ts - first_ts) / ts_range
            ts_offset = ts_fraction * h
            row_offset = first_y + i * cell_height
            ctx.move_to (-.5, ts_offset)
            ctx.line_to (half_height, ts_offset)
            ctx.line_to (w - quarter_height, row_offset)
            ctx.stroke ()
            ctx.line_to (w - quarter_height, row_offset)
            ctx.line_to (w + .5, row_offset - half_height)
            ctx.line_to (w + .5, row_offset + half_height)
            ctx.fill ()

    def __handle_size_request (self, self_, req):

        req.width = 64 # FIXME

    def clear (self):

        self.params = None
        self.thread_colors.clear ()
        self.next_thread_color = 0
        self.queue_draw ()

    def update (self, first_y, cell_height, data):

        # FIXME: Ideally we should take the vertical position difference of the
        # view into account (which is 0 with the current UI layout).

        self.params = (first_y, cell_height, data,)
        self.queue_draw ()

class TimelineWidget (gtk.DrawingArea):

    __gtype_name__ = "GstDebugViewerTimelineWidget"

    def __init__ (self):

        gtk.DrawingArea.__init__ (self)

        self.logger = logging.getLogger ("ui.timeline")

        self.process = UpdateProcess (None, None)
        self.connect ("expose-event", self.__handle_expose_event)
        self.connect ("configure-event", self.__handle_configure_event)
        self.connect ("size-request", self.__handle_size_request)
        self.process.handle_sentinel_progress = self.__handle_sentinel_progress
        self.process.handle_sentinel_finished = self.__handle_sentinel_finished
        self.process.handle_process_finished = self.__handle_process_finished

        self.model = None
        self.__offscreen = None

        self.__position_ts_range = None

    def __handle_sentinel_progress (self, sentinel):

        self.__redraw ()

    def __handle_sentinel_finished (self, sentinel):

        if sentinel == self.process.freq_sentinel:
            self.__redraw ()

    def __handle_process_finished (self):

        self.__redraw ()

    def __ensure_offscreen (self):

        x, y, w, h = self.get_allocation ()
        self.__offscreen = gtk.gdk.Pixmap (self.window, w, h, -1)
        if not self.__offscreen:
            raise ValueError ("could not obtain pixmap")

    def __redraw (self):

        if not self.props.visible:
            return

        self.__ensure_offscreen ()
        self.__draw (self.__offscreen)
        self.__update_from_offscreen ()

    def __update_from_offscreen (self):

        if not self.props.visible:
            return

        if self.__offscreen is None:
            self.__redraw ()

        gc = gtk.gdk.GC (self.window)
        self.window.draw_drawable (gc, self.__offscreen, 0, 0, 0, 0, -1, -1)
        self.__draw_position (self.window)

    def update (self, model):

        self.model = model

        width = self.get_allocation ()[2]

        self.process.abort ()
        if model:
            self.process.freq_sentinel = LineFrequencySentinel (model)
            self.process.dist_sentinel = LevelDistributionSentinel (self.process.freq_sentinel, model)
            self.process.freq_sentinel.run_for (width)
            self.process.run ()

    def clear (self):

        self.process.abort ()
        self.process.freq_sentinel = None
        self.process.dist_sentinel = None
        self.__redraw ()

    def update_position (self, start_ts, end_ts):

        self.__position_ts_range = (start_ts, end_ts,)

        if not self.process.freq_sentinel:
            return

        if not self.process.freq_sentinel.data:
            return

        self.__update_from_offscreen ()

    def find_indicative_time_step (self):

        MINIMUM_PIXEL_STEP = 32
        time_per_pixel = self.process.freq_sentinel.step
        return 32 # FIXME use self.freq_sentinel.step and len (self.process.freq_sentinel.data)

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

        if self.process.freq_sentinel is None:
            return

        # Vertical reference lines.
        pixel_step = self.find_indicative_time_step ()
        ctx.set_source_rgb (.9, .9, .9)
        for i in range (1, w // pixel_step + 1):
            x = i * pixel_step - .5
            ctx.move_to (x, 0)
            ctx.line_to (x, h)
            ctx.stroke ()

        if not self.process.freq_sentinel.data:
            self.logger.debug ("frequency sentinel has no data yet")
            return

        maximum = max (self.process.freq_sentinel.data)

        ctx.set_source_rgb (0., 0., 0.)
        self.__draw_graph (ctx, w, h, maximum, self.process.freq_sentinel.data)

        if not self.process.dist_sentinel.data:
            self.logger.debug ("level distribution sentinel has no data yet")
            return

        theme = GUI.LevelColorThemeTango ()
        dist_data = self.process.dist_sentinel.data

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

    def __have_position (self):

        if ((self.__position_ts_range is not None) and
            (self.process is not None) and
            (self.process.freq_sentinel is not None) and
            (self.process.freq_sentinel.ts_range is not None)):
            return True
        else:
            return False

    def __draw_position (self, drawable):

        if not self.__have_position ():
            return

        start_ts, end_ts = self.__position_ts_range
        first_ts, last_ts = self.process.freq_sentinel.ts_range
        step = self.process.freq_sentinel.step

        position1 = int (float (start_ts - first_ts) / step)
        position2 = int (float (end_ts - first_ts) / step)

        ctx = drawable.cairo_create ()
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

    def __handle_expose_event (self, self_, event):

        if self.__offscreen:
            self.__update_from_offscreen ()
        else:
            self.__redraw ()
        return True

    def __handle_configure_event (self, self_, event):

        self.logger.debug ("widget size configured to %ix%i",
                           event.width, event.height)

        if event.width < 16:
            return False

        self.update (self.model)

        return False

    def __handle_size_request (self, self_, req):

        # FIXME:
        req.height = 64

class TimelineState (Common.GUI.StateSection):

    _name = "timeline"

    shown = Common.GUI.StateBool ("shown", default = True)

class TimelineFeature (FeatureBase):

    def __init__ (self, app):

        self.logger = logging.getLogger ("ui.timeline")

        self.action_group = gtk.ActionGroup ("TimelineActions")
        self.action_group.add_toggle_actions ([("show-timeline",
                                                None, _("_Timeline"),)])

        self.state = app.state.sections[TimelineState._name]

    def handle_attach_window (self, window):

        self.log_view = window.log_view

        ui = window.ui_manager

        ui.insert_action_group (self.action_group, 0)
        
        self.merge_id = ui.new_merge_id ()
        ui.add_ui (self.merge_id, "/menubar/ViewMenu/ViewMenuAdditions",
                   "ViewTimeline", "show-timeline",
                   gtk.UI_MANAGER_MENUITEM, False)

        ui.add_ui (self.merge_id, "/", "TimelineContextMenu", None,
                   gtk.UI_MANAGER_POPUP, False)
        # TODO: Makes sense to have these here too, but we need to add logic to
        # the actions to associate the correct line with the activation.
        ## ui.add_ui (self.merge_id, "/TimelineContextMenu", "TimelineHideLinesBefore",
        ##            "hide-before-line", gtk.UI_MANAGER_MENUITEM, False)
        ## ui.add_ui (self.merge_id, "/TimelineContextMenu", "TimelineHideLinesAfter",
        ##            "hide-after-line", gtk.UI_MANAGER_MENUITEM, False)
        ui.add_ui (self.merge_id, "/TimelineContextMenu", "TimelineShowHiddenLines",
                   "show-hidden-lines", gtk.UI_MANAGER_MENUITEM, False)

        self.popup = ui.get_widget ("/TimelineContextMenu")
        
        box = window.get_top_attach_point ()

        self.timeline = TimelineWidget ()
        self.timeline.add_events (gtk.gdk.ALL_EVENTS_MASK) # FIXME
        self.timeline.connect ("button-press-event", self.handle_timeline_button_press_event)
        self.timeline.connect ("motion-notify-event", self.handle_timeline_motion_notify_event)
        box.pack_start (self.timeline, False, False, 0)
        self.timeline.hide ()

        box = window.get_side_attach_point ()

        self.vtimeline = VerticalTimelineWidget ()
        box.pack_start (self.vtimeline, False, False, 0)
        self.vtimeline.hide ()

        window.widgets.log_view_scrolled_window.props.vadjustment.connect ("value-changed",
                                                                           self.handle_log_view_adjustment_value_changed)

        handler = self.handle_show_action_toggled
        action = self.action_group.get_action ("show-timeline")
        action.connect ("toggled", handler)
        action.props.active = self.state.shown

        handler = self.handle_log_view_notify_model
        self.notify_model_id = self.log_view.connect ("notify::model", handler)

    def handle_detach_window (self, window):

        self.log_view.disconnect (self.notify_model_id)
        self.log_view = None

        window.ui_manager.remove_ui (self.merge_id)
        self.merge_id = None

        window.ui_manager.remove_action_group (self.action_group)

        self.timeline.destroy ()
        self.timeline = None

    def handle_attach_log_file (self, window, log_file):

        pass

    def handle_detach_log_file (self, window, log_file):

        self.timeline.clear ()
        self.vtimeline.clear ()

    def handle_log_view_notify_model (self, view, gparam):

        model = view.props.model

        if model is None:
            self.timeline.clear ()
            self.vtimeline.clear ()
            return
        
        self.timeline.update (model)

        # Need to dispatch these idly with a low priority to avoid triggering a
        # warning in treeview.get_visible_range:
        def idle_update ():
            self.update_timeline_position ()
            self.update_vtimeline ()
            return False
        gobject.idle_add (idle_update, priority = gobject.PRIORITY_LOW)

    def update_timeline_position (self):

        model = self.log_view.props.model
        visible_range = self.log_view.get_visible_range ()
        if visible_range is None:
            return
        start_path, end_path = visible_range
        ts1 = model.get_value (model.get_iter (start_path),
                               model.COL_TIME)
        ts2 = model.get_value (model.get_iter (end_path),
                               model.COL_TIME)
        
        self.timeline.update_position (ts1, ts2)

    def handle_log_view_adjustment_value_changed (self, adj):

        # FIXME: If not visible, disconnect this handler!
        if not self.timeline.props.visible:
            return

        self.update_timeline_position ()
        self.update_vtimeline ()

    def update_vtimeline (self):

        model = self.log_view.props.model
        visible_range = self.log_view.get_visible_range ()
        if visible_range is None:
            return
        start_path, end_path = visible_range

        if not start_path or not end_path:
            return

        column = self.log_view.get_column (0)
        bg_rect = self.log_view.get_background_area (start_path, column)
        cell_height = bg_rect.height
        cell_rect = self.log_view.get_cell_area (start_path, column)
        try:
            first_y = self.log_view.convert_bin_window_to_widget_coords (cell_rect.x, cell_rect.y)[1]
        except (AttributeError, SystemError,):
            # AttributeError is with PyGTK before 2.12.  SystemError is raised
            # with PyGTK 2.12.0, pygtk bug #479012.
            first_y = cell_rect.y % cell_height
            if not hasattr (self, "_warn_tree_view_coords"):
                self.logger.warning ("tree view coordinate conversion method "
                                     "not available, using aproximate offset")
                # Only warn once:
                self._warn_tree_view_coords = True

        data = []
        tree_iter = model.get_iter (start_path)
        while model.get_path (tree_iter) != end_path:
            data.append (model.get (tree_iter, model.COL_TIME, model.COL_THREAD))
            tree_iter = model.iter_next (tree_iter)

        self.vtimeline.update (first_y, cell_height, data)

    def handle_show_action_toggled (self, action):

        show = action.props.active

        if show:
            self.timeline.show ()
            self.vtimeline.show ()
            self.state.shown = True
        else:
            self.timeline.hide ()
            self.vtimeline.hide ()
            self.state.shown = False

    def handle_timeline_button_press_event (self, widget, event):

        if event.button == 3:
            self.popup.popup (None, None, None, event.button, event.get_time ())
            return True

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

        if not self.timeline.process.freq_sentinel:
            return True

        data = self.timeline.process.freq_sentinel.data
        if not data:
            return True

        if pos < 0:
            pos = 0
        elif pos >= len (data):
            pos = len (data) - 1

        count = sum (data[:pos + 1])

        model = self.log_view.props.model
        row = model[count]
        path = (count,)
        self.log_view.scroll_to_cell (path, use_align = True, row_align = .5)
        sel = self.log_view.get_selection ()
        sel.select_path (path)
        
        return False

class Plugin (PluginBase):

    features = [TimelineFeature]

    def __init__ (self, app):

        app.state.add_section_class (TimelineState)
        self.state = app.state.sections[TimelineState._name]
