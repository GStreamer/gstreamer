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

from GstDebugViewer import Common, Data
from GstDebugViewer.GUI.colors import LevelColorThemeTango, ThreadColorThemeTango
from GstDebugViewer.Plugins import FeatureBase, PluginBase

from gettext import gettext as _
from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import Gdk
import cairo


def iter_model_reversed(model):

    count = model.iter_n_children(None)
    for i in range(count - 1, 0, -1):
        yield model[i]


class LineFrequencySentinel (object):

    def __init__(self, model):

        self.model = model
        self.clear()

    def clear(self):

        self.data = None
        self.n_partitions = None
        self.partitions = None
        self.step = None
        self.ts_range = None

    def _search_ts(self, target_ts, first_index, last_index):

        model_get = self.model.get_value
        model_iter_nth_child = self.model.iter_nth_child
        col_id = self.model.COL_TIME

        # TODO: Rewrite using a lightweight view object + bisect.

        while True:
            middle = (last_index - first_index) // 2 + first_index
            if middle == first_index:
                return first_index
            ts = model_get(model_iter_nth_child(None, middle), col_id)
            if ts < target_ts:
                first_index = middle
            elif ts > target_ts:
                last_index = middle
            else:
                return middle

    def run_for(self, n):

        if n == 0:
            raise ValueError("illegal value for n")

        self.n_partitions = n

    def process(self):

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
        i = 0
        UNPARSABLE_LIMIT = 500
        for row in iter_model_reversed(self.model):
            last_ts = row[model.COL_TIME]
            # FIXME: We ignore 0 here (unparsable lines!), this should be
            # handled differently!
            i += 1
            if i == UNPARSABLE_LIMIT:
                break
            if last_ts:
                last_index = row.path[0]
                break

        if last_ts is None or last_ts < first_ts:
            return

        step = int(float(last_ts - first_ts) / float(self.n_partitions))

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
            found = self._search_ts(target_ts, first_index, last_index)
            result.append(found - old_found)
            partitions.append(found)
            old_found = found
            first_index = found
            target_ts += step

        if step == 0:
            result = []
            partitions = []

        self.step = step
        self.data = result
        self.partitions = partitions
        self.ts_range = (first_ts, last_ts,)


class LevelDistributionSentinel (object):

    def __init__(self, freq_sentinel, model):

        self.freq_sentinel = freq_sentinel
        self.model = model
        self.data = []

    def clear(self):

        del self.data[:]

    def process(self):

        MAX_LEVELS = 9
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
        counts = [0] * MAX_LEVELS
        tree_iter = self.model.get_iter_first()

        if not partitions:
            return

        level_index = 0
        level_iter = None

        finished = False
        while tree_iter:
            y -= 1
            if y == 0:
                y = YIELD_LIMIT
                yield True
            if level_iter is None:
                stop_index = level_index + 512
                levels = self.model.get_value_range(id_level,
                                                    level_index, stop_index)
                level_index = stop_index
                level_iter = iter(levels)
            try:
                level = level_iter.__next__()
            except StopIteration:
                level_iter = None
                continue
            while i > partitions[partitions_i]:
                data.append(tuple(counts))
                counts = [0] * MAX_LEVELS
                partitions_i += 1
                if partitions_i == len(partitions):
                    finished = True
                    break
            if finished:
                break
            counts[level] += 1
            i += 1

        # Now handle the last one:
        data.append(tuple(counts))

        yield False


class UpdateProcess (object):

    def __init__(self, freq_sentinel, dist_sentinel):

        self.freq_sentinel = freq_sentinel
        self.dist_sentinel = dist_sentinel
        self.is_running = False
        self.dispatcher = Common.Data.GSourceDispatcher()

    def __process(self):

        if self.freq_sentinel is None or self.dist_sentinel is None:
            return

        self.is_running = True

        for x in self.freq_sentinel.process():
            yield True

        self.handle_sentinel_finished(self.freq_sentinel)

        for x in self.dist_sentinel.process():
            yield True
            self.handle_sentinel_progress(self.dist_sentinel)

        self.is_running = False

        self.handle_sentinel_finished(self.dist_sentinel)
        self.handle_process_finished()

        yield False

    def run(self):

        if self.is_running:
            return

        self.dispatcher(self.__process())

    def abort(self):

        if not self.is_running:
            return

        self.dispatcher.cancel()
        self.is_running = False

    def handle_sentinel_progress(self, sentinel):

        pass

    def handle_sentinel_finished(self, sentinel):

        pass

    def handle_process_finished(self):

        pass


class VerticalTimelineWidget (Gtk.DrawingArea):

    __gtype_name__ = "GstDebugViewerVerticalTimelineWidget"

    def __init__(self, log_view):

        GObject.GObject.__init__(self)

        self.logger = logging.getLogger("ui.vtimeline")

        self.log_view = log_view
        self.theme = ThreadColorThemeTango()
        self.params = None
        self.thread_colors = {}
        self.next_thread_color = 0

        try:
            self.set_tooltip_text(_("Vertical timeline\n"
                                    "Different colors represent different threads"))
        except AttributeError:
            # Compatibility.
            pass

    def do_draw(self, ctx):

        alloc = self.get_allocation()
        x = alloc.x
        y = alloc.y
        w = alloc.width
        h = alloc.height

        # White background rectangle.
        ctx.set_line_width(0.)
        ctx.rectangle(0, 0, w, h)
        ctx.set_source_rgb(1., 1., 1.)
        ctx.fill()
        ctx.new_path()

        if self.params is None:
            self.__update_params()

        if self.params is None:
            return

        first_y, cell_height, data = self.params
        if len(data) < 2:
            return
        first_ts, last_ts = data[0][0], data[-1][0]
        ts_range = last_ts - first_ts
        if ts_range == 0:
            return

        ctx.set_line_width(1.)
        ctx.set_source_rgb(0., 0., 0.)

        half_height = cell_height // 2 - .5
        quarter_height = cell_height // 4 - .5
        first_y += half_height
        for i, i_data in enumerate(data):
            ts, thread = i_data
            if thread in self.thread_colors:
                ctx.set_source_rgb(*self.thread_colors[thread])
            else:
                self.next_thread_color += 1
                if self.next_thread_color == len(self.theme.colors):
                    self.next_thread_color = 0
                color = self.theme.colors[
                    self.next_thread_color][0].float_tuple()
                self.thread_colors[thread] = color
                ctx.set_source_rgb(*color)
            ts_fraction = float(ts - first_ts) / ts_range
            ts_offset = ts_fraction * h
            row_offset = first_y + i * cell_height
            ctx.move_to(-.5, ts_offset)
            ctx.line_to(half_height, ts_offset)
            ctx.line_to(w - quarter_height, row_offset)
            ctx.stroke()
            ctx.line_to(w - quarter_height, row_offset)
            ctx.line_to(w + .5, row_offset - half_height)
            ctx.line_to(w + .5, row_offset + half_height)
            ctx.fill()
        return True

    def do_configure_event(self, event):

        self.params = None
        self.queue_draw()

        return False

    def do_get_preferred_width(self):

        return 64, 64  # FIXME

    def clear(self):

        self.params = None
        self.thread_colors.clear()
        self.next_thread_color = 0
        self.queue_draw()

    def __update_params(self):

        # FIXME: Ideally we should take the vertical position difference of the
        # view into account (which is 0 with the current UI layout).

        view = self.log_view
        model = view.get_model()
        visible_range = view.get_visible_range()
        if visible_range is None:
            return
        start_path, end_path = visible_range

        if not start_path or not end_path:
            return

        column = view.get_column(0)
        bg_rect = view.get_background_area(start_path, column)
        cell_height = bg_rect.height
        cell_rect = view.get_cell_area(start_path, column)
        try:
            first_y = view.convert_bin_window_to_widget_coords(
                cell_rect.x, cell_rect.y)[1]
        except (AttributeError, SystemError,):
            # AttributeError is with PyGTK before 2.12.  SystemError is raised
            # with PyGTK 2.12.0, pygtk bug #479012.
            first_y = cell_rect.y % cell_height

            global _warn_tree_view_coords
            try:
                _warn_tree_view_coords
            except NameError:
                self.logger.warning("tree view coordinate conversion method "
                                    "not available, using aproximate offset")
                # Only warn once:
                _warn_tree_view_coords = True

        data = []
        tree_iter = model.get_iter(start_path)
        if tree_iter is None:
            return
        while model.get_path(tree_iter) != end_path:
            data.append(
                model.get(tree_iter, model.COL_TIME, model.COL_THREAD))
            tree_iter = model.iter_next(tree_iter)

        self.params = (first_y, cell_height, data,)

    def update(self):

        self.params = None
        self.queue_draw()


class TimelineWidget (Gtk.DrawingArea):

    __gtype_name__ = "GstDebugViewerTimelineWidget"

    __gsignals__ = {"change-position": (GObject.SignalFlags.RUN_LAST,
                                        None,
                                        (GObject.TYPE_INT,),)}

    def __init__(self):

        GObject.GObject.__init__(self)

        self.logger = logging.getLogger("ui.timeline")

        self.add_events(Gdk.EventMask.BUTTON1_MOTION_MASK |
                        Gdk.EventMask.BUTTON_PRESS_MASK |
                        Gdk.EventMask.BUTTON_RELEASE_MASK)

        self.process = UpdateProcess(None, None)
        self.process.handle_sentinel_progress = self.__handle_sentinel_progress
        self.process.handle_sentinel_finished = self.__handle_sentinel_finished

        self.model = None
        self.__offscreen = None
        self.__offscreen_size = (0, 0)
        self.__offscreen_dirty = (0, 0)

        self.__position_ts_range = None

        try:
            self.set_tooltip_text(_("Log event histogram\n"
                                    "Different colors represent different log-levels"))
        except AttributeError:
            # Compatibility.
            pass

    def __handle_sentinel_progress(self, sentinel):

        if sentinel == self.process.dist_sentinel:
            old_progress = self.__dist_sentinel_progress
            new_progress = len(sentinel.data)
            if new_progress - old_progress >= 32:
                self.__invalidate_offscreen(old_progress, new_progress)
                self.__dist_sentinel_progress = new_progress

    def __handle_sentinel_finished(self, sentinel):

        if sentinel == self.process.freq_sentinel:
            self.__invalidate_offscreen(0, -1)
        else:
            self.__invalidate_offscreen(self.__dist_sentinel_progress, -1)

    def __ensure_offscreen(self):

        alloc = self.get_allocation()
        if self.__offscreen_size == (alloc.width, alloc.height):
            return

        self.__offscreen = cairo.ImageSurface(
            cairo.FORMAT_ARGB32, alloc.width, alloc.height)
        self.__offscreen_size = (alloc.width, alloc.height)
        self.__offscreen_dirty = (0, alloc.width)
        if not self.__offscreen:
            self.__offscreen_size = (0, 0)
            raise ValueError("could not obtain offscreen image surface")

    def __invalidate_offscreen(self, start, stop):

        alloc = self.get_allocation()
        if stop < 0:
            stop += alloc.width

        dirty_start, dirty_stop = self.__offscreen_dirty
        if dirty_start != dirty_stop:
            dirty_start = min(dirty_start, start)
            dirty_stop = max(dirty_stop, stop)
        else:
            dirty_start = start
            dirty_stop = stop
        self.__offscreen_dirty = (dirty_start, dirty_stop)

        # Just like in __draw_offscreen. FIXME: Need this in one place!
        start -= 8
        stop += 8
        self.queue_draw_area(start, 0, stop - start, alloc.height)

    def __draw_from_offscreen(self, ctx):

        if not self.props.visible:
            return

        alloc = self.get_allocation()
        offscreen_width, offscreen_height = self.__offscreen_size
        rect = Gdk.Rectangle()  # TODO: damage region
        rect.x, rect.y, rect.width, rect.height = 0, 0, alloc.width, alloc.height

        # Fill the background (where the offscreen pixmap doesn't fit) with
        # white. This happens after enlarging the window, until all sentinels
        # have finished running.
        if offscreen_width < alloc.width or offscreen_height < alloc.height:
            ctx.rectangle(rect.x, rect.y, rect.width, rect.height)
            ctx.clip()

            if offscreen_width < alloc.width:
                ctx.rectangle(
                    offscreen_width, 0, alloc.width, offscreen_height)
            if offscreen_height < alloc.height:
                ctx.new_path()
                ctx.rectangle(0, offscreen_height, alloc.width, alloc.height)

            ctx.set_line_width(0.)
            ctx.set_source_rgb(1., 1., 1.)
            ctx.fill()

        ctx.set_source_surface(self.__offscreen)
        ctx.rectangle(rect.x, rect.y, rect.width, rect.height)
        ctx.paint()

        self.__draw_position(ctx, clip=rect)

    def update(self, model):

        self.clear()
        self.model = model

        if model is not None:
            self.__dist_sentinel_progress = 0
            self.process.freq_sentinel = LineFrequencySentinel(model)
            self.process.dist_sentinel = LevelDistributionSentinel(
                self.process.freq_sentinel, model)
            width = self.get_allocation().width
            self.process.freq_sentinel.run_for(width)
            self.process.run()

    def clear(self):

        self.model = None
        self.process.abort()
        self.process.freq_sentinel = None
        self.process.dist_sentinel = None
        self.__invalidate_offscreen(0, -1)

    def update_position(self, start_ts, end_ts):

        if not self.process.freq_sentinel:
            return

        if not self.process.freq_sentinel.data:
            return

        alloc = self.get_allocation()

        # Queue old position rectangle for redraw:
        if self.__position_ts_range is not None:
            start, stop = self.ts_range_to_position(*self.__position_ts_range)
            self.queue_draw_area(start - 1, 0, stop - start + 2, alloc.height)
        # And the new one:
        start, stop = self.ts_range_to_position(start_ts, end_ts)
        self.queue_draw_area(start - 1, 0, stop - start + 2, alloc.height)

        self.__position_ts_range = (start_ts, end_ts,)

    def find_indicative_time_step(self):

        MINIMUM_PIXEL_STEP = 32
        time_per_pixel = self.process.freq_sentinel.step
        return 32  # FIXME use self.freq_sentinel.step and len (self.process.freq_sentinel.data)

    def __draw_offscreen(self):

        dirty_start, dirty_stop = self.__offscreen_dirty
        if dirty_start == dirty_stop:
            return

        self.__offscreen_dirty = (0, 0)
        width, height = self.__offscreen_size

        ctx = cairo.Context(self.__offscreen)

        # Indicator (triangle) size is 8, so we need to draw surrounding areas
        # a bit:
        dirty_start -= 8
        dirty_stop += 8
        dirty_start = max(dirty_start, 0)
        dirty_stop = min(dirty_stop, width)

        ctx.rectangle(dirty_start, 0., dirty_stop, height)
        ctx.clip()

        # White background rectangle.
        ctx.set_line_width(0.)
        ctx.rectangle(0, 0, width, height)
        ctx.set_source_rgb(1., 1., 1.)
        ctx.fill()
        ctx.new_path()

        # Horizontal reference lines.
        ctx.set_line_width(1.)
        ctx.set_source_rgb(.95, .95, .95)
        for i in range(height // 16):
            y = i * 16 - .5
            ctx.move_to(0, y)
            ctx.line_to(width, y)
            ctx.stroke()

        if self.process.freq_sentinel is None:
            return

        # Vertical reference lines.
        pixel_step = self.find_indicative_time_step()
        ctx.set_source_rgb(.9, .9, .9)
        start = dirty_start - dirty_start % pixel_step
        for x in range(start + pixel_step, dirty_stop, pixel_step):
            ctx.move_to(x - .5, 0)
            ctx.line_to(x - .5, height)
            ctx.stroke()

        if not self.process.freq_sentinel.data:
            self.logger.debug("frequency sentinel has no data yet")
            return

        ctx.translate(dirty_start, 0.)

        maximum = max(self.process.freq_sentinel.data)

        ctx.set_source_rgb(0., 0., 0.)
        data = self.process.freq_sentinel.data[dirty_start:dirty_stop]
        self.__draw_graph(ctx, height, maximum, data)

        if not self.process.dist_sentinel.data:
            self.logger.debug("level distribution sentinel has no data yet")
            return

        colors = LevelColorThemeTango().colors
        dist_data = self.process.dist_sentinel.data[dirty_start:dirty_stop]

        def cumulative_level_counts(*levels):
            for level_counts in dist_data:
                yield sum((level_counts[level] for level in levels))

        level = Data.debug_level_info
        levels_prev = (Data.debug_level_trace,
                       Data.debug_level_fixme,
                       Data.debug_level_log,
                       Data.debug_level_debug,)
        ctx.set_source_rgb(*(colors[level][1].float_tuple()))
        self.__draw_graph(ctx, height, maximum,
                          list(cumulative_level_counts(level, *levels_prev)))

        level = Data.debug_level_debug
        levels_prev = (Data.debug_level_trace,
                       Data.debug_level_fixme,
                       Data.debug_level_log,)
        ctx.set_source_rgb(*(colors[level][1].float_tuple()))
        self.__draw_graph(ctx, height, maximum,
                          list(cumulative_level_counts(level, *levels_prev)))

        level = Data.debug_level_log
        levels_prev = (Data.debug_level_trace, Data.debug_level_fixme,)
        ctx.set_source_rgb(*(colors[level][1].float_tuple()))
        self.__draw_graph(ctx, height, maximum,
                          list(cumulative_level_counts(level, *levels_prev)))

        level = Data.debug_level_fixme
        levels_prev = (Data.debug_level_trace,)
        ctx.set_source_rgb(*(colors[level][1].float_tuple()))
        self.__draw_graph(ctx, height, maximum,
                          list(cumulative_level_counts(level, *levels_prev)))

        level = Data.debug_level_trace
        ctx.set_source_rgb(*(colors[level][1].float_tuple()))
        self.__draw_graph(ctx, height, maximum, [
                          counts[level] for counts in dist_data])

        # Draw error and warning triangle indicators:

        def triangle(ctx, size=8):
            ctx.move_to(-size // 2, 0)
            ctx.line_to((size + 1) // 2, 0)
            ctx.line_to(0, size / 1.41)
            ctx.close_path()

        for level in (Data.debug_level_warning, Data.debug_level_error,):
            ctx.set_source_rgb(*(colors[level][1].float_tuple()))
            for i, counts in enumerate(dist_data):
                if counts[level] == 0:
                    continue
                ctx.translate(i, 0.)
                triangle(ctx)
                ctx.fill()
                ctx.translate(-i, 0.)

    def __draw_graph(self, ctx, height, maximum, data):

        if not data:
            return

        if maximum:
            heights = [height * float(d) / maximum for d in data]
        else:
            heights = [0. for d in data]

        ctx.move_to(0, height)
        for i in range(len(heights)):
            ctx.line_to(i - .5, height - heights[i] + .5)

        ctx.line_to(i, height)
        ctx.close_path()

        ctx.fill()

    def __have_position(self):

        if ((self.process is not None) and
            (self.process.freq_sentinel is not None) and
                (self.process.freq_sentinel.ts_range is not None)):
            return True
        else:
            return False

    def ts_range_to_position(self, start_ts, end_ts):

        if not self.__have_position():
            return (0, 0)

        first_ts, last_ts = self.process.freq_sentinel.ts_range
        step = self.process.freq_sentinel.step
        if step == 0:
            return (0, 0)

        position1 = int(float(start_ts - first_ts) / step)
        position2 = int(float(end_ts - first_ts) / step)

        return (position1, position2)

    def __draw_position(self, ctx, clip=None):

        if not self.__have_position() or self.__position_ts_range is None:
            if not self.__have_position():
                self.logger.debug("have no positions")
            else:
                self.logger.debug("have no positions_ts_range")
            return

        start_ts, end_ts = self.__position_ts_range
        position1, position2 = self.ts_range_to_position(start_ts, end_ts)

        if clip:
            if clip.x + clip.width < position1 - 1 or clip.x > position2 + 1:
                self.logger.debug(
                    "outside of clip range: %d + %d, pos: %d, %d", clip.x, clip.width, position1, position2)
                return
            ctx.rectangle(clip.x, clip.y, clip.width, clip.height)
            ctx.clip()

        height = self.get_allocation().height

        line_width = position2 - position1
        if line_width <= 1:
            ctx.set_source_rgb(1., 0., 0.)
            ctx.set_line_width(1.)
            ctx.move_to(position1 + .5, 0)
            ctx.line_to(position1 + .5, height)
            ctx.stroke()
        else:
            ctx.set_source_rgba(1., 0., 0., .5)
            ctx.rectangle(position1, 0, line_width, height)
            ctx.fill()

    def do_draw(self, cr):

        self.__ensure_offscreen()
        self.__draw_offscreen()
        self.__draw_from_offscreen(cr)

        return True

    def do_configure_event(self, event):

        self.logger.debug("widget size configured to %ix%i",
                          event.width, event.height)

        if event.width < 16:
            return False

        self.update(self.model)

        return False

    def do_get_preferred_height(self):

        return 64, 64  # FIXME:

    def do_button_press_event(self, event):

        if event.button != 1:
            return False

        # TODO: Check if clicked inside a warning/error indicator triangle and
        # navigate there.

        if not self.has_grab():
            self.grab_add()
            self.props.has_tooltip = False

        pos = int(event.x)
        self.emit("change-position", pos)
        return True

    def do_button_release_event(self, event):

        if event.button != 1:
            return False

        if self.has_grab():
            self.grab_remove()
            self.props.has_tooltip = True

        return True

    def do_motion_notify_event(self, event):

        if event.get_state() & Gdk.ModifierType.BUTTON1_MASK:
            self.emit("change-position", int(event.x))
            Gdk.event_request_motions(event)
            return True
        else:
            self._handle_motion(event.x, event.y)
            Gdk.event_request_motions(event)
            return False

    def _handle_motion(self, x, y):

        # TODO: Prelight warning and error indicator triangles.

        pass


class AttachedWindow (object):

    def __init__(self, feature, window):

        self.window = window

        ui = window.ui_manager

        ui.insert_action_group(feature.action_group, 0)

        self.merge_id = ui.new_merge_id()
        ui.add_ui(self.merge_id, "/menubar/ViewMenu/ViewMenuAdditions",
                  "ViewTimeline", "show-timeline",
                  Gtk.UIManagerItemType.MENUITEM, False)

        ui.add_ui(self.merge_id, "/", "TimelineContextMenu", None,
                  Gtk.UIManagerItemType.POPUP, False)
        # TODO: Make hide before/after operate on the partition that the mouse
        # is pointed at instead of the currently selected line.
        # ui.add_ui (self.merge_id, "/TimelineContextMenu", "TimelineHideLinesBefore",
        #            "hide-before-line", Gtk.UIManagerItemType.MENUITEM, False)
        # ui.add_ui (self.merge_id, "/TimelineContextMenu", "TimelineHideLinesAfter",
        #            "hide-after-line", Gtk.UIManagerItemType.MENUITEM, False)
        ui.add_ui(
            self.merge_id, "/TimelineContextMenu", "TimelineShowHiddenLines",
            "show-hidden-lines", Gtk.UIManagerItemType.MENUITEM, False)

        box = window.get_top_attach_point()

        self.timeline = TimelineWidget()
        self.timeline.connect("change-position",
                              self.handle_timeline_change_position)
        box.pack_start(self.timeline, False, False, 0)
        self.timeline.hide()

        self.popup = ui.get_widget("/TimelineContextMenu")
        Common.GUI.widget_add_popup_menu(self.timeline, self.popup)

        box = window.get_side_attach_point()

        self.vtimeline = VerticalTimelineWidget(self.window.log_view)
        box.pack_start(self.vtimeline, False, False, 0)
        self.vtimeline.hide()

        handler = self.handle_log_view_adjustment_value_changed
        adjustment = window.widgets.log_view_scrolled_window.props.vadjustment
        adjustment.connect("value-changed", handler)

        handler = self.handle_show_action_toggled
        action = feature.action_group.get_action("show-timeline")
        action.connect("toggled", handler)
        handler(action)

        handler = self.handle_log_view_notify_model
        self.notify_model_id = window.log_view.connect(
            "notify::model", handler)

        self.idle_scroll_path = None
        self.idle_scroll_id = None

    def detach(self, feature):

        self.window.log_view.disconnect(self.notify_model_id)
        self.notify_model_id = None

        self.window.ui_manager.remove_ui(self.merge_id)
        self.merge_id = None

        self.window.ui_manager.remove_action_group(feature.action_group)

        self.timeline.destroy()
        self.timeline = None

        self.idle_scroll_path = None
        if self.idle_scroll_id is not None:
            GObject.source_remove(self.idle_scroll_id)
            self.idle_scroll_id = None

    def handle_detach_log_file(self, log_file):

        self.timeline.clear()
        self.vtimeline.clear()

    def handle_log_view_notify_model(self, view, gparam):

        model = view.get_model()

        if model is None:
            self.timeline.clear()
            self.vtimeline.clear()
            return

        self.timeline.update(model)

        # Need to dispatch these idly with a low priority to avoid triggering a
        # warning in treeview.get_visible_range:
        def idle_update():
            self.update_timeline_position()
            self.vtimeline.update()
            return False
        GObject.idle_add(idle_update, priority=GObject.PRIORITY_LOW)

    def handle_log_view_adjustment_value_changed(self, adj):

        # FIXME: If not visible, disconnect this handler!
        if not self.timeline.props.visible:
            return

        self.update_timeline_position()
        self.vtimeline.update()

    def update_timeline_position(self):

        visible_range = self.window.get_range()
        if visible_range is None:
            return
        ts1, ts2 = visible_range
        self.timeline.update_position(ts1, ts2)

    def handle_show_action_toggled(self, action):

        show = action.props.active

        if show:
            self.timeline.show()
            self.vtimeline.show()
        else:
            self.timeline.hide()
            self.vtimeline.hide()

    def handle_timeline_change_position(self, widget, pos):

        self.goto_time_position(pos)

    def goto_time_position(self, pos):

        if not self.timeline.process.freq_sentinel:
            return True

        data = self.timeline.process.freq_sentinel.data
        if not data:
            return True

        if pos < 0:
            pos = 0
        elif pos >= len(data):
            pos = len(data) - 1

        count = sum(data[:pos + 1])

        path = (count,)
        self.idle_scroll_path = path

        if self.idle_scroll_id is None:
            self.idle_scroll_id = GObject.idle_add(self.idle_scroll)

        return False

    def idle_scroll(self):

        self.idle_scroll_id = None

        if self.idle_scroll_path is None:
            return False

        path = self.idle_scroll_path
        self.idle_scroll_path = None

        view = self.window.log_view
        view.scroll_to_cell(path, use_align=True, row_align=.5)

        return False


class TimelineFeature (FeatureBase):

    def __init__(self, app):

        self.logger = logging.getLogger("ui.timeline")

        self.action_group = Gtk.ActionGroup("TimelineActions")
        self.action_group.add_toggle_actions([("show-timeline",
                                               None, _("_Timeline"),
                                               "<Ctrl>t")])

        self.state = app.state.sections[TimelineState._name]

        self.attached_windows = {}

        action = self.action_group.get_action("show-timeline")
        action.props.active = self.state.shown
        action.connect("toggled", self.handle_show_action_toggled)

    def handle_show_action_toggled(self, action):

        self.state.shown = action.props.active

    def handle_attach_window(self, window):

        self.attached_windows[window] = AttachedWindow(self, window)

    def handle_detach_window(self, window):

        attached_window = self.attached_windows.pop(window)
        attached_window.detach(self)

    def handle_attach_log_file(self, window, log_file):

        pass

    def handle_detach_log_file(self, window, log_file):

        attached_window = self.attached_windows[window]
        attached_window.handle_detach_log_file(log_file)


class TimelineState (Common.GUI.StateSection):

    _name = "timeline"

    shown = Common.GUI.StateBool("shown", default=True)


class Plugin (PluginBase):

    features = (TimelineFeature,)

    def __init__(self, app):

        app.state.add_section_class(TimelineState)
        self.state = app.state.sections[TimelineState._name]
