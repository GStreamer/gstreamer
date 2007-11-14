#!/usr/bin/python
# -*- coding: utf-8; mode: python; -*-
##
## gst-debug-viewer.py: GStreamer debug log viewer
##
## Copyright (C) 2006 Rene Stadler <mail@renestadler.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU Lesser General Public
## License along with this library; if not, write to the Free
## Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
## Boston, MA 02110-1301 USA
##

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

import GstDebugViewer.Common.Data
import GstDebugViewer.Common.GUI
import GstDebugViewer.Common.Main
Common = GstDebugViewer.Common
from GstDebugViewer.Common import utils

from GstDebugViewer import Data, Main

class LogModelBase (gtk.GenericTreeModel):

    __metaclass__ = Common.GUI.MetaModel

    columns = ("COL_LEVEL", str, # FIXME: Use Data.DebugLevel instances/ints!
               "COL_PID", int,
               "COL_THREAD", gobject.TYPE_UINT64,
               "COL_TIME", gobject.TYPE_UINT64,
               "COL_CATEGORY", str,
               "COL_FILENAME", str,
               "COL_LINE", int,
               "COL_FUNCTION", str,
               "COL_OBJECT", str,
               "COL_MESSAGE", str,)

    def __init__ (self):

        gtk.GenericTreeModel.__init__ (self)

        ##self.props.leak_references = False

        self.line_offsets = []
        self.line_cache = {}

    def on_get_flags (self):

        flags = gtk.TREE_MODEL_LIST_ONLY | gtk.TREE_MODEL_ITERS_PERSIST

        return flags

    def on_get_n_columns (self):
        
        return len (self.column_types)

    def on_get_column_type (self, col_id):

        return self.column_types[col_id]
    
    def on_get_iter (self, path):

        if len (path) > 1:
            return None

        line_index = path[0]

        if line_index > len (self.line_offsets) - 1:
            return None

        return line_index

    def on_get_path (self, line_index):

        return (line_index,)

    def on_get_value (self, line_index, col_id):

        if line_index > len (self.line_offsets) - 1:
            return None

        line_offset = self.line_offsets[line_index]
        self.ensure_cached (line_offset)

        return self.line_cache[line_offset][col_id]

    def on_iter_next (self, line_index):

        if line_index >= len (self.line_offsets) - 1:
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

        if parent or n > len (self.line_offsets) - 1:
            return None
        else:
            return n ## self.line_offsets[n]

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

        self.__line_regex = Data.default_log_line_regex ()
        self.__line_match_order = (self.COL_TIME,
                                   self.COL_PID,
                                   self.COL_THREAD,
                                   self.COL_LEVEL, 
                                   self.COL_CATEGORY,
                                   self.COL_FILENAME,
                                   self.COL_LINE,
                                   self.COL_FUNCTION,
                                   self.COL_OBJECT,
                                   self.COL_MESSAGE,)
        if log_obj:
            self.set_log (log_obj)

    def set_log (self, log_obj):

        self.__fileobj = log_obj.fileobj

        self.line_cache.clear ()
        self.line_offsets = log_obj.line_cache.offsets

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

        ts_len = 17
        ts = Data.parse_time (line[:ts_len])
        match = self.__line_regex.match (line[ts_len:-len (os.linesep)])
        if match is None:
            # FIXME?
            groups = [ts, 0, 0, "?", "", "", 0, "", "", line[ts_len:-len (os.linesep)]]
        else:            
            groups = [ts] + list (match.groups ())

            # TODO: Figure out how much string interning can save here and how
            # much run time speed it costs!
            groups[1] = int (groups[1]) # pid
            groups[2] = int (groups[2], 16) # thread pointer
            groups[6] = int (groups[6]) # line
            groups[8] = groups[8] or "" # object (optional)

        groups = [x[1] for x in sorted (zip (self.__line_match_order,
                                             groups))]
        self.line_cache[line_offset] = groups

class FilteredLogModel (LogModelBase):

    def __init__ (self, lazy_log_model):

        LogModelBase.__init__ (self)

        self.parent_model = lazy_log_model
        self.ensure_cached = lazy_log_model.ensure_cached
        self.line_cache = lazy_log_model.line_cache

        self.line_offsets += lazy_log_model.line_offsets

# Sync with gst-inspector!
class Column (object):

    """A single list view column, managed by a ColumnManager instance."""

    name = None
    id = None
    label_header = None
    get_modify_func = None
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

    def __init__ (self):

        Column.__init__ (self)

        column = self.view_column
        cell = gtk.CellRendererText ()
        column.pack_start (cell)

        if not self.get_modify_func:
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

        cell = self.view_column.get_cells ()[0]

        if self.get_modify_func is not None:
            format = self.get_modify_func ()
        else:
            def identity (x):
                return x
            format = identity
        max_width = 0
        for value in values:
            cell.props.text = format (value)
            max_width = max (max_width, cell.get_size (view, None)[2])

        return max_width

    def get_values_for_size (self):

        return ()

class TimeColumn (TextColumn):

    name = "time"
    label_header = _("Time")
    id = LazyLogModel.COL_TIME

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

    @staticmethod
    def get_modify_func ():

        def format_level (value):
            if value is None:
                # FIXME: Should never be None!
                return ""
            return value[0]

        return format_level

    def get_values_for_size (self):

        values = ["LOG", "DEBUG", "INFO", "WARN", "ERROR"]

        return values

class PidColumn (TextColumn):

    name = "pid"
    label_header = _("PID")
    id = LazyLogModel.COL_PID

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

    @staticmethod
    def get_modify_func ():

        def format_thread (value):
            return "0x%07x" % (value,)

        return format_thread

    def get_values_for_size (self):

        # TODO: Same as for TimeColumn.  There is no guarantee that aaaaaaaa is
        # the widest string; use fixed font or come up with something better.

        return [int ("aaaaaaaaa", 16)]

class CategoryColumn (TextColumn):

    name = "category"
    label_header = _("Category")
    id = LazyLogModel.COL_CATEGORY

    def get_values_for_size (self):

        return ["GST_LONG_CATEGORY", "somelongelement"]

class FilenameColumn (TextColumn):

    name = "filename"
    label_header = _("Filename")
    id = LazyLogModel.COL_FILENAME

    def get_values_for_size (self):

        return ["gstsomefilename.c"]

class FunctionColumn (TextColumn):

    name = "function"
    label_header = _("Function")
    id = LazyLogModel.COL_FUNCTION

    def get_values_for_size (self):

        return ["gst_this_should_be_enough"]

## class FullCodeLocation (TextColumn):

##     name = "code-location"
##     label_header = _("Code Location")
##     id = LazyLogModel.COL_FILENAME

##     def get_values_for_size (self):

##         return ["gstwhateverfile.c:1234"]

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
                      FilenameColumn, FunctionColumn, ObjectColumn, MessageColumn,)

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

        self.sentinels = []

        self.progress_bar = None
        self.update_progress_id = None

        self.window_state = Common.GUI.WindowState ()
        self.column_manager = ViewColumnManager (app.state)

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
                            ("close-window", gtk.STOCK_CLOSE, _("Close _Window"), "<Ctrl>W"),
                            ("show-about", gtk.STOCK_ABOUT, None)])
        ## group.add_toggle_actions ([("show-line-density", None, _("Line _Density"), "<Ctrl>D")])
        self.actions.add_group (group)

        group = gtk.ActionGroup ("RowActions")
        group.add_actions ([("edit-copy-line", gtk.STOCK_COPY, _("Copy line"), "<Ctrl>C"),
                            ("edit-copy-message", gtk.STOCK_COPY, _("Copy message"))])
        self.actions.add_group (group)

        self.actions.add_group (self.column_manager.action_group)

        self.file = None
        self.log_model = LazyLogModel ()

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
        self.log_view.props.fixed_height_mode = True
        #self.log_view.props.model = self.log_model.filtered ()

        self.log_view.connect ("button-press-event", self.handle_log_view_button_press_event)

        self.attach ()
        self.column_manager.attach (self.log_view)

##         cell = gtk.CellRendererText ()
##         column = gtk.TreeViewColumn ("Level", cell,
##                                      text = self.log_model.COL_LEVEL)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         column.props.fixed_width = 80 # FIXME
##         self.log_view.append_column (column)

##         cell = gtk.CellRendererText ()
##         cell.props.family = "monospace"
##         cell.props.family_set = True
##         column = gtk.TreeViewColumn ("Time", cell)
##                                      #text = self.log_model.COL_TIME)
##         column.set_cell_data_func (cell, self._timestamp_cell_data_func)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         column.props.fixed_width = 180 # FIXME
##         self.log_view.append_column (column)

##         cell = gtk.CellRendererText ()
##         column = gtk.TreeViewColumn ("Category", cell,
##                                      text = self.log_model.COL_CATEGORY)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         column.props.fixed_width = 150 # FIXME
##         self.log_view.append_column (column)

##         cell = gtk.CellRendererText ()
##         column = gtk.TreeViewColumn ("Function", cell,
##                                      text = self.log_model.COL_FUNCTION)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         column.props.fixed_width = 180 # FIXME
##         self.log_view.append_column (column)

##         cell = gtk.CellRendererText ()
##         column = gtk.TreeViewColumn ("Object", cell,
##                                      text = self.log_model.COL_OBJECT)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         column.props.fixed_width = 150 # FIXME
##         self.log_view.append_column (column)

##         cell = gtk.CellRendererText ()
##         column = gtk.TreeViewColumn ("Message", cell, text = self.log_model.COL_MESSAGE)
##         ##column.set_cell_data_func (cell, self._message_cell_data_func)
##         column.props.sizing = gtk.TREE_VIEW_COLUMN_FIXED
##         self.log_view.append_column (column)

    def get_top_attach_point (self):

        return self.widgets.vbox_main

    def attach (self):

        self.window_state.attach (window = self.gtk_window, state = self.app.state)

        self.clipboard = gtk.Clipboard (self.gtk_window.get_display (),
                                        gtk.gdk.SELECTION_CLIPBOARD)

        for action_name in ("new-window", "open-file", "close-window",
                            "edit-copy-line", "edit-copy-message",
                            "show-about",):
            name = action_name.replace ("-", "_")
            action = getattr (self.actions, name)
            handler = getattr (self, "handle_%s_action_activate" % (name,))
            action.connect ("activate", handler)

        self.gtk_window.connect ("delete-event", self.handle_window_delete_event)

        self.features = []
        for plugin_feature in self.app.iter_plugin_features ():
            feature = plugin_feature ()
            feature.attach (self)
            self.features.append (feature)

    def detach (self):

        self.window_state.detach ()
        self.column_manager.detach ()

    def get_active_line (self):

        selection = self.log_view.get_selection ()
        model, tree_iter = selection.get_selected ()
        if tree_iter is None:
            raise ValueError ("no line selected")
        return self.log_model.get (tree_iter, *LazyLogModel.column_ids)

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
                                        (gtk.STOCK_CANCEL, 1,
                                         gtk.STOCK_OPEN, 0,))
        response = dialog.run ()
        dialog.hide ()
        if response == 0:
            self.set_log_file (dialog.get_filename ())
        dialog.destroy ()

    def handle_close_window_action_activate (self, action):

        self.close ()

    def handle_edit_copy_line_action_activate (self, action):

        self.logger.warning ("FIXME")
        return
        col_id = self.log_model.COL_
        self.clipboard.set_text (self.get_active_line ()[col_id])

    def handle_edit_copy_message_action_activate (self, action):

        col_id = self.log_model.COL_MESSAGE
        self.clipboard.set_text (self.get_active_line ()[col_id])

    def handle_show_about_action_activate (self, action):

        from GstDebugViewer import version

        dialog = self.widget_factory.make_one ("about_dialog")
        dialog.props.version = version
        dialog.run ()
        dialog.destroy ()

    @staticmethod
    def _timestamp_cell_data_func (column, renderer, model, tree_iter):

        ts = model.get (tree_iter, LogModel.COL_TIME)[0]
        renderer.props.text = Data.time_args (ts)

    def _message_cell_data_func (self, column, renderer, model, tree_iter):

        offset = model.get (tree_iter, LogModel.COL_MESSAGE_OFFSET)[0]
        self.log_file.seek (offset)
        renderer.props.text = strip_escape (self.log_file.readline ().strip ())

    def set_log_file (self, filename):

        self.logger.debug ("setting log file %r", filename)

        dispatcher = Common.Data.GSourceDispatcher ()
        self.log_file = Data.LogFile (filename, dispatcher)
        self.log_file.consumers.append (self)
        self.log_file.start_loading ()

    def handle_log_view_button_press_event (self, view, event):

        if event.button != 3:
            return False

        self.view_popup.popup (None, None, None, event.button, event.get_time ())
        return True

    def handle_load_started (self):

        self.logger.debug ("load has started")

        widgets = self.widget_factory.make ("progress_dialog")
        dialog = widgets.progress_dialog
        self.progress_dialog = dialog
        self.progress_bar = widgets.progress_bar
        dialog.set_transient_for (self.gtk_window)
        dialog.show ()

        self.update_progress_id = gobject.timeout_add (250, self.update_load_progress)

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

        for sentinel in self.sentinels:
            sentinel ()

        def idle_set ():
            #self.log_view.props.model = self.log_model
            self.log_view.props.model = FilteredLogModel (self.log_model)
            return False

        gobject.idle_add (idle_set)

class AppState (Common.GUI.AppState):

    geometry = Common.GUI.StateInt4 ("window-geometry")
    maximized = Common.GUI.StateBool ("window-maximized")

    column_order = Common.GUI.StateItemList ("column-order", ViewColumnManager)
    columns_visible = Common.GUI.StateItemList ("columns-visible", ViewColumnManager)

class App (object):

    def __init__ (self):

        self.load_plugins ()

        self.attach ()

    def load_plugins (self):

        from GstDebugViewer import Plugins

        self.plugins = list (Plugins.load ([os.path.dirname (Plugins.__file__)]))

    def iter_plugin_features (self):

        for plugin in self.plugins:
            for feature in plugin.features:
                yield feature

    def attach (self):

        state_filename = os.path.join (utils.XDG.CONFIG_HOME, "gst-debug-viewer", "state")

        self.state = AppState (state_filename)

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

        # For some reason, going down takes some time for large files.  Let's
        # block until the window is hidden:
        gobject.idle_add (gtk.main_quit)
        gtk.main ()
        
        gtk.main_quit ()

import time

class TestParsingPerformance (object):

    def __init__ (self, filename):

        self.main_loop = gobject.MainLoop ()
        self.log_file = Data.LogFile (filename, DefaultDispatcher ())
        self.log_file.consumers.append (self)

    def start (self):

        self.log_file.start_loading ()

    def handle_load_started (self):

        self.start_time = time.time ()

    def handle_load_finished (self):

        diff = time.time () - self.start_time
        print "line cache built in %0.1f ms" % (diff * 1000.,)

        self.start_time = time.time ()
        model = LazyLogModel (self.log_file)
        for row in model:
            pass
        diff = time.time () - self.start_time
        print "data parsed in %0.1f ms" % (diff * 1000.,)

def main ():

    if len (sys.argv) > 1 and sys.argv[1] == "--benchmark":
        test = TestParsingPerformance (sys.argv[2])
        test.start ()
        return

    app = App ()

    window = app.windows[0]
    if len (sys.argv) > 1:
        window.set_log_file (sys.argv[-1])

    app.run ()

if __name__ == "__main__":
    main ()
