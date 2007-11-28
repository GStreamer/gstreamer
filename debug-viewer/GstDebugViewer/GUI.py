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

__author__ = u"René Stadler <mail@renestadler.de>"
__version__ = "0.1"

def _ (s):
    return s

import sys
import os
import os.path
from operator import add
from sets import Set
import logging

import pygtk
pygtk.require ("2.0")

import gobject
import gtk
import gtk.glade

## import gnome # FIXME

from GstDebugViewer import Common, Data, Main

class Color (object):

    def __init__ (self, hex_24):

        if hex_24.startswith ("#"):
            s = hex_24[1:]
        else:
            s = hex_24

        self._fields = tuple ((int (hs, 16) for hs in (s[:2], s[2:4], s[4:],)))

    def hex_string (self):

        return "#%02x%02x%02x" % self._fields

    def float_tuple (self):

        return tuple ((float (x) / 255 for x in self._fields))

    def byte_tuple (self):

        return self._fields

    def short_tuple (self):

        return tuple ((x << 8 for x in self._fields))

class ColorPalette (object):

    @classmethod
    def get (cls):

        try:
            return cls._instance
        except AttributeError:
            cls._instance = cls ()
            return cls._instance

class TangoPalette (ColorPalette):

    def __init__ (self):

        for name, r, g, b in  [("butter1", 252, 233, 79),
                               ("butter2", 237, 212, 0),
                               ("butter3", 196, 160, 0),
                               ("chameleon1", 138, 226, 52),
                               ("chameleon2", 115, 210, 22),
                               ("chameleon3", 78, 154, 6),
                               ("orange1", 252, 175, 62),
                               ("orange2", 245, 121, 0),
                               ("orange3", 206, 92, 0),
                               ("skyblue1", 114, 159, 207),
                               ("skyblue2", 52, 101, 164),
                               ("skyblue3", 32, 74, 135),
                               ("plum1", 173, 127, 168),
                               ("plum2", 117, 80, 123),
                               ("plum3", 92, 53, 102),
                               ("chocolate1", 233, 185, 110),
                               ("chocolate2", 193, 125, 17),
                               ("chocolate3", 143, 89, 2),
                               ("scarletred1", 239, 41, 41),
                               ("scarletred2", 204, 0, 0),
                               ("scarletred3", 164, 0, 0),
                               ("aluminium1", 238, 238, 236),
                               ("aluminium2", 211, 215, 207),
                               ("aluminium3", 186, 189, 182),
                               ("aluminium4", 136, 138, 133),
                               ("aluminium5", 85, 87, 83),
                               ("aluminium6", 46, 52, 54)]:
            setattr (self, name, Color ("%02x%02x%02x" % (r, g, b,)))

class ColorTheme (object):

    def __init__ (self):

        self.colors = {}

    def add_color (self, key, fg_color, bg_color = None, bg_color2 = None):

        self.colors[key] = (fg_color, bg_color, bg_color2,)

    def colors_float (self, key):

        return tuple ((self.hex_string_to_floats (color)
                       for color in self.colors[key]))

    @staticmethod
    def hex_string_to_floats (s):

        if s.startswith ("#"):
            s = s[1:]
        return tuple ((float (int (hs, 16)) / 255. for hs in (s[:2], s[2:4], s[4:],)))

class LevelColorTheme (ColorTheme):

    pass

class LevelColorThemeTango (LevelColorTheme):

    def __init__ (self):

        LevelColorTheme.__init__ (self)

        self.add_color (Data.debug_level_none, None, None, None)
        self.add_color (Data.debug_level_log, "#000000", "#ad7fa8", "#e0a4d9")
        self.add_color (Data.debug_level_debug, "#000000", "#729fcf", "#8cc4ff")
        self.add_color (Data.debug_level_info, "#000000", "#8ae234", "#9dff3b")
        self.add_color (Data.debug_level_warning, "#000000", "#fcaf3e", "#ffc266")
        self.add_color (Data.debug_level_error, "#ffffff", "#ef2929", "#ff4545")

class ThreadColorTheme (ColorTheme):

    pass

class ThreadColorThemeTango (ThreadColorTheme):

    def __init__ (self):

        ThreadColorTheme.__init__ (self)

        t = TangoPalette.get ()
        for i, color in enumerate ([t.butter2,
                                    t.orange2,
                                    t.chocolate3,
                                    t.chameleon2,
                                    t.skyblue1,
                                    t.plum1,
                                    t.scarletred1,
                                    t.aluminium6]):
            self.add_color (i, color)

class LogModelBase (gtk.GenericTreeModel):

    __metaclass__ = Common.GUI.MetaModel

    columns = ("COL_TIME", gobject.TYPE_UINT64,
               "COL_PID", int,
               "COL_THREAD", gobject.TYPE_UINT64,
               "COL_LEVEL", object,               
               "COL_CATEGORY", str,
               "COL_FILENAME", str,
               "COL_LINE_NUMBER", int,
               "COL_FUNCTION", str,
               "COL_OBJECT", str,
               "COL_MESSAGE", str,)

    def __init__ (self):

        gtk.GenericTreeModel.__init__ (self)

        ##self.props.leak_references = False

        self.line_offsets = []
        self.line_levels = [] # FIXME: Not so nice!
        self.line_cache = {}

    def ensure_cached (self, line_offset):

        raise NotImplementedError ("derived classes must override this method")

    def access_offset (self, offset):

        raise NotImplementedError ("derived classes must override this method")

    def iter_rows_offset (self):

        for i, offset in enumerate (self.line_offsets):
            self.ensure_cached (offset)
            row = self.line_cache[offset]
            row[self.COL_LEVEL] = self.line_levels[i] # FIXME
            yield (row, offset,)

    def on_get_flags (self):

        flags = gtk.TREE_MODEL_LIST_ONLY | gtk.TREE_MODEL_ITERS_PERSIST

        return flags

    def on_get_n_columns (self):
        
        return len (self.column_types)

    def on_get_column_type (self, col_id):

        return self.column_types[col_id]

    def on_get_iter (self, path):

        if not path:
            return

        if len (path) > 1:
            # Flat model.
            return None

        line_index = path[0]

        if line_index > len (self.line_offsets) - 1:
            return None

        return line_index

    def on_get_path (self, rowref):

        line_index = rowref

        return (line_index,)

    def on_get_value (self, line_index, col_id):

        last_index = len (self.line_offsets) - 1

        if line_index > last_index:
            return None

        if col_id == self.COL_LEVEL:
            return self.line_levels[line_index]

        line_offset = self.line_offsets[line_index]
        self.ensure_cached (line_offset)

        value = self.line_cache[line_offset][col_id]
        if col_id == self.COL_MESSAGE:
            message_offset = value
            value = self.access_offset (line_offset + message_offset).strip ()

        return value

    def on_iter_next (self, line_index):

        last_index = len (self.line_offsets) - 1

        if line_index >= last_index:
            return None
        else:
            return line_index + 1

    def on_iter_children (self, parent):

        return self.on_iter_nth_child (parent, 0)

    def on_iter_has_child (self, rowref):

        return False

    def on_iter_n_children (self, rowref):

        if rowref is not None:
            return 0

        return len (self.line_offsets)

    def on_iter_nth_child (self, parent, n):

        last_index = len (self.line_offsets) - 1

        if parent or n > last_index:
            return None

        return n

    def on_iter_parent (self, child):

        return None

    ## def on_ref_node (self, rowref):

    ##     pass

    ## def on_unref_node (self, rowref):

    ##     pass

class LazyLogModel (LogModelBase):

    def __init__ (self, log_obj = None):

        LogModelBase.__init__ (self)

        self.__log_obj = log_obj

        if log_obj:
            self.set_log (log_obj)

    def set_log (self, log_obj):

        self.__fileobj = log_obj.fileobj

        self.line_cache.clear ()
        self.line_offsets = log_obj.line_cache.offsets
        self.line_levels = log_obj.line_cache.levels

    def access_offset (self, offset):

        self.__fileobj.seek (offset)
        return self.__fileobj.readline ()

    def ensure_cached (self, line_offset):

        if line_offset in self.line_cache:
            return

        if line_offset == 0:
            self.__fileobj.seek (0)
            line = self.__fileobj.readline ()
        else:
            # Seek a bit further backwards to verify that offset (still) points
            # to the beginning of a line:
            self.__fileobj.seek (line_offset - len (os.linesep))
            line_start = (self.__fileobj.readline () == os.linesep)
            if not line_start:
                # FIXME: We should re-read the file instead!
                raise ValueError ("file changed!")
            line = self.__fileobj.readline ()

        self.line_cache[line_offset] = Data.LogLine.parse_full (line)

class FilteredLogModel (LogModelBase):

    def __init__ (self, lazy_log_model):

        LogModelBase.__init__ (self)

        self.parent_model = lazy_log_model
        self.access_offset = lazy_log_model.access_offset
        self.ensure_cached = lazy_log_model.ensure_cached
        self.line_cache = lazy_log_model.line_cache
        self.reset ()
        
    def reset (self):

        del self.line_offsets[:]
        self.line_offsets += self.parent_model.line_offsets
        del self.line_levels[:]
        self.line_levels += self.parent_model.line_levels

    def add_filter (self, filter):

        func = filter.filter_func
        #enum = self.lazy_log_model.iter_rows_offset ()
        enum = self.iter_rows_offset ()
        self.line_offsets[:] = (offset for row, offset in enum
                                if func (row))

    def parent_line_index (self, line_index):

        return line_index # FIXME

class Filter (object):

    pass

class SubRange (object):

    def __init__ (self, l, start, end):

        if start > end:
            raise ValueError ("need start <= end")

        self.l = l
        self.start = start
        self.end = end

    def __getitem__ (self, i):

        return self.l[i + self.start]

    def __len__ (self):

        return self.end - self.start

    def __iter__ (self):

        l = self.l
        i = self.start
        while i <= self.end:
            yield l[i]

class RangeFilteredLogModel (FilteredLogModel):

    def __init__ (self, lazy_log_model):

        FilteredLogModel.__init__ (self, lazy_log_model)

        self.line_index_range = None

    def set_range (self, start_index, last_index):

        self.line_index_range = (start_index, last_index,)
        self.line_offsets = SubRange (self.parent_model.line_offsets,
                                      start_index, last_index)
        self.line_levels = SubRange (self.parent_model.line_levels,
                                     start_index, last_index)

    def parent_line_index (self, line_index):

        start_index = self.line_index_range[0]

        return line_index + start_index

class DebugLevelFilter (Filter):

    def __init__ (self, debug_level):

        col_id = LogModelBase.COL_LEVEL
        def filter_func (row):
            return row[col_id] < debug_level
        self.filter_func = filter_func

# Sync with gst-inspector!
class Column (object):

    """A single list view column, managed by a ColumnManager instance."""

    name = None
    id = None
    label_header = None
    get_modify_func = None
    get_data_func = None
    get_sort_func = None

    def __init__ (self):

        view_column = gtk.TreeViewColumn (self.label_header)
        view_column.props.reorderable = True

        self.view_column = view_column

# FIXME: Merge with gst-inspector?
class SizedColumn (Column):

    default_size = None

    def compute_default_size (self, view, model):

        return None

# Sync with gst-inspector?
class TextColumn (SizedColumn):

    font_family = None

    def __init__ (self):

        Column.__init__ (self)

        column = self.view_column
        cell = gtk.CellRendererText ()
        column.pack_start (cell)

        if self.font_family:
            cell.props.family = self.font_family
            cell.props.family_set = True

        if self.get_data_func:
            data_func = self.get_data_func ()
            assert data_func
            id_ = self.id
            if id_ is not None:
                def cell_data_func (column, cell, model, tree_iter):
                    data_func (cell.props, model.get_value (tree_iter, id_), model.get_path (tree_iter))
            else:
                cell_data_func = data_func
            column.set_cell_data_func (cell, cell_data_func)
        elif not self.get_modify_func:
            column.add_attribute (cell, "text", self.id)
        else:
            modify_func = self.get_modify_func ()
            id_ = self.id
            def cell_data_func (column, cell, model, tree_iter):
                cell.props.text = modify_func (model.get (tree_iter, id_)[0])
            column.set_cell_data_func (cell, cell_data_func)

        column.props.resizable = True
        ## column.set_sort_column_id (self.id)

    def compute_default_size (self, view, model):

        values = self.get_values_for_size ()
        if not values:
            return SizedColumn.compute_default_size (self, view, model)

        cell = self.view_column.get_cell_renderers ()[0]

        if self.get_modify_func is not None:
            format = self.get_modify_func ()
        else:
            def identity (x):
                return x
            format = identity
        max_width = 0
        for value in values:
            cell.props.text = format (value)
            rect, x, y, w, h = self.view_column.cell_get_size ()
            max_width = max (max_width, w)

        return max_width

    def get_values_for_size (self):

        return ()

class TimeColumn (TextColumn):

    name = "time"
    label_header = _("Time")
    id = LazyLogModel.COL_TIME
    font_family = "monospace"

    @staticmethod
    def get_modify_func ():

        time_args = Data.time_args
        def format_time (value):
            # TODO: This is hard coded to omit hours as well as the last 3
            # digits at the end, since current gst uses g_get_current_time,
            # which has microsecond precision only.
            return time_args (value)[2:-3]

        return format_time

    def get_values_for_size (self):

        # TODO: Use more than just 0:00:00.000000000 to account for funny fonts
        # maybe? Well, or use monospaced...
        values = [0]

        return values

class LevelColumn (TextColumn):

    name = "level"
    label_header = _("L")
    id = LazyLogModel.COL_LEVEL

    def __init__ (self):

        TextColumn.__init__ (self)

        cell = self.view_column.get_cell_renderers ()[0]
        cell.props.xalign = .5

    @staticmethod
    def get_modify_func ():

        def format_level (value):
            return value.name[0]

        return format_level

    @staticmethod
    def get_data_func ():

        theme = LevelColorThemeTango ()
        colors = theme.colors
        def level_data_func (cell_props, level, path):
            cell_props.text = level.name[0]
            cell_colors = colors[level]
            # FIXME: Use GdkColors!
            cell_props.foreground = cell_colors[0]
            if path[0] % 2:
                cell_props.background = cell_colors[1]
            else:
                cell_props.background = cell_colors[2]

        return level_data_func

    def get_values_for_size (self):

        values = [Data.debug_level_log, Data.debug_level_debug,
                  Data.debug_level_info, Data.debug_level_warning,
                  Data.debug_level_error]

        return values

class PidColumn (TextColumn):

    name = "pid"
    label_header = _("PID")
    id = LazyLogModel.COL_PID
    font_family = "monospace"

    @staticmethod
    def get_modify_func ():

        return str

    def get_values_for_size (self):

        # TODO: Same as for TimeColumn.  There is no guarantee that 999999 is
        # the widest string; use fixed font or come up with something better.

        return ["999999"]

class ThreadColumn (TextColumn):

    name = "thread"
    label_header = _("Thread")
    id = LazyLogModel.COL_THREAD
    font_family = "monospace"

    @staticmethod
    def get_modify_func ():

        def format_thread (value):
            return "0x%07x" % (value,)

        return format_thread

    def get_values_for_size (self):

        # TODO: Same as for TimeColumn.  There is no guarantee that ffffff is
        # the widest string; use fixed font or come up with something better.

        return [int ("ffffff", 16)]

class CategoryColumn (TextColumn):

    name = "category"
    label_header = _("Category")
    id = LazyLogModel.COL_CATEGORY

    def get_values_for_size (self):

        return ["GST_LONG_CATEGORY", "somelongelement"]

class CodeColumn (TextColumn):

    name = "code"
    label_header = _("Code")
    id = None

    @staticmethod
    def get_data_func ():

        filename_id = LogModelBase.COL_FILENAME
        line_number_id = LogModelBase.COL_LINE_NUMBER
        def filename_data_func (column, cell, model, tree_iter):
            args = model.get (tree_iter, filename_id, line_number_id)
            cell.props.text = "%s:%i" % args

        return filename_data_func

    def get_values_for_size (self):

        return ["gstsomefilename.c:1234"]

class FunctionColumn (TextColumn):

    name = "function"
    label_header = _("Function")
    id = LazyLogModel.COL_FUNCTION

    def get_values_for_size (self):

        return ["gst_this_should_be_enough"]

class ObjectColumn (TextColumn):

    name = "object"
    label_header = _("Object")
    id = LazyLogModel.COL_OBJECT

    def get_values_for_size (self):

        return ["longobjectname00"]

class MessageColumn (TextColumn):

    name = "message"
    label_header = _("Message")
    id = LazyLogModel.COL_MESSAGE

    def __init__ (self, *a, **kw):

        self.highlight = {}

        TextColumn.__init__ (self, *a, **kw)

    def get_data_func (self):

        highlight = self.highlight
        escape = gobject.markup_escape_text

        def message_data_func (props, value, path):

            line_index = path[0]
            if line_index in highlight:
                props.text = None
                start, end = highlight[line_index]
                props.markup = escape (value[:start]) + \
                               "<span background='blue' foreground='white'>" + \
                               escape (value[start:end]) + \
                               "</span>" + \
                               escape (value[end:])
            else:
                props.markup = None
                props.text = value

        return message_data_func

class ColumnManager (Common.GUI.Manager):

    column_classes = ()

    @classmethod
    def iter_item_classes (cls):

        return iter (cls.column_classes)

    def __init__ (self):

        self.view = None
        self.actions = None
        self.__columns_changed_id = None
        self.columns = []
        self.column_order = list (self.column_classes)

        self.action_group = gtk.ActionGroup ("ColumnActions")

        def make_entry (col_class):
            return ("show-%s-column" % (col_class.name,),
                    None,
                    col_class.label_header,
                    None,
                    None,
                    None,
                    True,)

        entries = [make_entry (cls) for cls in self.column_classes]
        self.action_group.add_toggle_actions (entries)

    def iter_items (self):

        return iter (self.columns)

    def attach (self):

        for col_class in self.column_classes:
            action = self.get_toggle_action (col_class)
            if action.props.active:
                self._add_column (col_class ())
            action.connect ("toggled",
                            self.__handle_show_column_action_toggled,
                            col_class.name)

        self.__columns_changed_id = self.view.connect ("columns-changed",
                                                       self.__handle_view_columns_changed)

    def detach (self):

        if self.__columns_changed_id is not None:
            self.view.disconnect (self.__columns_changed_id)
            self.__columns_changed_id = None

    def attach_sort (self):

        sort_model = self.view.props.model

        # Inform the sorted tree model of any custom sorting functions.
        for col_class in self.column_classes:
            if col_class.get_sort_func:
                sort_func = col_class.get_sort_func ()
                sort_model.set_sort_func (col_class.id, sort_func)

    def enable_sort (self):

        sort_model = self.view.props.model

        if sort_model:
            self.logger.debug ("activating sort")
            sort_model.set_sort_column_id (*self.default_sort)
            self.default_sort = None
        else:
            self.logger.debug ("not activating sort (no model set)")

    def disable_sort (self):

        self.logger.debug ("deactivating sort")

        sort_model = self.view.props.model

        self.default_sort = tree_sortable_get_sort_column_id (sort_model)

        sort_model.set_sort_column_id (TREE_SORTABLE_UNSORTED_COLUMN_ID,
                                       gtk.SORT_ASCENDING)

    def get_toggle_action (self, column_class):

        action_name = "show-%s-column" % (column_class.name,)
        return self.action_group.get_action (action_name)

    def get_initial_column_order (self):

        return tuple (self.column_classes)

    def _add_column (self, column):

        name = column.name
        pos = self.__get_column_insert_position (column)
        if self.view.props.fixed_height_mode:
            column.view_column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
        self.columns.insert (pos, column)
        self.view.insert_column (column.view_column, pos)

    def _remove_column (self, column):

        self.columns.remove (column)
        self.view.remove_column (column.view_column)

    def __get_column_insert_position (self, column):

        col_class = self.find_item_class (name = column.name)
        pos = self.column_order.index (col_class)
        before = self.column_order[:pos]
        shown_names = [col.name for col in self.columns]
        for col_class in before:
            if not col_class.name in shown_names:
                pos -= 1
        return pos

    def __iter_next_hidden (self, column_class):

        pos = self.column_order.index (column_class)
        rest = self.column_order[pos + 1:]
        for next_class in rest:
            try:
                self.find_item (name = next_class.name)
            except KeyError:
                # No instance -- the column is hidden.
                yield next_class
            else:
                break

    def __handle_show_column_action_toggled (self, toggle_action, name):

        if toggle_action.props.active:
            try:
                # This should fail.
                column = self.find_item (name = name)
            except KeyError:
                col_class = self.find_item_class (name = name)
                self._add_column (col_class ())
            else:
                # Out of sync for some reason.
                return
        else:
            try:
                column = self.find_item (name = name)
            except KeyError:
                # Out of sync for some reason.
                return
            else:
                self._remove_column (column)

    def __handle_view_columns_changed (self, element_view):

        view_columns = element_view.get_columns ()
        new_visible = [self.find_item (view_column = column)
                       for column in view_columns]

        # We only care about reordering here.
        if len (new_visible) != len (self.columns):
            return

        if new_visible != self.columns:

            new_order = []
            for column in new_visible:
                col_class = self.find_item_class (name = column.name)
                new_order.append (col_class)
                new_order.extend (self.__iter_next_hidden (col_class))
            
            names = (column.name for column in new_visible)
            self.logger.debug ("visible columns reordered: %s",
                               ", ".join (names))

            self.columns[:] = new_visible
            self.column_order[:] = new_order

class ViewColumnManager (ColumnManager):

    column_classes = (TimeColumn, LevelColumn, PidColumn, ThreadColumn, CategoryColumn,
                      CodeColumn, FunctionColumn, ObjectColumn, MessageColumn,)

    def __init__ (self, state):

        ColumnManager.__init__ (self)

        self.logger = logging.getLogger ("ui.columns")

        self.state = state

    def attach (self, view):

        self.view = view
        view.connect ("notify::model", self.__handle_notify_model)

        order = self.state.column_order
        if len (order) == len (self.column_classes):
            self.column_order[:] = order

        visible = self.state.columns_visible
        if not visible:
            visible = self.column_classes
        for col_class in self.column_classes:
            action = self.get_toggle_action (col_class)
            action.props.active = (col_class in visible)

        ColumnManager.attach (self)

    def detach (self):

        self.state.column_order = self.column_order
        self.state.columns_visible = self.columns

        return ColumnManager.detach (self)

    def size_column (self, column, view, model):

        if column.default_size is None:
            default_size = column.compute_default_size (view, model)
        else:
            default_size = column.default_size
        # FIXME: Abstract away fixed size setting in Column class!
        if default_size is None:
            # Dummy fallback:
            column.view_column.props.fixed_width = 50
            self.logger.warning ("%s column does not implement default size", column.name)
        else:
            column.view_column.props.fixed_width = default_size

    def _add_column (self, column):

        result = ColumnManager._add_column (self, column)
        model = self.view.props.model
        self.size_column (column, self.view, model)
        return result

    def _remove_column (self, column):

        column.default_size = column.view_column.props.fixed_width
        return ColumnManager._remove_column (self, column)

    def __handle_notify_model (self, view, gparam):

        model = self.view.props.model
        self.logger.debug ("model changed: %r", model)
        if model is None:
            return
        for column in self.iter_items ():
            self.size_column (column, view, model)

class Window (object):

    def __init__ (self, app):

        self.logger = logging.getLogger ("ui.window")
        self.app = app

        self.dispatcher = None
        self.progress_bar = None
        self.update_progress_id = None

        self.window_state = Common.GUI.WindowState ()
        self.column_manager = ViewColumnManager (app.state_section)

        self.actions = Common.GUI.Actions ()

        group = gtk.ActionGroup ("MenuActions")
        group.add_actions ([("FileMenuAction", None, _("_File")),
                            ("ViewMenuAction", None, _("_View")),
                            ("ViewColumnsMenuAction", None, _("_Columns")),
                            ("HelpMenuAction", None, _("_Help"))])
        self.actions.add_group (group)

        group = gtk.ActionGroup ("WindowActions")
        group.add_actions ([("new-window", gtk.STOCK_NEW, _("_New Window"), "<Ctrl>N"),
                            ("open-file", gtk.STOCK_OPEN, _("_Open File"), "<Ctrl>O"),
                            ("reload-file", gtk.STOCK_REFRESH, _("_Reload File"), "<Ctrl>R"),
                            ("close-window", gtk.STOCK_CLOSE, _("Close _Window"), "<Ctrl>W"),
                            ("cancel-load", gtk.STOCK_CANCEL, None,),
                            ("show-about", gtk.STOCK_ABOUT, None)])
        ## group.add_toggle_actions ([("show-line-density", None, _("Line _Density"), "<Ctrl>D")])
        self.actions.add_group (group)
        self.actions.reload_file.props.sensitive = False

        group = gtk.ActionGroup ("RowActions")
        group.add_actions ([("omit-before-line", None, _("Omit lines before this one")),
                            ("omit-after-line", None, _("Omit lines after this one")),
                            ("show-hidden-lines", None, _("Show omitted lines")),
                            ("edit-copy-line", gtk.STOCK_COPY, _("Copy line"), "<Ctrl>C"),
                            ("edit-copy-message", gtk.STOCK_COPY, _("Copy message")),
                            ("filter-out-higher-levels", None, _("Filter out higher debug levels"))])
        group.props.sensitive = False
        self.actions.add_group (group)

        self.actions.add_group (self.column_manager.action_group)

        self.log_file = None
        self.log_model = LazyLogModel ()
        self.log_filter = FilteredLogModel (self.log_model)

        glade_filename = os.path.join (Main.Paths.data_dir, "gst-debug-viewer.glade")
        self.widget_factory = Common.GUI.WidgetFactory (glade_filename)
        self.widgets = self.widget_factory.make ("main_window")

        ui_filename = os.path.join (Main.Paths.data_dir,
                                    "gst-debug-viewer.ui")
        self.ui_factory = Common.GUI.UIFactory (ui_filename, self.actions)

        self.ui_manager = ui = self.ui_factory.make ()
        menubar = ui.get_widget ("/ui/menubar")        
        self.widgets.vbox_main.pack_start (menubar, False, False, 0)
        self.view_popup = ui.get_widget ("/ui/menubar/ViewMenu").get_submenu ()

        self.gtk_window = self.widgets.main_window
        self.gtk_window.add_accel_group (ui.get_accel_group ())
        self.log_view = self.widgets.log_view
        self.log_view.drag_dest_unset ()
        self.log_view.set_search_column (-1)
        self.log_view.props.enable_search = False
        self.log_view.props.fixed_height_mode = True

        self.log_view.connect ("button-press-event", self.handle_log_view_button_press_event)

        self.attach ()
        self.column_manager.attach (self.log_view)

    def get_top_attach_point (self):

        return self.widgets.vbox_main

    def get_side_attach_point (self):

        return self.widgets.hbox_view

    def attach (self):

        self.window_state.attach (window = self.gtk_window,
                                  state = self.app.state_section)

        self.clipboard = gtk.Clipboard (self.gtk_window.get_display (),
                                        gtk.gdk.SELECTION_CLIPBOARD)

        for action_name in ("new-window", "open-file", "reload-file",
                            "close-window", "cancel-load",
                            "omit-before-line", "omit-after-line", "show-hidden-lines",
                            "edit-copy-line", "edit-copy-message",
                            "filter-out-higher-levels",
                            "show-about",):
            name = action_name.replace ("-", "_")
            action = getattr (self.actions, name)
            handler = getattr (self, "handle_%s_action_activate" % (name,))
            action.connect ("activate", handler)

        self.gtk_window.connect ("delete-event", self.handle_window_delete_event)

        self.features = []

        for plugin_feature in self.app.iter_plugin_features ():
            feature = plugin_feature (self.app)
            self.features.append (feature)

        for feature in self.features:
            feature.handle_attach_window (self)

        # FIXME: With multiple selection mode, browsing the list with key
        # up/down slows to a crawl! WTF is wrong with this stupid widget???
        sel = self.log_view.get_selection ()
        sel.set_mode (gtk.SELECTION_BROWSE)

    def detach (self):

        self.set_log_file (None)
        for feature in self.features:
            feature.handle_detach_window (self)

        self.window_state.detach ()
        self.column_manager.detach ()

    def get_active_line_index (self):

        selection = self.log_view.get_selection ()
        model, tree_iter = selection.get_selected ()
        if tree_iter is None:
            raise ValueError ("no line selected")
        path = model.get_path (tree_iter)
        return path[0]

    def get_active_line (self):

        selection = self.log_view.get_selection ()
        model, tree_iter = selection.get_selected ()
        if tree_iter is None:
            raise ValueError ("no line selected")
        model = self.log_view.props.model
        return model.get (tree_iter, *LogModelBase.column_ids)

    def close (self, *a, **kw):

        self.logger.debug ("closing window, detaching")
        self.detach ()
        self.gtk_window.hide ()
        self.logger.debug ("requesting close from app")
        self.app.close_window (self)

    def handle_window_delete_event (self, window, event):

        self.actions.close_window.activate ()

    def handle_new_window_action_activate (self, action):

        pass

    def handle_open_file_action_activate (self, action):

        dialog = gtk.FileChooserDialog (None, self.gtk_window,
                                        gtk.FILE_CHOOSER_ACTION_OPEN,
                                        (gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                         gtk.STOCK_OPEN, gtk.RESPONSE_ACCEPT,))
        response = dialog.run ()
        dialog.hide ()
        if response == gtk.RESPONSE_ACCEPT:
            self.set_log_file (dialog.get_filename ())
        dialog.destroy ()

    def handle_reload_file_action_activate (self, action):

        if self.log_file is None:
            return

        self.set_log_file (self.log_file.path)

    def handle_cancel_load_action_activate (self, action):

        self.logger.debug ("cancelling data load")

        self.set_log_file (None)
        if self.progress_dialog:
            self.progress_dialog.destroy ()
            self.progress_dialog = None
        self.progress_bar = None
        if self.update_progress_id is not None:
            gobject.source_remove (self.update_progress_id)
            self.update_progress_id = None

    def handle_close_window_action_activate (self, action):

        self.close ()

    def handle_omit_after_line_action_activate (self, action):

        first_index = self.log_filter.parent_line_index (0)
        try:
            filtered_line_index = self.get_active_line_index ()
        except ValueError:
            return
        last_index = self.log_filter.parent_line_index (filtered_line_index)

        self.logger.info ("omitting lines after %i (abs %i), first line is abs %i",
                          filtered_line_index,
                          last_index,
                          first_index)

        self.log_filter = RangeFilteredLogModel (self.log_model)
        self.log_filter.set_range (first_index, last_index + 1)
        self.log_view.props.model = self.log_filter
        self.actions.show_hidden_lines.props.sensitive = True

    def handle_omit_before_line_action_activate (self, action):

        try:
            filtered_line_index = self.get_active_line_index ()
        except ValueError:
            return
        first_index = self.log_filter.parent_line_index (filtered_line_index)
        last_index = self.log_filter.parent_line_index (len (self.log_filter) - 1)

        self.logger.info ("omitting lines before %i (abs %i), last line is abs %i",
                          filtered_line_index,
                          first_index,
                          last_index)

        self.log_filter = RangeFilteredLogModel (self.log_model)
        self.log_filter.set_range (first_index, last_index)
        self.log_view.props.model = self.log_filter
        self.actions.show_hidden_lines.props.sensitive = True

    def handle_show_hidden_lines_action_activate (self, action):

        self.logger.info ("restoring model filter to show all lines")
        self.log_filter = FilteredLogModel (self.log_model)
        self.log_view.props.model = self.log_filter
        self.actions.show_hidden_lines.props.sensitive = False

    def handle_edit_copy_line_action_activate (self, action):

        line_index = self.get_active_line_index ()
        line = self.log_file.get_full_line (line_index)
        self.logger.warning ("FIXME: This gets the wrong level; we still have the level in the model only (d'oh)")
        self.clipboard.set_text (line.line_string ())

    def handle_edit_copy_message_action_activate (self, action):

        col_id = LogModelBase.COL_MESSAGE
        self.clipboard.set_text (self.get_active_line ()[col_id])

    def handle_filter_out_higher_levels_action_activate (self, action):

        row = self.get_active_line ()
        debug_level = row[LogModelBase.COL_LEVEL]

        try:
            target_level = debug_level.higher_level ()
        except ValueError:
            return
        self.log_filter.add_filter (DebugLevelFilter (target_level))

        # FIXME:
        self.log_view.props.model = gtk.TreeStore (str)
        self.log_view.props.model = self.log_filter

    def handle_show_about_action_activate (self, action):

        from GstDebugViewer import version

        dialog = self.widget_factory.make_one ("about_dialog")
        dialog.props.version = version
        dialog.run ()
        dialog.destroy ()

    @staticmethod
    def _timestamp_cell_data_func (column, renderer, model, tree_iter):

        ts = model.get_value (tree_iter, LogModel.COL_TIME)
        renderer.props.text = Data.time_args (ts)

    def _message_cell_data_func (self, column, renderer, model, tree_iter):

        offset = model.get_value (tree_iter, LogModel.COL_MESSAGE_OFFSET)
        self.log_file.seek (offset)
        renderer.props.text = strip_escape (self.log_file.readline ().strip ())

    def set_log_file (self, filename):

        if self.log_file is not None:
            for feature in self.features:
                feature.handle_detach_log_file (self, self.log_file)

        if filename is None:
            if self.dispatcher is not None:
                self.dispatcher.cancel ()
            self.dispatcher = None
            self.log_file = None
            self.actions.groups["RowActions"].props.sensitive = False
        else:
            self.logger.debug ("setting log file %r", filename)

            try:
                self.log_model = LazyLogModel ()
                self.log_filter = FilteredLogModel (self.log_model)
                
                self.dispatcher = Common.Data.GSourceDispatcher ()
                self.log_file = Data.LogFile (filename, self.dispatcher)
            except EnvironmentError, exc:
                try:
                    file_size = os.path.getsize (filename)
                except EnvironmentError:
                    pass
                else:
                    if file_size == 0:
                        # Trying to mmap an empty file results in an invalid
                        # argument error.
                        self.show_error (_("Could not open file"),
                                         _("The selected file is empty"))
                        return
                self.handle_environment_error (exc, filename)
                return

            basename = os.path.basename (filename)
            self.gtk_window.props.title = _("%s - GStreamer Debug Viewer") % (basename,)

            self.log_file.consumers.append (self)
            self.log_file.start_loading ()

    def handle_environment_error (self, exc, filename):

        self.show_error (_("Could not open file"), str (exc))

    def show_error (self, message1, message2):

        dialog = gtk.MessageDialog (self.gtk_window, gtk.DIALOG_MODAL, gtk.MESSAGE_ERROR,
                                    gtk.BUTTONS_OK, message1)
        # The property for secondary text is new in 2.10, so we use this clunky
        # method instead.
        dialog.format_secondary_text (message2)
        dialog.set_default_response (0)
        dialog.run ()
        dialog.destroy ()

    def handle_log_view_button_press_event (self, view, event):

        if event.button != 3:
            return False

        self.view_popup.popup (None, None, None, event.button, event.get_time ())
        return True

    def handle_load_started (self):

        self.logger.debug ("load has started")

        widgets = self.widget_factory.make ("progress_dialog")
        dialog = widgets.progress_dialog
        dialog.connect ("response", self.handle_progress_dialog_response)
        self.progress_dialog = dialog
        self.progress_bar = widgets.progress_bar
        dialog.set_transient_for (self.gtk_window)
        dialog.show ()

        self.update_progress_id = gobject.timeout_add (250, self.update_load_progress)

    def handle_progress_dialog_response (self, dialog, response):

        self.actions.cancel_load.activate ()

    def update_load_progress (self):

        if not self.progress_bar:
            self.logger.debug ("progress window is gone, removing progress update timeout")
            self.update_progress_id = None
            return False

        progress = self.log_file.get_load_progress ()
        self.logger.debug ("update progress to %i%%", progress * 100)
        self.progress_bar.props.fraction = progress

        return True

    def handle_load_finished (self):

        self.logger.debug ("load has finshed")

        if self.update_progress_id is not None:
            gobject.source_remove (self.update_progress_id)
            self.update_progress_id = None

        self.progress_dialog.hide ()
        self.progress_dialog.destroy ()
        self.progress_dialog = None
        self.progress_bar = None

        self.log_model.set_log (self.log_file)

        self.log_filter.reset ()

        self.actions.reload_file.props.sensitive = True
        self.actions.groups["RowActions"].props.sensitive = True
        self.actions.show_hidden_lines.props.sensitive = False

        def idle_set ():
            ##self.log_view.props.model = self.log_model
            self.log_view.props.model = self.log_filter
            for feature in self.features:
                feature.handle_attach_log_file (self, self.log_file)
            return False

        gobject.idle_add (idle_set)

class AppStateSection (Common.GUI.StateSection):

    _name = "state"

    geometry = Common.GUI.StateInt4 ("window-geometry")
    maximized = Common.GUI.StateBool ("window-maximized")

    column_order = Common.GUI.StateItemList ("column-order", ViewColumnManager)
    columns_visible = Common.GUI.StateItemList ("columns-visible", ViewColumnManager)    

class AppState (Common.GUI.State):

    def __init__ (self, *a, **kw):

        Common.GUI.State.__init__ (self, *a, **kw)

        self.add_section_class (AppStateSection)

class App (object):

    def __init__ (self):

        self.attach ()

    def load_plugins (self):

        from GstDebugViewer import Plugins

        plugin_classes = list (Plugins.load ([os.path.dirname (Plugins.__file__)]))
        self.plugins = []
        for plugin_class in plugin_classes:
            plugin = plugin_class (self)
            self.plugins.append (plugin)

    def iter_plugin_features (self):

        for plugin in self.plugins:
            for feature in plugin.features:
                yield feature

    def attach (self):

        config_home = Common.utils.XDG.CONFIG_HOME

        state_filename = os.path.join (config_home, "gst-debug-viewer", "state")

        self.state = AppState (state_filename)
        self.state_section = self.state.sections["state"]

        self.load_plugins ()

        self.windows = [Window (self)]

    def detach (self):

        # TODO: If we take over deferred saving from the inspector, specify now
        # = True here!
        self.state.save ()

    def run (self):

        try:
            Common.Main.MainLoopWrapper (gtk.main, gtk.main_quit).run ()
        except:
            raise
        else:
            self.detach ()

    def close_window (self, window):

        # GtkTreeView takes some time to go down for large files.  Let's block
        # until the window is hidden:
        gobject.idle_add (gtk.main_quit)
        gtk.main ()
        
        gtk.main_quit ()

import time

class TestParsingPerformance (object):

    def __init__ (self, filename):

        self.main_loop = gobject.MainLoop ()
        self.log_file = Data.LogFile (filename, Common.Data.DefaultDispatcher ())
        self.log_file.consumers.append (self)

    def start (self):

        self.log_file.start_loading ()

    def handle_load_started (self):

        self.start_time = time.time ()

    def handle_load_finished (self):

        diff = time.time () - self.start_time
        print "line cache built in %0.1f ms" % (diff * 1000.,)

        start_time = time.time ()
        model = LazyLogModel (self.log_file)
        for row in model:
            pass
        diff = time.time () - start_time
        print "data parsed in %0.1f ms" % (diff * 1000.,)
        print "overall time spent: %0.1f s" % (time.time () - self.start_time,)

        import resource
        rusage = resource.getrusage (resource.RUSAGE_SELF)
        print "time spent in user mode: %.2f s" % (rusage.ru_utime,)
        print "time spent in system mode: %.2f s" % (rusage.ru_stime,)

def main (options):

    args = options["args"]

    if len (args) > 1 and args[0] == "benchmark":
        test = TestParsingPerformance (args[1])
        test.start ()
        return

    app = App ()

    # TODO: Once we support more than one window, open one window for each
    # supplied filename.
    window = app.windows[0]
    if len (args) > 0:
        window.set_log_file (args[0])

    app.run ()

if __name__ == "__main__":
    main ()
