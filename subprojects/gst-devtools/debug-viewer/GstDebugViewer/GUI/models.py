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

from array import array
from bisect import bisect_left
import logging

from gi.repository import GObject
from gi.repository import Gtk

from GstDebugViewer import Common, Data


class LogModelBase (Common.GUI.GenericTreeModel, metaclass=Common.GUI.MetaModel):

    columns = ("COL_TIME", GObject.TYPE_UINT64,
               "COL_PID", int,
               "COL_THREAD", GObject.TYPE_UINT64,
               "COL_LEVEL", object,
               "COL_CATEGORY", str,
               "COL_FILENAME", str,
               "COL_LINE_NUMBER", int,
               "COL_FUNCTION", str,
               "COL_OBJECT", str,
               "COL_MESSAGE", str,)

    def __init__(self):

        Common.GUI.GenericTreeModel.__init__(self)

        # self.props.leak_references = False

        self.line_offsets = array("I")
        self.line_levels = []  # FIXME: Not so nice!
        self.line_cache = {}

    def ensure_cached(self, line_offset):

        raise NotImplementedError("derived classes must override this method")

    def access_offset(self, offset):

        raise NotImplementedError("derived classes must override this method")

    def iter_rows_offset(self):

        ensure_cached = self.ensure_cached
        line_cache = self.line_cache
        line_levels = self.line_levels
        COL_LEVEL = self.COL_LEVEL
        COL_MESSAGE = self.COL_MESSAGE
        access_offset = self.access_offset

        for i, offset in enumerate(self.line_offsets):
            ensure_cached(offset)
            row = line_cache[offset]
            # adjust special rows
            row[COL_LEVEL] = line_levels[i]
            msg_offset = row[COL_MESSAGE]
            row[COL_MESSAGE] = access_offset(offset + msg_offset)
            yield (row, offset,)
            row[COL_MESSAGE] = msg_offset

    def on_get_flags(self):

        flags = Gtk.TreeModelFlags.LIST_ONLY | Gtk.TreeModelFlags.ITERS_PERSIST

        return flags

    def on_get_n_columns(self):

        return len(self.column_types)

    def on_get_column_type(self, col_id):

        return self.column_types[col_id]

    def on_get_iter(self, path):

        if not path:
            return

        if len(path) > 1:
            # Flat model.
            return None

        line_index = path[0]

        if line_index > len(self.line_offsets) - 1:
            return None

        return line_index

    def on_get_path(self, rowref):

        line_index = rowref

        return (line_index,)

    def on_get_value(self, line_index, col_id):

        last_index = len(self.line_offsets) - 1

        if line_index > last_index:
            return None

        if col_id == self.COL_LEVEL:
            return self.line_levels[line_index]

        line_offset = self.line_offsets[line_index]
        self.ensure_cached(line_offset)

        value = self.line_cache[line_offset][col_id]
        if col_id == self.COL_MESSAGE:
            # strip whitespace + newline
            value = self.access_offset(line_offset + value).strip()
        elif col_id in (self.COL_TIME, self.COL_THREAD):
            value = GObject.Value(GObject.TYPE_UINT64, value)

        return value

    def get_value_range(self, col_id, start, stop):

        if col_id != self.COL_LEVEL:
            raise NotImplementedError("XXX FIXME")

        return self.line_levels[start:stop]

    def on_iter_next(self, line_index):

        last_index = len(self.line_offsets) - 1

        if line_index >= last_index:
            return None
        else:
            return line_index + 1

    def on_iter_children(self, parent):

        return self.on_iter_nth_child(parent, 0)

    def on_iter_has_child(self, rowref):

        return False

    def on_iter_n_children(self, rowref):

        if rowref is not None:
            return 0

        return len(self.line_offsets)

    def on_iter_nth_child(self, parent, n):

        last_index = len(self.line_offsets) - 1

        if parent or n > last_index:
            return None

        return n

    def on_iter_parent(self, child):

        return None

    # def on_ref_node (self, rowref):

    # pass

    # def on_unref_node (self, rowref):

    # pass


class LazyLogModel (LogModelBase):

    def __init__(self, log_obj=None):

        LogModelBase.__init__(self)

        self.__log_obj = log_obj

        if log_obj:
            self.set_log(log_obj)

    def set_log(self, log_obj):

        self.__fileobj = log_obj.fileobj

        self.line_cache.clear()
        self.line_offsets = log_obj.line_cache.offsets
        self.line_levels = log_obj.line_cache.levels

    def access_offset(self, offset):

        # TODO: Implement using one slice access instead of seek+readline.
        self.__fileobj.seek(offset)
        return self.__fileobj.readline()

    def ensure_cached(self, line_offset):

        if line_offset in self.line_cache:
            return

        if len(self.line_cache) > 10000:
            self.line_cache.clear()

        self.__fileobj.seek(line_offset)
        line = self.__fileobj.readline()

        self.line_cache[line_offset] = Data.LogLine.parse_full(line)


class FilteredLogModelBase (LogModelBase):

    def __init__(self, super_model):

        LogModelBase.__init__(self)

        self.logger = logging.getLogger("filter-model-base")

        self.super_model = super_model
        self.access_offset = super_model.access_offset
        self.ensure_cached = super_model.ensure_cached
        self.line_cache = super_model.line_cache

    def line_index_to_super(self, line_index):

        raise NotImplementedError("index conversion not supported")

    def line_index_from_super(self, super_line_index):

        raise NotImplementedError("index conversion not supported")


class FilteredLogModel (FilteredLogModelBase):

    def __init__(self, super_model):

        FilteredLogModelBase.__init__(self, super_model)

        self.logger = logging.getLogger("filtered-log-model")

        self.filters = []
        self.reset()
        self.__active_process = None
        self.__filter_progress = 0.

    def reset(self):

        self.logger.debug("reset filter")

        self.line_offsets = self.super_model.line_offsets
        self.line_levels = self.super_model.line_levels
        self.super_index = range(len(self.line_offsets))

        del self.filters[:]

    def __filter_process(self, filter):

        YIELD_LIMIT = 10000

        self.logger.debug("preparing new filter")
        new_line_offsets = array("I")
        new_line_levels = []
        new_super_index = array("I")
        level_id = self.COL_LEVEL
        func = filter.filter_func

        def enum():
            i = 0
            for row, offset in self.iter_rows_offset():
                line_index = self.super_index[i]
                yield (line_index, row, offset,)
                i += 1
        self.logger.debug("running filter")
        progress = 0.
        progress_full = float(len(self))
        y = YIELD_LIMIT
        for i, row, offset in enum():
            if func(row):
                new_line_offsets.append(offset)
                new_line_levels.append(row[level_id])
                new_super_index.append(i)
            y -= 1
            if y == 0:
                progress += float(YIELD_LIMIT)
                self.__filter_progress = progress / progress_full
                y = YIELD_LIMIT
                yield True
        self.line_offsets = new_line_offsets
        self.line_levels = new_line_levels
        self.super_index = new_super_index
        self.logger.debug("filtering finished")

        self.__filter_progress = 1.
        self.__handle_filter_process_finished()
        yield False

    def add_filter(self, filter, dispatcher):

        if self.__active_process is not None:
            raise ValueError("dispatched a filter process already")

        self.logger.debug("adding filter")

        self.filters.append(filter)

        self.__dispatcher = dispatcher
        self.__active_process = self.__filter_process(filter)
        dispatcher(self.__active_process)

    def abort_process(self):

        if self.__active_process is None:
            raise ValueError("no filter process running")

        self.__dispatcher.cancel()
        self.__active_process = None
        self.__dispatcher = None

        del self.filters[-1]

    def get_filter_progress(self):

        if self.__active_process is None:
            raise ValueError("no filter process running")

        return self.__filter_progress

    def __handle_filter_process_finished(self):

        self.__active_process = None
        self.handle_process_finished()

    def handle_process_finished(self):

        pass

    def line_index_from_super(self, super_line_index):

        return bisect_left(self.super_index, super_line_index)

    def line_index_to_super(self, line_index):

        return self.super_index[line_index]

    def set_range(self, super_start, super_stop):

        old_super_start = self.line_index_to_super(0)
        old_super_stop = self.line_index_to_super(
            len(self.super_index) - 1) + 1

        self.logger.debug("set range (%i, %i), current (%i, %i)",
                          super_start, super_stop, old_super_start, old_super_stop)

        if len(self.filters) == 0:
            # Identity.
            self.super_index = range(super_start, super_stop)
            self.line_offsets = SubRange(self.super_model.line_offsets,
                                         super_start, super_stop)
            self.line_levels = SubRange(self.super_model.line_levels,
                                        super_start, super_stop)
            return

        if super_start < old_super_start:
            # TODO:
            raise NotImplementedError("Only handling further restriction of the range"
                                      " (start offset = %i)" % (super_start,))

        if super_stop > old_super_stop:
            # TODO:
            raise NotImplementedError("Only handling further restriction of the range"
                                      " (end offset = %i)" % (super_stop,))

        start = self.line_index_from_super(super_start)
        stop = self.line_index_from_super(super_stop)

        self.super_index = SubRange(self.super_index, start, stop)
        self.line_offsets = SubRange(self.line_offsets, start, stop)
        self.line_levels = SubRange(self.line_levels, start, stop)


class SubRange (object):

    __slots__ = ("size", "start", "stop",)

    def __init__(self, size, start, stop):

        if start > stop:
            raise ValueError(
                "need start <= stop (got %r, %r)" % (start, stop,))

        if isinstance(size, type(self)):
            # Another SubRange, don't stack:
            start += size.start
            stop += size.start
            size = size.size

        self.size = size
        self.start = start
        self.stop = stop

    def __getitem__(self, i):

        if isinstance(i, slice):
            stop = i.stop
            if stop >= 0:
                stop += self.start
            else:
                stop += self.stop

            return self.size[i.start + self.start:stop]
        else:
            return self.size[i + self.start]

    def __len__(self):

        return self.stop - self.start

    def __iter__(self):

        size = self.size
        for i in range(self.start, self.stop):
            yield size[i]


class LineViewLogModel (FilteredLogModelBase):

    def __init__(self, super_model):

        FilteredLogModelBase.__init__(self, super_model)

        self.line_offsets = []
        self.line_levels = []

        self.parent_indices = []

    def reset(self):

        del self.line_offsets[:]
        del self.line_levels[:]

    def line_index_to_super(self, line_index):

        return self.parent_indices[line_index]

    def insert_line(self, position, super_line_index):

        if position == -1:
            position = len(self.line_offsets)
        li = super_line_index
        self.line_offsets.insert(position, self.super_model.line_offsets[li])
        self.line_levels.insert(position, self.super_model.line_levels[li])
        self.parent_indices.insert(position, super_line_index)

        path = (position,)
        tree_iter = self.get_iter(path)
        self.row_inserted(path, tree_iter)

    def replace_line(self, line_index, super_line_index):

        li = line_index
        self.line_offsets[li] = self.super_model.line_offsets[super_line_index]
        self.line_levels[li] = self.super_model.line_levels[super_line_index]
        self.parent_indices[li] = super_line_index

        path = (line_index,)
        tree_iter = self.get_iter(path)
        self.row_changed(path, tree_iter)

    def remove_line(self, line_index):

        for l in (self.line_offsets,
                  self.line_levels,
                  self.parent_indices,):
            del l[line_index]

        path = (line_index,)
        self.row_deleted(path)
