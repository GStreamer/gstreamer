#!/usr/bin/env python
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

"""GStreamer Debug Viewer test suite for the custom tree models."""

import sys
import os
import os.path
from glob import glob

from unittest import TestCase, main as test_main

from .. import Common, Data
from .. GUI.filters import CategoryFilter, Filter
from .. GUI.models import (FilteredLogModel,
                           LogModelBase,
                           SubRange,)


class TestSubRange (TestCase):

    def test_len(self):

        values = list(range(20))

        sr = SubRange(values, 0, 20)
        self.assertEqual(len(sr), 20)

        sr = SubRange(values, 10, 20)
        self.assertEqual(len(sr), 10)

        sr = SubRange(values, 0, 10)
        self.assertEqual(len(sr), 10)

        sr = SubRange(values, 5, 15)
        self.assertEqual(len(sr), 10)

    def test_iter(self):

        values = list(range(20))

        sr = SubRange(values, 0, 20)
        self.assertEqual(list(sr), values)

        sr = SubRange(values, 10, 20)
        self.assertEqual(list(sr), list(range(10, 20)))

        sr = SubRange(values, 0, 10)
        self.assertEqual(list(sr), list(range(0, 10)))

        sr = SubRange(values, 5, 15)
        self.assertEqual(list(sr), list(range(5, 15)))


class Model (LogModelBase):

    def __init__(self):

        LogModelBase.__init__(self)

        for i in range(20):
            self.line_offsets.append(i * 100)
            self.line_levels.append(Data.debug_level_debug)

    def ensure_cached(self, line_offset):

        pid = line_offset // 100
        if pid % 2 == 0:
            category = b"EVEN"
        else:
            category = b"ODD"

        line_fmt = (b"0:00:00.000000000 %5i 0x0000000 DEBUG "
                    b"%20s dummy.c:1:dummy: dummy")
        line_str = line_fmt % (pid, category,)
        log_line = Data.LogLine.parse_full(line_str)
        self.line_cache[line_offset] = log_line

    def access_offset(self, line_offset):

        return ""


class IdentityFilter (Filter):

    def __init__(self):

        def filter_func(row):
            return True
        self.filter_func = filter_func


class RandomFilter (Filter):

    def __init__(self, seed):

        import random
        rand = random.Random()
        rand.seed(seed)

        def filter_func(row):
            return rand.choice((True, False,))
        self.filter_func = filter_func


class TestDynamicFilter (TestCase):

    def test_unset_filter_rerange(self):

        full_model = Model()
        filtered_model = FilteredLogModel(full_model)
        row_list = self.__row_list

        self.assertEqual(row_list(full_model), list(range(20)))
        self.assertEqual(row_list(filtered_model), list(range(20)))

        filtered_model.set_range(5, 16)

        self.assertEqual(row_list(filtered_model), list(range(5, 16)))

    def test_identity_filter_rerange(self):

        full_model = Model()
        filtered_model = FilteredLogModel(full_model)
        row_list = self.__row_list

        self.assertEqual(row_list(full_model), list(range(20)))
        self.assertEqual(row_list(filtered_model), list(range(20)))

        filtered_model.add_filter(IdentityFilter(),
                                  Common.Data.DefaultDispatcher())
        filtered_model.set_range(5, 16)

        self.assertEqual(row_list(filtered_model), list(range(5, 16)))

    def test_filtered_range_refilter_skip(self):

        full_model = Model()
        filtered_model = FilteredLogModel(full_model)

        row_list = self.__row_list

        filtered_model.add_filter(CategoryFilter("EVEN"),
                                  Common.Data.DefaultDispatcher())
        self.__dump_model(filtered_model, "filtered")

        self.assertEqual(row_list(filtered_model), list(range(1, 20, 2)))
        self.assertEqual([filtered_model.line_index_from_super(i)
                          for i in range(1, 20, 2)],
                         list(range(10)))
        self.assertEqual([filtered_model.line_index_to_super(i)
                          for i in range(10)],
                         list(range(1, 20, 2)))

        filtered_model.set_range(1, 20)
        self.__dump_model(filtered_model, "ranged (1, 20)")
        self.__dump_model(filtered_model, "filtered range")

        self.assertEqual([filtered_model.line_index_from_super(i)
                          for i in range(0, 19, 2)],
                         list(range(10)))
        self.assertEqual([filtered_model.line_index_to_super(i)
                          for i in range(10)],
                         list(range(1, 20, 2)))

        filtered_model.set_range(2, 20)
        self.__dump_model(filtered_model, "ranged (2, 20)")

        self.assertEqual(row_list(filtered_model), list(range(3, 20, 2)))

    def test_filtered_range_refilter(self):

        full_model = Model()
        filtered_model = FilteredLogModel(full_model)

        row_list = self.__row_list
        rows = row_list(full_model)
        rows_filtered = row_list(filtered_model)

        self.__dump_model(full_model, "full model")

        self.assertEqual(rows, rows_filtered)

        self.assertEqual([filtered_model.line_index_from_super(i)
                          for i in range(20)],
                         list(range(20)))
        self.assertEqual([filtered_model.line_index_to_super(i)
                          for i in range(20)],
                         list(range(20)))

        filtered_model.set_range(5, 16)
        self.__dump_model(filtered_model, "ranged model (5, 16)")

        rows_ranged = row_list(filtered_model)
        self.assertEqual(rows_ranged, list(range(5, 16)))

        self.__dump_model(filtered_model, "filtered model (nofilter, 5, 15)")

        filtered_model.add_filter(CategoryFilter("EVEN"),
                                  Common.Data.DefaultDispatcher())
        rows_filtered = row_list(filtered_model)
        self.assertEqual(rows_filtered, list(range(5, 16, 2)))

        self.__dump_model(filtered_model, "filtered model")

    def test_random_filtered_range_refilter(self):

        full_model = Model()
        filtered_model = FilteredLogModel(full_model)
        row_list = self.__row_list

        self.assertEqual(row_list(full_model), list(range(20)))
        self.assertEqual(row_list(filtered_model), list(range(20)))

        filtered_model.add_filter(RandomFilter(538295943),
                                  Common.Data.DefaultDispatcher())
        random_rows = row_list(filtered_model)

        self.__dump_model(filtered_model)

        filtered_model = FilteredLogModel(full_model)
        filtered_model.add_filter(RandomFilter(538295943),
                                  Common.Data.DefaultDispatcher())
        self.__dump_model(filtered_model, "filtered model")
        self.assertEqual(row_list(filtered_model), random_rows)

        filtered_model.set_range(1, 10)
        self.__dump_model(filtered_model)
        self.assertEqual(row_list(filtered_model), [
                         x for x in range(0, 10) if x in random_rows])

    def __row_list(self, model):

        return [row[Model.COL_PID] for row in model]

    def __dump_model(self, model, comment=None):

        # TODO: Provide a command line option to turn this on and off.

        return

        if not hasattr(model, "super_model"):
            # Top model.
            print("\t(%s)" % ("|".join([str(i).rjust(2)
                                        for i in self.__row_list(model)]),), end=' ')
        else:
            top_model = model.super_model
            if hasattr(top_model, "super_model"):
                top_model = top_model.super_model
            top_indices = self.__row_list(top_model)
            positions = self.__row_list(model)
            output = ["  "] * len(top_indices)
            for i, position in enumerate(positions):
                output[position] = str(i).rjust(2)
            print("\t(%s)" % ("|".join(output),), end=' ')

        if comment is None:
            print()
        else:
            print(comment)


if __name__ == "__main__":
    test_main()
