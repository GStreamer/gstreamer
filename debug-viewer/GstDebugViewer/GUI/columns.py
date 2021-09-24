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

import logging

from gi.repository import Gtk, GLib

from GstDebugViewer import Common, Data
from GstDebugViewer.GUI.colors import LevelColorThemeTango
from GstDebugViewer.GUI.models import LazyLogModel, LogModelBase


def _(s):
    return s

# Sync with gst-inspector!


class Column (object):

    """A single list view column, managed by a ColumnManager instance."""

    name = None
    id = None
    label_header = None
    get_modify_func = None
    get_data_func = None
    get_sort_func = None

    def __init__(self):

        view_column = Gtk.TreeViewColumn(self.label_header)
        view_column.props.reorderable = True

        self.view_column = view_column


class SizedColumn (Column):

    default_size = None

    def compute_default_size(self):

        return None

# Sync with gst-inspector?


class TextColumn (SizedColumn):

    font_family = None

    def __init__(self):

        Column.__init__(self)

        column = self.view_column
        cell = Gtk.CellRendererText()
        column.pack_start(cell, True)

        cell.props.yalign = 0.
        cell.props.ypad = 0

        if self.font_family:
            cell.props.family = self.font_family
            cell.props.family_set = True

        if self.get_data_func:
            data_func = self.get_data_func()
            assert data_func
            id_ = self.id
            if id_ is not None:
                def cell_data_func(column, cell, model, tree_iter, user_data):
                    data_func(cell.props, model.get_value(tree_iter, id_))
            else:
                cell_data_func = data_func
            column.set_cell_data_func(cell, cell_data_func)
        elif not self.get_modify_func:
            column.add_attribute(cell, "text", self.id)
        else:
            self.update_modify_func(column, cell)

        column.props.resizable = True

    def update_modify_func(self, column, cell):

        modify_func = self.get_modify_func()
        id_ = self.id

        def cell_data_func(column, cell, model, tree_iter, user_data):
            cell.props.text = modify_func(model.get_value(tree_iter, id_))
        column.set_cell_data_func(cell, cell_data_func)

    def compute_default_size(self):

        values = self.get_values_for_size()
        if not values:
            return SizedColumn.compute_default_size(self)

        cell = self.view_column.get_cells()[0]

        if self.get_modify_func is not None:
            format = self.get_modify_func()
        else:
            def identity(x):
                return x
            format = identity
        max_width = 0
        for value in values:
            cell.props.text = format(value)
            x, y, w, h = self.view_column.cell_get_size()
            max_width = max(max_width, w)

        return max_width

    def get_values_for_size(self):

        return ()


class TimeColumn (TextColumn):

    name = "time"
    label_header = _("Time")
    id = LazyLogModel.COL_TIME
    font_family = "monospace"

    def __init__(self, *a, **kw):

        self.base_time = 0

        TextColumn.__init__(self, *a, **kw)

    def get_modify_func(self):

        if self.base_time:
            time_diff_args = Data.time_diff_args
            base_time = self.base_time

            def format_time(value):
                return time_diff_args(value - base_time)
        else:
            time_args = Data.time_args

            def format_time(value):
                # TODO: This is hard coded to omit hours.
                return time_args(value)[2:]

        return format_time

    def get_values_for_size(self):

        values = [0]

        return values

    def set_base_time(self, base_time):

        self.base_time = base_time

        column = self.view_column
        cell = column.get_cells()[0]
        self.update_modify_func(column, cell)


class LevelColumn (TextColumn):

    name = "level"
    label_header = _("L")
    id = LazyLogModel.COL_LEVEL

    def __init__(self):

        TextColumn.__init__(self)

        cell = self.view_column.get_cells()[0]
        cell.props.xalign = .5

    @staticmethod
    def get_modify_func():

        def format_level(value):
            return value.name[0]

        return format_level

    @staticmethod
    def get_data_func():

        theme = LevelColorThemeTango()
        colors = dict((level, tuple((c.gdk_color()
                                     for c in theme.colors[level])),)
                      for level in Data.debug_levels
                      if level != Data.debug_level_none)

        def level_data_func(cell_props, level):
            cell_props.text = level.name[0]
            if level in colors:
                cell_colors = colors[level]
            else:
                cell_colors = (None, None, None,)
            cell_props.foreground_gdk = cell_colors[0]
            cell_props.background_gdk = cell_colors[1]

        return level_data_func

    def get_values_for_size(self):

        values = [Data.debug_level_log, Data.debug_level_debug,
                  Data.debug_level_info, Data.debug_level_warning,
                  Data.debug_level_error, Data.debug_level_memdump]

        return values


class PidColumn (TextColumn):

    name = "pid"
    label_header = _("PID")
    id = LazyLogModel.COL_PID
    font_family = "monospace"

    @staticmethod
    def get_modify_func():

        return str

    def get_values_for_size(self):

        return ["999999"]


class ThreadColumn (TextColumn):

    name = "thread"
    label_header = _("Thread")
    id = LazyLogModel.COL_THREAD
    font_family = "monospace"

    @staticmethod
    def get_modify_func():

        def format_thread(value):
            return "0x%07x" % (value,)

        return format_thread

    def get_values_for_size(self):

        return [int("ffffff", 16)]


class CategoryColumn (TextColumn):

    name = "category"
    label_header = _("Category")
    id = LazyLogModel.COL_CATEGORY

    def get_values_for_size(self):

        return ["GST_LONG_CATEGORY", "somelongelement"]


class CodeColumn (TextColumn):

    name = "code"
    label_header = _("Code")
    id = None

    @staticmethod
    def get_data_func():

        filename_id = LogModelBase.COL_FILENAME
        line_number_id = LogModelBase.COL_LINE_NUMBER

        def filename_data_func(column, cell, model, tree_iter, user_data):
            args = model.get(tree_iter, filename_id, line_number_id)
            cell.props.text = "%s:%i" % args

        return filename_data_func

    def get_values_for_size(self):

        return ["gstsomefilename.c:1234"]


class FunctionColumn (TextColumn):

    name = "function"
    label_header = _("Function")
    id = LazyLogModel.COL_FUNCTION

    def get_values_for_size(self):

        return ["gst_this_should_be_enough"]


class ObjectColumn (TextColumn):

    name = "object"
    label_header = _("Object")
    id = LazyLogModel.COL_OBJECT

    def get_values_for_size(self):

        return ["longobjectname00"]


class MessageColumn (TextColumn):

    name = "message"
    label_header = _("Message")
    id = None

    def __init__(self, *a, **kw):

        self.highlighters = {}

        TextColumn.__init__(self, *a, **kw)

    def get_data_func(self):

        highlighters = self.highlighters
        id_ = LazyLogModel.COL_MESSAGE

        def message_data_func(column, cell, model, tree_iter, user_data):

            msg = model.get_value(tree_iter, id_).decode("utf8", errors="replace")

            if not highlighters:
                cell.props.text = msg
                return

            if len(highlighters) > 1:
                raise NotImplementedError("FIXME: Support more than one...")

            highlighter = list(highlighters.values())[0]
            row = model[tree_iter]
            ranges = highlighter(row)
            if not ranges:
                cell.props.text = msg
            else:
                tags = []
                prev_end = 0
                end = None
                for start, end in ranges:
                    if prev_end < start:
                        tags.append(
                            GLib.markup_escape_text(msg[prev_end:start]))
                    msg_escape = GLib.markup_escape_text(msg[start:end])
                    tags.append("<span foreground=\'#FFFFFF\'"
                                " background=\'#0000FF\'>%s</span>" % (msg_escape,))
                    prev_end = end
                if end is not None:
                    tags.append(GLib.markup_escape_text(msg[end:]))
                cell.props.markup = "".join(tags)

        return message_data_func

    def get_values_for_size(self):

        values = ["Just some good minimum size"]

        return values


class ColumnManager (Common.GUI.Manager):

    column_classes = ()

    @classmethod
    def iter_item_classes(cls):

        return iter(cls.column_classes)

    def __init__(self):

        self.view = None
        self.actions = None
        self.zoom = 1.0
        self.__columns_changed_id = None
        self.columns = []
        self.column_order = list(self.column_classes)

        self.action_group = Gtk.ActionGroup("ColumnActions")

        def make_entry(col_class):
            return ("show-%s-column" % (col_class.name,),
                    None,
                    col_class.label_header,
                    None,
                    None,
                    None,
                    True,)

        entries = [make_entry(cls) for cls in self.column_classes]
        self.action_group.add_toggle_actions(entries)

    def iter_items(self):

        return iter(self.columns)

    def attach(self):

        for col_class in self.column_classes:
            action = self.get_toggle_action(col_class)
            if action.props.active:
                self._add_column(col_class())
            action.connect("toggled",
                           self.__handle_show_column_action_toggled,
                           col_class.name)

        self.__columns_changed_id = self.view.connect("columns-changed",
                                                      self.__handle_view_columns_changed)

    def detach(self):

        if self.__columns_changed_id is not None:
            self.view.disconnect(self.__columns_changed_id)
            self.__columns_changed_id = None

    def attach_sort(self):

        sort_model = self.view.get_model()

        # Inform the sorted tree model of any custom sorting functions.
        for col_class in self.column_classes:
            if col_class.get_sort_func:
                sort_func = col_class.get_sort_func()
                sort_model.set_sort_func(col_class.id, sort_func)

    def enable_sort(self):

        sort_model = self.view.get_model()

        if sort_model:
            self.logger.debug("activating sort")
            sort_model.set_sort_column_id(*self.default_sort)
            self.default_sort = None
        else:
            self.logger.debug("not activating sort (no model set)")

    def disable_sort(self):

        self.logger.debug("deactivating sort")

        sort_model = self.view.get_model()

        self.default_sort = tree_sortable_get_sort_column_id(sort_model)

        sort_model.set_sort_column_id(TREE_SORTABLE_UNSORTED_COLUMN_ID,
                                      Gtk.SortType.ASCENDING)

    def set_zoom(self, scale):

        for column in self.columns:
            cell = column.view_column.get_cells()[0]
            cell.props.scale = scale
            column.view_column.queue_resize()

        self.zoom = scale

    def set_base_time(self, base_time):

        try:
            time_column = self.find_item(name=TimeColumn.name)
        except KeyError:
            return

        time_column.set_base_time(base_time)
        self.size_column(time_column)

    def get_toggle_action(self, column_class):

        action_name = "show-%s-column" % (column_class.name,)
        return self.action_group.get_action(action_name)

    def get_initial_column_order(self):

        return tuple(self.column_classes)

    def _add_column(self, column):

        name = column.name
        pos = self.__get_column_insert_position(column)

        if self.view.props.fixed_height_mode:
            column.view_column.props.sizing = Gtk.TreeViewColumnSizing.FIXED

        cell = column.view_column.get_cells()[0]
        cell.props.scale = self.zoom

        self.columns.insert(pos, column)
        self.view.insert_column(column.view_column, pos)

    def _remove_column(self, column):

        self.columns.remove(column)
        self.view.remove_column(column.view_column)

    def __get_column_insert_position(self, column):

        col_class = self.find_item_class(name=column.name)
        pos = self.column_order.index(col_class)
        before = self.column_order[:pos]
        shown_names = [col.name for col in self.columns]
        for col_class in before:
            if col_class.name not in shown_names:
                pos -= 1
        return pos

    def __iter_next_hidden(self, column_class):

        pos = self.column_order.index(column_class)
        rest = self.column_order[pos + 1:]
        for next_class in rest:
            try:
                self.find_item(name=next_class.name)
            except KeyError:
                # No instance -- the column is hidden.
                yield next_class
            else:
                break

    def __handle_show_column_action_toggled(self, toggle_action, name):

        if toggle_action.props.active:
            try:
                # This should fail.
                column = self.find_item(name=name)
            except KeyError:
                col_class = self.find_item_class(name=name)
                self._add_column(col_class())
            else:
                # Out of sync for some reason.
                return
        else:
            try:
                column = self.find_item(name=name)
            except KeyError:
                # Out of sync for some reason.
                return
            else:
                self._remove_column(column)

    def __handle_view_columns_changed(self, element_view):

        view_columns = element_view.get_columns()
        new_visible = [self.find_item(view_column=column)
                       for column in view_columns]

        # We only care about reordering here.
        if len(new_visible) != len(self.columns):
            return

        if new_visible != self.columns:

            new_order = []
            for column in new_visible:
                col_class = self.find_item_class(name=column.name)
                new_order.append(col_class)
                new_order.extend(self.__iter_next_hidden(col_class))

            names = (column.name for column in new_visible)
            self.logger.debug("visible columns reordered: %s",
                              ", ".join(names))

            self.columns[:] = new_visible
            self.column_order[:] = new_order


class ViewColumnManager (ColumnManager):

    column_classes = (
        TimeColumn, LevelColumn, PidColumn, ThreadColumn, CategoryColumn,
        CodeColumn, FunctionColumn, ObjectColumn, MessageColumn,)

    default_column_classes = (
        TimeColumn, LevelColumn, CategoryColumn, CodeColumn,
        FunctionColumn, ObjectColumn, MessageColumn,)

    def __init__(self, state):

        ColumnManager.__init__(self)

        self.logger = logging.getLogger("ui.columns")

        self.state = state

    def attach(self, view):

        self.view = view
        view.connect("notify::model", self.__handle_notify_model)

        order = self.state.column_order
        if len(order) == len(self.column_classes):
            self.column_order[:] = order

        visible = self.state.columns_visible
        if not visible:
            visible = self.default_column_classes
        for col_class in self.column_classes:
            action = self.get_toggle_action(col_class)
            action.props.active = (col_class in visible)

        ColumnManager.attach(self)

        self.columns_sized = False

    def detach(self):

        self.state.column_order = self.column_order
        self.state.columns_visible = self.columns

        return ColumnManager.detach(self)

    def set_zoom(self, scale):

        ColumnManager.set_zoom(self, scale)

        if self.view is None:
            return

        # Timestamp and log level columns are pretty much fixed size, so resize
        # them back to default on zoom change:
        names = (TimeColumn.name,
                 LevelColumn.name,
                 PidColumn.name,
                 ThreadColumn.name)
        for column in self.columns:
            if column.name in names:
                self.size_column(column)

    def size_column(self, column):

        if column.default_size is None:
            default_size = column.compute_default_size()
        else:
            default_size = column.default_size
        # FIXME: Abstract away fixed size setting in Column class!
        if default_size is None:
            # Dummy fallback:
            column.view_column.props.fixed_width = 50
            self.logger.warning(
                "%s column does not implement default size", column.name)
        else:
            column.view_column.props.fixed_width = default_size

    def _add_column(self, column):

        result = ColumnManager._add_column(self, column)
        self.size_column(column)
        return result

    def _remove_column(self, column):

        column.default_size = column.view_column.props.fixed_width
        return ColumnManager._remove_column(self, column)

    def __handle_notify_model(self, view, gparam):

        if self.columns_sized:
            # Already sized.
            return
        model = self.view.get_model()
        if model is None:
            return
        self.logger.debug("model changed, sizing columns")
        for column in self.iter_items():
            self.size_column(column)
        self.columns_sized = True


class WrappingMessageColumn (MessageColumn):

    def wrap_to_width(self, width):

        col = self.view_column
        col.props.max_width = width
        col.get_cells()[0].props.wrap_width = width
        col.queue_resize()


class LineViewColumnManager (ColumnManager):

    column_classes = (TimeColumn, WrappingMessageColumn,)

    def __init__(self):

        ColumnManager.__init__(self)

    def attach(self, window):

        self.__size_update = None

        self.view = window.widgets.line_view
        self.view.set_size_request(0, 0)
        self.view.connect_after("size-allocate", self.__handle_size_allocate)
        ColumnManager.attach(self)

    def __update_sizes(self):

        view_width = self.view.get_allocation().width
        if view_width == self.__size_update:
            # Prevent endless recursion.
            return

        self.__size_update = view_width

        col = self.find_item(name="time")
        other_width = col.view_column.props.width

        try:
            col = self.find_item(name="message")
        except KeyError:
            return

        width = view_width - other_width
        col.wrap_to_width(width)

    def __handle_size_allocate(self, self_, allocation):

        self.__update_sizes()
