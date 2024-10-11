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

"""GStreamer Debug Viewer GUI module."""

import os.path
import tempfile
import shutil

from bisect import bisect_right, bisect_left
import logging

from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import Gdk
from gi.repository import GLib

from GstDebugViewer import Common, Data, Main
from GstDebugViewer.GUI.columns import LineViewColumnManager, ViewColumnManager
from GstDebugViewer.GUI.filters import (CategoryFilter,
                                        DebugLevelFilter,
                                        FilenameFilter,
                                        FunctionFilter,
                                        ThreadFilter,
                                        ObjectFilter)
from GstDebugViewer.GUI.models import (FilteredLogModel,
                                       LazyLogModel,
                                       LineViewLogModel,
                                       LogModelBase)


ZOOM_FACTOR = 1.15


def _(s):
    return s


def action(func):

    func.is_action_handler = True

    return func


def iter_actions(manager):

    cls = type(manager)
    it = cls.__dict__.items()
    for name, member in it:
        try:
            member.is_action_handler
        except AttributeError:
            continue

        bound_method = getattr(manager, name)

        assert name.startswith("handle_")
        assert name.endswith("_action_activate")
        action_name = name[len("handle_"):-len("_action_activate")]
        action_name = action_name.replace("_", "-")

        yield (action_name, bound_method,)


class LineView (object):

    def __init__(self):

        self.column_manager = LineViewColumnManager()

    def attach(self, window):

        for action_name, handler in iter_actions(self):
            action = getattr(window.actions, action_name)
            action.connect("activate", handler)

        self.clear_action = window.actions.clear_line_view

        self.line_view = window.widgets.line_view
        self.line_view.connect(
            "row-activated", self.handle_line_view_row_activated)

        ui = window.ui_manager
        self.popup = ui.get_widget(
            "/ui/context/LineViewContextMenu").get_submenu()
        Common.GUI.widget_add_popup_menu(self.line_view, self.popup)

        self.log_view = log_view = window.log_view
        log_view.connect("row-activated", self.handle_log_view_row_activated)
        sel = log_view.get_selection()
        sel.connect("changed", self.handle_log_view_selection_changed)

        self.clear_action.props.sensitive = False
        self.column_manager.attach(window)

    def clear(self):

        model = self.line_view.get_model()

        if len(model) == 0:
            return

        for i in range(1, len(model)):
            model.remove_line(1)

        self.clear_action.props.sensitive = False

    def handle_attach_log_file(self, window):

        self.line_view.set_model(LineViewLogModel(window.log_model))

    def handle_line_view_row_activated(self, view, path, column):

        line_index = path[0]
        line_model = view.get_model()
        log_model = self.log_view.get_model()
        super_index = line_model.line_index_to_super(line_index)
        log_index = log_model.line_index_from_super(super_index)
        path = (log_index,)
        self.log_view.scroll_to_cell(path, use_align=True, row_align=.5)
        sel = self.log_view.get_selection()
        sel.select_path(path)

    def handle_log_view_row_activated(self, view, path, column):

        log_model = view.get_model()
        line_index = path[0]

        super_index = log_model.line_index_to_super(line_index)
        line_model = self.line_view.get_model()
        if line_model is None:
            return

        if len(line_model):
            timestamps = [row[line_model.COL_TIME] for row in line_model]
            row = log_model[(line_index,)]
            position = bisect_right(timestamps, row[line_model.COL_TIME])
        else:
            position = 0
        if len(line_model) > 1:
            other_index = line_model.line_index_to_super(position - 1)
        else:
            other_index = -1
        if other_index == super_index and position != 1:
            # Already have the line.
            pass
        else:
            line_model.insert_line(position, super_index)
            self.clear_action.props.sensitive = True

    def handle_log_view_selection_changed(self, selection):

        line_model = self.line_view.get_model()
        if line_model is None:
            return

        model, tree_iter = selection.get_selected()

        if tree_iter is None:
            return

        path = model.get_path(tree_iter)
        line_index = model.line_index_to_super(path[0])

        if len(line_model) == 0:
            line_model.insert_line(0, line_index)
        else:
            line_model.replace_line(0, line_index)

    @action
    def handle_clear_line_view_action_activate(self, action):

        self.clear()


class ProgressDialog (object):

    def __init__(self, window, title=""):

        bar = Gtk.InfoBar()
        bar.props.message_type = Gtk.MessageType.INFO
        bar.connect("response", self.__handle_info_bar_response)
        bar.add_button(Gtk.STOCK_CANCEL, 1)
        area_box = bar.get_content_area()
        box = Gtk.HBox(spacing=8)

        box.pack_start(Gtk.Label(label=title), False, False, 0)

        progress = Gtk.ProgressBar()
        box.pack_start(progress, False, False, 0)

        area_box.pack_start(box, False, False, 0)

        self.widget = bar
        self.__progress_bar = progress

    def __handle_info_bar_response(self, info_bar, response):

        self.handle_cancel()

    def handle_cancel(self):

        pass

    def update(self, progress):

        if self.__progress_bar is None:
            return

        self.__progress_bar.props.fraction = progress


class Window (object):

    def __init__(self, app):

        self.logger = logging.getLogger("ui.window")
        self.app = app

        self.tmpfile = None
        self.dispatcher = None
        self.info_widget = None
        self.progress_dialog = None
        self.update_progress_id = None

        self.window_state = Common.GUI.WindowState()
        self.column_manager = ViewColumnManager(app.state_section)

        self.actions = Common.GUI.Actions()

        group = Gtk.ActionGroup("MenuActions")
        group.add_actions([("AppMenuAction", None, _("_Application")),
                           ("ViewMenuAction", None, _("_View")),
                           ("ViewColumnsMenuAction", None, _("_Columns")),
                           ("HelpMenuAction", None, _("_Help")),
                           ("LineViewContextMenuAction", None, "")])
        self.actions.add_group(group)

        group = Gtk.ActionGroup("WindowActions")
        group.add_actions(
            [("new-window", Gtk.STOCK_NEW, _("_New Window"), "<Ctrl>N"),
             ("open-file", Gtk.STOCK_OPEN, _(
              "_Open File"), "<Ctrl>O"),
             ("reload-file", Gtk.STOCK_REFRESH, _(
              "_Reload File"), "<Ctrl>R"),
             ("close-window", Gtk.STOCK_CLOSE, _(
              "Close _Window"), "<Ctrl>W"),
             ("cancel-load", Gtk.STOCK_CANCEL, None,),
             ("clear-line-view", Gtk.STOCK_CLEAR, None),
             ("show-about", None, _(
              "About GStreamer Debug Viewer",)),
             ("enlarge-text", Gtk.STOCK_ZOOM_IN, _(
              "Enlarge Text"), "<Ctrl>plus"),
             ("shrink-text", Gtk.STOCK_ZOOM_OUT, _(
              "Shrink Text"), "<Ctrl>minus"),
             ("reset-text", Gtk.STOCK_ZOOM_100, _("Normal Text Size"), "<Ctrl>0")])
        self.actions.add_group(group)
        self.actions.reload_file.props.sensitive = False

        group = Gtk.ActionGroup("RowActions")
        group.add_actions(
            [("hide-before-line", None, _("Hide lines before this point")),
             ("hide-after-line", None, _(
              "Hide lines after this point")),
             ("show-hidden-lines", None, _(
              "Show hidden lines")),
             ("edit-copy-line", Gtk.STOCK_COPY, _(
              "Copy line"), "<Ctrl>C"),
             ("edit-copy-message", Gtk.STOCK_COPY, _(
              "Copy message"), ""),
             ("set-base-time", None, _("Set base time")),
             ("hide-log-level", None, _("Hide log level")),
             ("hide-log-level-and-above", None, _(
              "Hide this log level and above")),
             ("show-only-log-level", None, _(
              "Show only log level")),
             ("hide-log-category", None, _(
              "Hide log category")),
             ("show-only-log-category", None, _(
              "Show only log category")),
             ("hide-thread", None, _(
              "Hide thread")),
             ("show-only-thread", None, _(
              "Show only thread")),
             ("hide-object", None, _("Hide object")),
             ("show-only-object", None, _(
              "Show only object")),
             ("hide-function", None, _("Hide function")),
             ("show-only-function", None, _(
              "Show only function")),
             ("hide-filename", None, _("Hide filename")),
             ("show-only-filename", None, _("Show only filename"))])
        group.props.sensitive = False
        self.actions.add_group(group)

        self.actions.add_group(self.column_manager.action_group)

        self.log_file = None
        self.log_model = None
        self.log_filter = None

        self.widget_factory = Common.GUI.WidgetFactory(Main.Paths.data_dir)
        self.widgets = self.widget_factory.make("main-window.ui", "main_window")

        ui_filename = os.path.join(Main.Paths.data_dir, "menus.ui")
        self.ui_factory = Common.GUI.UIFactory(ui_filename, self.actions)

        self.ui_manager = ui = self.ui_factory.make()
        menubar = ui.get_widget("/ui/menubar")
        self.widgets.vbox_main.pack_start(menubar, False, False, 0)

        self.gtk_window = self.widgets.main_window
        self.gtk_window.add_accel_group(ui.get_accel_group())
        self.log_view = self.widgets.log_view
        self.log_view.drag_dest_unset()
        self.log_view.set_search_column(-1)
        sel = self.log_view.get_selection()
        sel.connect("changed", self.handle_log_view_selection_changed)

        self.view_popup = ui.get_widget(
            "/ui/context/LogViewContextMenu").get_submenu()
        Common.GUI.widget_add_popup_menu(self.log_view, self.view_popup)

        # Widgets to set insensitive when the window is considered as
        # such. This is done during loading/filtering, where we can't set the
        # whole window insensitive because the progress info bar should be
        # usable to allow cancellation.
        self.main_sensitivity = [menubar]
        self.main_sensitivity.extend(self.widgets.vbox_main.get_children())

        self.line_view = LineView()

        self.attach()
        self.column_manager.attach(self.log_view)

    def setup_model(self, model):

        self.log_model = model
        self.log_filter = FilteredLogModel(self.log_model)
        self.log_filter.handle_process_finished = self.handle_log_filter_process_finished

    def get_top_attach_point(self):

        return self.widgets.vbox_main

    def get_side_attach_point(self):

        return self.widgets.hbox_view

    def attach(self):

        self.zoom_level = 0
        zoom_percent = self.app.state_section.zoom_level
        if zoom_percent:
            self.restore_zoom(float(zoom_percent) / 100.)

        self.window_state.attach(window=self.gtk_window,
                                 state=self.app.state_section)

        self.clipboard = Gtk.Clipboard.get_for_display(
            self.gtk_window.get_display(),
            Gdk.SELECTION_CLIPBOARD)

        for action_name, handler in iter_actions(self):
            action = getattr(self.actions, action_name)
            action.connect("activate", handler)

        self.gtk_window.connect(
            "delete-event", self.handle_window_delete_event)

        self.features = []

        for plugin_feature in self.app.iter_plugin_features():
            feature = plugin_feature(self.app)
            self.features.append(feature)

        for feature in self.features:
            feature.handle_attach_window(self)

        # FIXME: With multiple selection mode, browsing the list with key
        # up/down slows to a crawl! WTF is wrong with this stupid widget???
        sel = self.log_view.get_selection()
        sel.set_mode(Gtk.SelectionMode.BROWSE)

        self.line_view.attach(self)

        # Do not translate; fallback application name for e.g. gnome-shell if
        # the desktop file is not installed:
        self.gtk_window.set_wmclass(
            "gst-debug-viewer", "GStreamer Debug Viewer")

        self.gtk_window.show()

    def detach(self):

        self.set_log_file(None)
        for feature in self.features:
            feature.handle_detach_window(self)

        self.window_state.detach()
        self.column_manager.detach()

    def get_active_line_index(self):

        selection = self.log_view.get_selection()
        model, tree_iter = selection.get_selected()
        if tree_iter is None:
            raise ValueError("no line selected")
        path = model.get_path(tree_iter)
        return path[0]

    def get_active_line(self):

        selection = self.log_view.get_selection()
        model, tree_iter = selection.get_selected()
        if tree_iter is None:
            raise ValueError("no line selected")
        model = self.log_view.get_model()
        return model.get(tree_iter, *LogModelBase.column_ids)

    def close(self, *a, **kw):

        self.logger.debug("closing window, detaching")
        self.detach()
        self.gtk_window.hide()
        self.logger.debug("requesting close from app")
        self.app.close_window(self)

    def push_view_state(self):

        self.default_index = None
        self.default_start_index = None

        model = self.log_view.get_model()
        if model is None:
            return

        try:
            line_index = self.get_active_line_index()
        except ValueError:
            super_index = None
            self.logger.debug("no line selected")
        else:
            super_index = model.line_index_to_super(line_index)
            self.logger.debug("pushing selected line %i (abs %i)",
                              line_index, super_index)

        self.default_index = super_index

        vis_range = self.log_view.get_visible_range()
        if vis_range is not None:
            start_path, end_path = vis_range
            start_index = start_path[0]
            self.default_start_index = model.line_index_to_super(start_index)

    def update_model(self, model=None):

        if model is None:
            model = self.log_view.get_model()

        previous_model = self.log_view.get_model()

        if previous_model == model:
            # Force update.
            self.log_view.set_model(None)
        self.log_view.set_model(model)

    def pop_view_state(self, scroll_to_selection=False):

        model = self.log_view.get_model()
        if model is None:
            return

        selected_index = self.default_index
        start_index = self.default_start_index

        if selected_index is not None:

            try:
                select_index = model.line_index_from_super(selected_index)
            except IndexError as exc:
                self.logger.debug(
                    "abs line index %i filtered out, not reselecting",
                    selected_index)
            else:
                assert select_index >= 0
                sel = self.log_view.get_selection()
                path = (select_index,)
                sel.select_path(path)

                if start_index is None or scroll_to_selection:
                    self.log_view.scroll_to_cell(
                        path, use_align=True, row_align=.5)

        if start_index is not None and not scroll_to_selection:

            def traverse():
                for i in range(start_index, len(model)):
                    yield i
                for i in range(start_index - 1, 0, -1):
                    yield i
            for current_index in traverse():
                try:
                    target_index = model.line_index_from_super(current_index)
                except IndexError:
                    continue
                else:
                    path = (target_index,)
                    self.log_view.scroll_to_cell(
                        path, use_align=True, row_align=0.)
                    break

    def update_view(self):

        view = self.log_view
        model = view.get_model()

        visible_range = view.get_visible_range()
        if visible_range is None:
            return
        start_path, end_path = visible_range
        start_index, end_index = start_path[0], end_path[0]

        for line_index in range(start_index, end_index + 1):
            path = (line_index,)
            tree_iter = model.get_iter(path)
            model.row_changed(path, tree_iter)

    def handle_log_view_selection_changed(self, selection):

        try:
            line_index = self.get_active_line_index()
        except ValueError:
            first_selected = True
            last_selected = True
        else:
            first_selected = (line_index == 0)
            last_selected = (
                line_index == len(self.log_view.get_model()) - 1)

        self.actions.hide_before_line.props.sensitive = not first_selected
        self.actions.hide_after_line.props.sensitive = not last_selected

    def handle_window_delete_event(self, window, event):

        self.actions.close_window.activate()

        return True

    @action
    def handle_new_window_action_activate(self, action):

        self.app.open_window()

    @action
    def handle_open_file_action_activate(self, action):

        dialog = Gtk.FileChooserNative.new(None, self.gtk_window,
                                       Gtk.FileChooserAction.OPEN, None, None)
        response = dialog.run()
        dialog.hide()
        if response == Gtk.ResponseType.ACCEPT:
            self.set_log_file(dialog.get_filename())
        dialog.destroy()

    @action
    def handle_reload_file_action_activate(self, action):

        if self.log_file is None:
            return

        self.set_log_file(self.log_file.path)

    @action
    def handle_cancel_load_action_activate(self, action):

        self.logger.debug("cancelling data load")

        self.set_log_file(None)

        if self.progress_dialog is not None:
            self.hide_info()
            self.progress_dialog = None
        if self.update_progress_id is not None:
            GObject.source_remove(self.update_progress_id)
            self.update_progress_id = None

        self.set_sensitive(True)

    @action
    def handle_close_window_action_activate(self, action):

        self.close()

    @action
    def handle_hide_after_line_action_activate(self, action):

        self.hide_range(after=True)

    @action
    def handle_hide_before_line_action_activate(self, action):

        self.hide_range(after=False)

    def hide_range(self, after):

        model = self.log_view.get_model()
        try:
            filtered_line_index = self.get_active_line_index()
        except ValueError:
            return

        if after:
            first_index = model.line_index_to_super(0)
            last_index = model.line_index_to_super(filtered_line_index)

            self.logger.info(
                "hiding lines after %i (abs %i), first line is abs %i",
                filtered_line_index,
                last_index,
                first_index)
        else:
            first_index = model.line_index_to_super(filtered_line_index)
            last_index = model.line_index_to_super(len(model) - 1)

            self.logger.info(
                "hiding lines before %i (abs %i), last line is abs %i",
                filtered_line_index,
                first_index,
                last_index)

        self.push_view_state()
        start_index = first_index
        stop_index = last_index + 1
        self.log_filter.set_range(start_index, stop_index)
        self.update_model()
        self.pop_view_state()
        self.actions.show_hidden_lines.props.sensitive = True

    def get_range(self):

        view = self.log_view
        model = view.get_model()
        visible_range = view.get_visible_range()
        if visible_range is None:
            return None
        start_path, end_path = visible_range
        if not start_path or not end_path:
            return None
        ts1 = model.get_value(model.get_iter(start_path),
                              model.COL_TIME)
        ts2 = model.get_value(model.get_iter(end_path),
                              model.COL_TIME)
        return (ts1, ts2)

    @action
    def handle_show_hidden_lines_action_activate(self, action):

        self.logger.info("restoring model filter to show all lines")
        self.push_view_state()
        self.log_view.set_model(None)
        self.log_filter.reset()
        self.update_model(self.log_filter)
        self.pop_view_state(scroll_to_selection=True)
        self.actions.show_hidden_lines.props.sensitive = False

    @action
    def handle_edit_copy_line_action_activate(self, action):

        line_index = self.get_active_line_index()
        model = self.log_view.get_model()
        line_offset = model.line_offsets[line_index]

        line_text = model.access_offset(line_offset).strip()
        line_text = Data.strip_escape(line_text)

        self.clipboard.set_text(line_text.decode('utf8', errors='replace'), -1)

    @action
    def handle_edit_copy_message_action_activate(self, action):

        col_id = LogModelBase.COL_MESSAGE
        self.clipboard.set_text(self.get_active_line()[
                                col_id].decode('utf8'), -1)

    @action
    def handle_enlarge_text_action_activate(self, action):

        self.update_zoom_level(1)

    @action
    def handle_shrink_text_action_activate(self, action):

        self.update_zoom_level(-1)

    @action
    def handle_reset_text_action_activate(self, action):

        self.update_zoom_level(-self.zoom_level)

    def restore_zoom(self, scale):

        from math import log

        self.zoom_level = int(round(log(scale) / log(ZOOM_FACTOR)))

        self.column_manager.set_zoom(scale)

    def update_zoom_level(self, delta_step):

        if not delta_step:
            return

        self.zoom_level += delta_step
        scale = ZOOM_FACTOR ** self.zoom_level

        self.column_manager.set_zoom(scale)

        self.app.state_section.zoom_level = int(round(scale * 100.))

    def set_sensitive(self, sensitive):

        for widget in self.main_sensitivity:
            widget.props.sensitive = sensitive

    def show_info(self, widget):

        self.hide_info()

        box = self.widgets.vbox_main
        box.pack_start(widget, False, False, 0)
        box.reorder_child(widget, 2)
        widget.show_all()
        self.info_widget = widget

    def hide_info(self):

        if self.info_widget is None:
            return

        self.info_widget.destroy()
        self.info_widget = None

    def add_model_filter(self, filter):

        self.progress_dialog = ProgressDialog(self, _("Filtering"))
        self.show_info(self.progress_dialog.widget)
        self.progress_dialog.handle_cancel = self.handle_filter_progress_dialog_cancel
        dispatcher = Common.Data.GSourceDispatcher()

        # FIXME: Unsetting the model to keep e.g. the dispatched timeline
        # sentinel from collecting data while we filter idly, which slows
        # things down for nothing.
        self.push_view_state()
        self.log_view.set_model(None)
        self.log_filter.add_filter(filter, dispatcher=dispatcher)

        GObject.timeout_add(250, self.update_filter_progress)

        self.set_sensitive(False)

    def update_filter_progress(self):

        if self.progress_dialog is None:
            return False

        try:
            progress = self.log_filter.get_filter_progress()
        except ValueError:
            self.logger.warning("no filter process running")
            return False

        self.progress_dialog.update(progress)

        return True

    def handle_filter_progress_dialog_cancel(self):

        self.hide_info()
        self.progress_dialog = None

        self.log_filter.abort_process()
        self.log_view.set_model(self.log_filter)
        self.pop_view_state()

        self.set_sensitive(True)

    def handle_log_filter_process_finished(self):

        self.hide_info()
        self.progress_dialog = None

        # No push_view_state here, did this in add_model_filter.
        self.update_model(self.log_filter)
        self.pop_view_state()

        self.actions.show_hidden_lines.props.sensitive = True

        self.set_sensitive(True)

    @action
    def handle_set_base_time_action_activate(self, action):

        row = self.get_active_line()
        self.column_manager.set_base_time(row[LogModelBase.COL_TIME])

    @action
    def handle_hide_log_level_action_activate(self, action):

        row = self.get_active_line()
        debug_level = row[LogModelBase.COL_LEVEL]
        self.add_model_filter(DebugLevelFilter(debug_level))

    @action
    def handle_hide_log_category_action_activate(self, action):

        row = self.get_active_line()
        category = row[LogModelBase.COL_CATEGORY]
        self.add_model_filter(CategoryFilter(category))

    @action
    def handle_hide_thread_action_activate(self, action):

        row = self.get_active_line()
        thread = row[LogModelBase.COL_THREAD]
        self.add_model_filter(ThreadFilter(thread))

    @action
    def handle_hide_object_action_activate(self, action):

        row = self.get_active_line()
        object_ = row[LogModelBase.COL_OBJECT]
        self.add_model_filter(ObjectFilter(object_))

    @action
    def handle_hide_function_action_activate(self, action):

        row = self.get_active_line()
        object_ = row[LogModelBase.COL_FUNCTION]
        self.add_model_filter(FunctionFilter(object_))

    @action
    def handle_hide_filename_action_activate(self, action):

        row = self.get_active_line()
        filename = row[LogModelBase.COL_FILENAME]
        self.add_model_filter(FilenameFilter(filename))

    @action
    def handle_hide_log_level_and_above_action_activate(self, action):

        row = self.get_active_line()
        debug_level = row[LogModelBase.COL_LEVEL]
        self.add_model_filter(
            DebugLevelFilter(debug_level, DebugLevelFilter.this_and_above))

    @action
    def handle_show_only_log_level_action_activate(self, action):

        row = self.get_active_line()
        debug_level = row[LogModelBase.COL_LEVEL]
        self.add_model_filter(
            DebugLevelFilter(debug_level, DebugLevelFilter.all_but_this))

    @action
    def handle_show_only_log_category_action_activate(self, action):

        row = self.get_active_line()
        category = row[LogModelBase.COL_CATEGORY]
        self.add_model_filter(CategoryFilter(category, True))

    @action
    def handle_show_only_thread_action_activate(self, action):

        row = self.get_active_line()
        thread = row[LogModelBase.COL_THREAD]
        self.add_model_filter(ThreadFilter(thread, True))

    @action
    def handle_show_only_object_action_activate(self, action):

        row = self.get_active_line()
        object_ = row[LogModelBase.COL_OBJECT]
        self.add_model_filter(ObjectFilter(object_, True))

    @action
    def handle_show_only_function_action_activate(self, action):

        row = self.get_active_line()
        object_ = row[LogModelBase.COL_FUNCTION]
        self.add_model_filter(FunctionFilter(object_, True))

    @action
    def handle_show_only_filename_action_activate(self, action):

        row = self.get_active_line()
        filename = row[LogModelBase.COL_FILENAME]
        self.add_model_filter(FilenameFilter(filename, True))

    @action
    def handle_show_about_action_activate(self, action):

        from GstDebugViewer import version

        dialog = self.widget_factory.make_one(
            "about-dialog.ui", "about_dialog")
        dialog.props.version = version
        dialog.run()
        dialog.destroy()

    @staticmethod
    def _timestamp_cell_data_func(column, renderer, model, tree_iter):

        ts = model.get_value(tree_iter, LogModel.COL_TIME)
        renderer.props.text = Data.time_args(ts)

    def _message_cell_data_func(self, column, renderer, model, tree_iter):

        offset = model.get_value(tree_iter, LogModel.COL_MESSAGE_OFFSET)
        self.log_file.seek(offset)
        renderer.props.text = strip_escape(self.log_file.readline().strip())

    def set_log_file(self, filename):

        if self.log_file is not None:
            for feature in self.features:
                feature.handle_detach_log_file(self, self.log_file)

        if filename is None:
            if self.dispatcher is not None:
                self.dispatcher.cancel()
            self.dispatcher = None
            self.log_file = None
            self.tmpfile = None
            self.actions.groups["RowActions"].props.sensitive = False
        else:
            if self.tmpfile and filename != self.tmpfile.name:
                self.tmpfile = tempfile.NamedTemporaryFile()
                shutil.copyfile(filename, self.tmpfile.name)
                filename = self.tmpfile.name
            self.logger.debug("setting log file %r", filename)

            try:
                self.setup_model(LazyLogModel())

                self.dispatcher = Common.Data.GSourceDispatcher()
                self.log_file = Data.LogFile(filename, self.dispatcher)
            except EnvironmentError as exc:
                try:
                    file_size = os.path.getsize(filename)
                except EnvironmentError:
                    pass
                else:
                    if file_size == 0:
                        # Trying to mmap an empty file results in an invalid
                        # argument error.
                        self.show_error(_("Could not open file"),
                                        _("The selected file is empty"))
                        return
                self.handle_environment_error(exc, filename)
                return

            basename = os.path.basename(filename)
            self.gtk_window.props.title = _(
                "%s - GStreamer Debug Viewer") % (basename,)

            self.log_file.consumers.append(self)
            self.log_file.start_loading()

    def handle_environment_error(self, exc, filename):

        self.show_error(_("Could not open file"), str(exc))

    def show_error(self, message1, message2):

        bar = Gtk.InfoBar()
        bar.props.message_type = Gtk.MessageType.ERROR
        box = bar.get_content_area()

        markup = "<b>%s</b> %s" % (GLib.markup_escape_text(message1),
                                   GLib.markup_escape_text(message2),)
        label = Gtk.Label()
        label.props.use_markup = True
        label.props.label = markup
        label.props.selectable = True
        box.pack_start(label, False, False, 0)

        self.show_info(bar)

    def handle_load_started(self):

        self.logger.debug("load has started")

        self.progress_dialog = ProgressDialog(self, _("Loading log file"))
        self.show_info(self.progress_dialog.widget)
        self.progress_dialog.handle_cancel = self.handle_load_progress_dialog_cancel
        self.update_progress_id = GObject.timeout_add(
            250, self.update_load_progress)

        self.set_sensitive(False)

    def handle_load_progress_dialog_cancel(self):

        self.actions.cancel_load.activate()

    def update_load_progress(self):

        if self.progress_dialog is None:
            self.logger.debug(
                "progress dialog is gone, removing progress update timeout")
            self.update_progress_id = None
            return False

        progress = self.log_file.get_load_progress()
        self.progress_dialog.update(progress)

        return True

    def handle_load_finished(self):

        self.logger.debug("load has finshed")

        self.hide_info()
        self.progress_dialog = None

        self.log_model.set_log(self.log_file)
        self.log_filter.reset()

        self.actions.reload_file.props.sensitive = True
        self.actions.groups["RowActions"].props.sensitive = True
        self.actions.show_hidden_lines.props.sensitive = False

        self.set_sensitive(True)

        if len(self.log_model) == 0:
            self.show_error(
                _("The file does not contain any parsable lines."),
                _("It is not a GStreamer log file."))

        def idle_set():
            self.logger.debug("idle trigger after load finished")
            self.log_view.set_model(self.log_filter)

            self.line_view.handle_attach_log_file(self)
            for feature in self.features:
                feature.handle_attach_log_file(self, self.log_file)
            if len(self.log_filter):
                sel = self.log_view.get_selection()
                sel.select_path((0,))
            return False

        GObject.idle_add(idle_set)
