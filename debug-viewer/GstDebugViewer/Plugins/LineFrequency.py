
import logging

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

    def _search_ts (self, target_ts, first_index, last_index):

        model_get = self.model.get
        model_iter_nth_child = self.model.iter_nth_child
        col_id = self.model.COL_TIME

        while True:
            middle = (last_index - first_index) // 2 + first_index
            if middle == first_index:
                return last_index
            ts = model_get (model_iter_nth_child (None, middle), col_id)[0]
            if ts < target_ts:
                first_index = middle + 1
            elif ts > target_ts:
                last_index = middle - 1
            else:
                return middle

    def run_for (self, n):

        if n == 0:
            raise ValueError ("illegal value for n")

        model = self.model
        result = []

        last_ts = None
        for row in iter_model_reversed (self.model):
            last_ts = row[model.COL_TIME]
            if last_ts:
                last_index = row.path[0]
                break

        if last_ts is None:
            return result

        step = int (float (last_ts) / float (n))

        first_index = 0
        target_ts = step
        old_found = 0
        while target_ts < last_ts:
            found = self._search_ts (target_ts, first_index, last_index)
            result.append (found - old_found)
            old_found = found
            first_index = found
            target_ts += step
        
        ## count = 0
        ## limit = step
        ## for row in self.model:
        ##     ts = row[model.COL_TIME]
        ##     if ts is None:
        ##         continue
        ##     if ts > limit:
        ##         limit += step
        ##         result.append (count)
        ##         count = 0
        ##     count += 1

        return (step, result,)

class LineFrequencyWidget (gtk.DrawingArea):

    __gtype_name__ = "LineFrequencyWidget"

    def __init__ (self, sentinel = None):

        gtk.DrawingArea.__init__ (self)

        self.logger = logging.getLogger ("ui.density-widget")

        self.sentinel = sentinel
        self.sentinel_step = None
        self.sentinel_data = None
        self.__configure_id = None
        self.connect ("expose-event", self.__handle_expose_event)
        self.connect ("configure-event", self.__handle_configure_event)
        self.connect ("size-request", self.__handle_size_request)

    def set_sentinel (self, sentinel):

        self.sentinel = sentinel
        self.__redraw ()

    def __redraw (self):

        if not self.props.visible:
            return

        x, y, w, h = self.get_allocation ()
        self.offscreen = gtk.gdk.Pixmap (self.window, w, h, -1)

        self.__draw (self.offscreen)

        self.__update ()

    def __update (self):

        if not self.props.visible:
            return

        gc = gtk.gdk.GC (self.window)
        self.window.draw_drawable (gc, self.offscreen, 0, 0, 0, 0, -1, -1)

    def update_position (self, start_ts, end_ts):

        self.__update ()

        position1 = int (float (start_ts) / self.sentinel_step)
        position2 = int (float (end_ts) / self.sentinel_step)

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
        time_per_pixel = self.sentinel_step
        return 32 # FIXME use self.sentinel_step and len (self.sentinel_data)

    def __draw (self, drawable):

        ctx = drawable.cairo_create ()
        x, y, w, h = self.get_allocation ()
        ctx.set_line_width (0.)
        ctx.rectangle (0, 0, w, h)
        ctx.set_source_rgb (1., 1., 1.)
        ctx.fill ()
        ctx.new_path ()

        ctx.set_line_width (1.)
        ctx.set_source_rgb (.95, .95, .95)
        for i in range (h // 16):
            y = i * 16 - .5
            ctx.move_to (0, y)
            ctx.line_to (w, y)
            ctx.stroke ()

        pixel_step = self.find_indicative_time_step ()
        ctx.set_source_rgb (.9, .9, .9)
        for i in range (w // pixel_step):
            x = i * pixel_step - .5
            ctx.move_to (x, 0)
            ctx.line_to (x, h)
            ctx.stroke ()

        if self.sentinel_data is None and self.sentinel:
            if w > 15:
                self.logger.debug ("running sentinel for width %i", w)
                self.sentinel_step, self.sentinel_data = self.sentinel.run_for (w)
            else:
                return

        if self.sentinel_data is None:
            self.logger.debug ("not redrawing: no sentinel set")
            return

        from operator import add
        maximum = max (self.sentinel_data)
        heights = [h * float (d) / maximum for d in self.sentinel_data]
        ctx.move_to (0, h)
        ctx.set_source_rgb (0., 0., 0.)
        for i in range (len (heights)):
            ctx.line_to (i - .5, h - heights[i] + .5)
            #ctx.rectangle (i - .5, h - heights[i] + .5, i + 1, h)

        ctx.line_to (i, h)
        ctx.close_path ()
        
        ctx.fill ()

    def __handle_expose_event (self, self_, event):

        self.__redraw ()
        return True

    def __handle_configure_event (self, self_, event):

        if event.width < 16:
            return

        if self.sentinel:
            self.sentinel_step, self.sentinel_data = self.sentinel.run_for (event.width)

        # FIXME: Is this done automatically?
        self.queue_draw ()
        return False

    def __handle_size_request (self, self_, req):

        # FIXME:
        req.height = 64

class LineFrequencyFeature (FeatureBase):

    state_section_name = "line-frequency-display"

    def __init__ (self):

        self.action_group = gtk.ActionGroup ("LineFrequencyActions")
        self.action_group.add_toggle_actions ([("show-line-frequency",
                                                None, _("Line _Density"))])

    def attach (self, window):

        self.log_model = window.log_model
        self.log_view = window.log_view

        ui = window.ui_manager

        ui.insert_action_group (self.action_group, 0)
        
        self.merge_id = ui.new_merge_id ()
        ui.add_ui (self.merge_id, "/menubar/ViewMenu/ViewMenuAdditions",
                   "ViewLineFrequency", "show-line-frequency",
                   gtk.UI_MANAGER_MENUITEM, False)

        box = window.get_top_attach_point ()

        self.density_display = LineFrequencyWidget ()
        self.density_display.add_events (gtk.gdk.ALL_EVENTS_MASK) # FIXME
        self.density_display.connect ("button-press-event", self.handle_density_button_press_event)
        self.density_display.connect ("motion-notify-event", self.handle_density_motion_notify_event)
        box.pack_start (self.density_display, False, False, 0)
        self.density_display.hide ()

        window.widgets.log_view_scrolled_window.props.vadjustment.connect ("value-changed",
                                                                           self.handle_log_view_adjustment_value_changed)

        handler = self.handle_show_action_toggled
        self.action_group.get_action ("show-line-frequency").connect ("toggled", handler)

        window.sentinels.append (self.sentinel_process)

    def detach (self, window):

        window.sentinels.remove (self.sentinel_process)

        window.ui_manager.remove_ui (self.merge_id)
        self.merge_id = None

        # FIXME: Remove action group from ui manager!

        self.density_display.destroy ()
        self.density_display = None

    def sentinel_process (self):

        if self.action_group.get_action ("show-line-frequency").props.active:
            sentinel = LineDensitySentinel (self.log_model)
            self.density_display.set_sentinel (sentinel)

    def handle_log_view_adjustment_value_changed (self, adj):

        # FIXME: If not visible, disconnect this handler!
        if not self.density_display.props.visible:
            return

        start_path, end_path = self.log_view.get_visible_range ()
        ts1 = self.log_model.get (self.log_model.get_iter (start_path),
                                  self.log_model.COL_TIME)[0]
        ts2 = self.log_model.get (self.log_model.get_iter (end_path),
                                  self.log_model.COL_TIME)[0]
        self.density_display.update_position (ts1, ts2)

    def handle_show_action_toggled (self, action):

        show = action.props.active

        if show:
            self.density_display.show ()
            if self.density_display.sentinel is None:
                sentinel = LineFrequencySentinel (self.log_model)
                self.density_display.set_sentinel (sentinel)
        else:
            self.density_display.hide ()

    def handle_density_button_press_event (self, widget, event):

        if event.button != 1:
            return True

        pos = int (event.x)
        self.goto_density (pos)
        return False

    def handle_density_motion_notify_event (self, widget, event):

        if not event.state & gtk.gdk.BUTTON1_MASK:
            return True

        pos = int (event.x)
        self.goto_density (pos)
        return False

    def goto_density (self, pos):

        data = self.density_display.sentinel_data
        if not data:
            return True
        count = 0
        for i in range (pos):
            count += data[i]

        row = self.log_model[count]
        self.log_view.scroll_to_cell ((count,), use_align = True, row_align = .5)
        
        return False

class Plugin (PluginBase):

    features = [LineFrequencyFeature]
