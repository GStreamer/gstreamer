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

from bisect import bisect_left
import logging

import gobject
import gtk

from GstDebugViewer import Common, Data

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

        ensure_cached = self.ensure_cached
        line_cache = self.line_cache
        line_levels = self.line_levels
        COL_LEVEL = self.COL_LEVEL

        for i, offset in enumerate (self.line_offsets):
            ensure_cached (offset)
            row = line_cache[offset]
            row[COL_LEVEL] = line_levels[i] # FIXME
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

        # TODO: Implement using one slice access instead of seek+readline.
        self.__fileobj.seek (offset)
        return self.__fileobj.readline ()

    def ensure_cached (self, line_offset):

        if line_offset in self.line_cache:
            return

        if len (self.line_cache) > 10000:
            self.line_cache.clear ()

        self.__fileobj.seek (line_offset)
        line = self.__fileobj.readline ()

        self.line_cache[line_offset] = Data.LogLine.parse_full (line)

class FilteredLogModelBase (LogModelBase):

    def __init__ (self, super_model):

        LogModelBase.__init__ (self)

        self.logger = logging.getLogger ("filter-model-base")

        self.super_model = super_model
        self.access_offset = super_model.access_offset
        self.ensure_cached = super_model.ensure_cached
        self.line_cache = super_model.line_cache

    def line_index_to_super (self, line_index):

        raise NotImplementedError ("index conversion not supported")

    def line_index_from_super (self, super_line_index):

        raise NotImplementedError ("index conversion not supported")

    def line_index_to_top (self, line_index):

        _log_indices = [line_index]

        super_index = line_index
        for model in self._iter_hierarchy ():
            super_index = model.line_index_to_super (super_index)
            _log_indices.append (super_index)

        _log_trans = " -> ".join ([str (x) for x in _log_indices])
        self.logger.debug ("translated index to top: %s", _log_trans)

        return super_index

    def line_index_from_top (self, super_index):

        _log_indices = [super_index]

        line_index = super_index
        for model in reversed (list (self._iter_hierarchy ())):
            line_index = model.line_index_from_super (line_index)
            _log_indices.append (line_index)

        _log_trans = " -> ".join ([str (x) for x in _log_indices])
        self.logger.debug ("translated index from top: %s", _log_trans)

        return line_index

    def super_model_changed (self):

        pass

    def _iter_hierarchy (self):

        model = self
        while hasattr (model, "super_model") and model.super_model:
            yield model
            model = model.super_model

class FilteredLogModelIdentity (FilteredLogModelBase):

    def __init__ (self, super_model):

        FilteredLogModelBase.__init__ (self, super_model)

        self.line_offsets = self.super_model.line_offsets
        self.line_levels = self.super_model.line_levels

    def line_index_from_super (self, super_line_index):

        return super_line_index

    def line_index_to_super (self, line_index):

        return line_index

class FilteredLogModel (FilteredLogModelBase):

    def __init__ (self, super_model):

        FilteredLogModelBase.__init__ (self, super_model)

        self.logger = logging.getLogger ("filtered-log-model")

        self.filters = []
        self.super_index = []
        self.from_super_index = {}
        self.reset ()
        self.__active_process = None
        self.__filter_progress = 0.
        self.__old_super_model_range = super_model.line_index_range

    def reset (self):

        del self.line_offsets[:]
        self.line_offsets += self.super_model.line_offsets
        del self.line_levels[:]
        self.line_levels += self.super_model.line_levels

        del self.super_index[:]
        self.from_super_index.clear ()

        del self.filters[:]

    def __filter_process (self, filter):

        YIELD_LIMIT = 10000

        self.logger.debug ("preparing new filter")
        ## del self.line_offsets[:]
        ## del self.line_levels[:]
        new_line_offsets = []
        new_line_levels = []
        new_super_index = []
        new_from_super_index = {}
        level_id = self.COL_LEVEL
        func = filter.filter_func
        if len (self.filters) == 1:
            # This is the first filter that gets applied.
            def enum ():
                i = 0
                for row, offset in self.iter_rows_offset ():
                    yield (i, row, offset,)
                    i += 1
        else:
            def enum ():
                i = 0
                for row, offset in self.iter_rows_offset ():
                    line_index = self.super_index[i]
                    yield (line_index, row, offset,)
                    i += 1
        self.logger.debug ("running filter")
        progress = 0.
        progress_full = float (len (self))
        y = YIELD_LIMIT
        for i, row, offset in enum ():
            if func (row):
                new_line_offsets.append (offset)
                new_line_levels.append (row[level_id])
                new_super_index.append (i)
                new_from_super_index[i] = len (new_super_index) - 1
            y -= 1
            if y == 0:
                progress += float (YIELD_LIMIT)
                self.__filter_progress = progress / progress_full
                y = YIELD_LIMIT
                yield True
        self.line_offsets = new_line_offsets
        self.line_levels = new_line_levels
        self.super_index = new_super_index
        self.from_super_index = new_from_super_index
        self.logger.debug ("filtering finished")

        self.__filter_progress = 1.
        self.__handle_filter_process_finished ()
        yield False

    def add_filter (self, filter, dispatcher):

        if self.__active_process is not None:
            raise ValueError ("dispatched a filter process already")

        self.filters.append (filter)

        self.__dispatcher = dispatcher
        self.__active_process = self.__filter_process (filter)
        dispatcher (self.__active_process)

    def abort_process (self):

        if self.__active_process is None:
            raise ValueError ("no filter process running")

        self.__dispatcher.cancel ()
        self.__active_process = None
        self.__dispatcher = None

        del self.filters[-1]

    def get_filter_progress (self):

        if self.__active_process is None:
            raise ValueError ("no filter process running")

        return self.__filter_progress

    def __handle_filter_process_finished (self):

        self.__active_process = None
        self.handle_process_finished ()

    def handle_process_finished (self):

        pass

    def line_index_from_super (self, super_line_index):

        if len (self.filters) == 0:
            # Identity.
            return super_line_index

        try:
            return self.from_super_index[super_line_index]
        except KeyError:
            raise IndexError ("super index %i not handled" % (super_line_index,))

    def line_index_to_super (self, line_index):

        if len (self.filters) == 0:
            # Identity.
            return line_index

        return self.super_index[line_index]

    def __filtered_indices_in_range (self, start, stop):

        if start < 0:
            raise ValueError ("start cannot be negative (got %r)" % (start,))

        super_start = bisect_left (self.super_index, start)
        super_stop = bisect_left (self.super_index, stop)

        return super_stop - super_start

    def super_model_changed_range (self):

        range_model = self.super_model
        old_start, old_stop = self.__old_super_model_range
        super_start, super_stop = range_model.line_index_range

        super_start_offset = super_start - old_start
        if super_start_offset < 0:
            # TODO:
            raise NotImplementedError ("Only handling further restriction of the range"
                                       " (start offset = %i)" % (super_start_offset,))

        super_end_offset = super_stop - old_stop
        if super_end_offset > 0:
            # TODO:
            raise NotImplementedError ("Only handling further restriction of the range"
                                       " (end offset = %i)" % (super_end_offset,))

        if super_end_offset < 0:
            if not self.super_index:
                # Identity; there are no filters.
                end_offset = len (self.line_offsets) + super_end_offset
            else:
                n_filtered = self.__filtered_indices_in_range (super_stop - super_start,
                                                               old_stop - super_start)
                end_offset = len (self.line_offsets) - n_filtered
            stop = len (self.line_offsets) # FIXME?
            assert end_offset < stop

            self.__remove_range (end_offset, stop)

        if super_start_offset > 0:
            if not self.super_index:
                # Identity; there are no filters.
                n_filtered = super_start_offset
                start_offset = n_filtered
            else:
                n_filtered = self.__filtered_indices_in_range (0, super_start_offset)
                start_offset = n_filtered

            if n_filtered > 0:
                self.__remove_range (0, start_offset)

            from_super = self.from_super_index
            for i in self.super_index:
                old_index = from_super[i]
                del from_super[i]
                from_super[i - super_start_offset] = old_index - start_offset

            for i in range (len (self.super_index)):
                self.super_index[i] -= super_start_offset

        self.__old_super_model_range = (super_start, super_stop,)

    def __remove_range (self, start, stop):

        if start < 0:
            raise ValueError ("start cannot be negative (got %r)" % (start,))
        if start == stop:
            return
        if stop > len (self.line_offsets):
            raise ValueError ("stop value out of range (got %r)" % (stop,))
        if start > stop:
            raise ValueError ("start cannot be greater than stop (got %r, %r)" % (start, stop,))

        self.logger.debug ("removing line range (%i, %i)",
                           start, stop)

        del self.line_offsets[start:stop]
        del self.line_levels[start:stop]
        for super_index in self.super_index[start:stop]:
            del self.from_super_index[super_index]
        del self.super_index[start:stop]

class SubRange (object):

    __slots__ = ("l", "start", "stop",)

    def __init__ (self, l, start, stop):

        if start > stop:
            raise ValueError ("need start <= stop (got %r, %r)" % (start, stop,))

        self.l = l
        self.start = start
        self.stop = stop

    def __getitem__ (self, i):

        return self.l[i + self.start]

    def __len__ (self):

        return self.stop - self.start

    def __iter__ (self):

        l = self.l
        for i in xrange (self.start, self.stop):
            yield l[i]

class RangeFilteredLogModel (FilteredLogModelBase):

    def __init__ (self, super_model):

        FilteredLogModelBase.__init__ (self, super_model)

        self.logger = logging.getLogger ("range-filtered-model")

        self.line_index_range = None

    def set_range (self, start_index, stop_index):

        self.logger.debug ("setting range to start = %i, stop = %i",
                           start_index, stop_index)

        self.line_index_range = (start_index, stop_index,)
        self.line_offsets = SubRange (self.super_model.line_offsets,
                                      start_index, stop_index)
        self.line_levels = SubRange (self.super_model.line_levels,
                                     start_index, stop_index)

    def reset (self):

        self.logger.debug ("reset")

        start_index = 0
        stop_index = len (self.super_model)

        self.set_range (start_index, stop_index,)

    def line_index_to_super (self, line_index):

        start_index = self.line_index_range[0]

        return line_index + start_index

    def line_index_from_super (self, li):

        start, stop = self.line_index_range

        if li < start or li >= stop:
            raise IndexError ("not in range")

        return li - start

class LineViewLogModel (FilteredLogModelBase):

    def __init__ (self, super_model):

        FilteredLogModelBase.__init__ (self, super_model)

        self.line_offsets = []
        self.line_levels = []

        self.parent_indices = []

    def reset (self):

        del self.line_offsets[:]
        del self.line_levels[:]

    def line_index_to_super (self, line_index):

        return self.parent_indices[line_index]

    def insert_line (self, position, super_line_index):

        if position == -1:
            position = len (self.line_offsets)
        li = super_line_index
        self.line_offsets.insert (position, self.super_model.line_offsets[li])
        self.line_levels.insert (position, self.super_model.line_levels[li])
        self.parent_indices.insert (position, super_line_index)

        path = (position,)
        tree_iter = self.get_iter (path)
        self.row_inserted (path, tree_iter)

    def replace_line (self, line_index, super_line_index):

        li = line_index
        self.line_offsets[li] = self.super_model.line_offsets[super_line_index]
        self.line_levels[li] = self.super_model.line_levels[super_line_index]
        self.parent_indices[li] = super_line_index

        path = (line_index,)
        tree_iter = self.get_iter (path)
        self.row_changed (path, tree_iter)

    def remove_line (self, line_index):

        for l in (self.line_offsets,
                  self.line_levels,
                  self.parent_indices,):
            del l[line_index]

        path = (line_index,)
        self.row_deleted (path)

