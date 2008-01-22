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

sys.path.insert (0, os.path.join (sys.path[0], os.pardir))

from unittest import TestCase, main as test_main

from GstDebugViewer import Common, Data, GUI

class TestSubRange (TestCase):

    def test_len (self):

        l = range (20)

        sr = GUI.SubRange (l, 0, 20)
        self.assertEquals (len (sr), 20)

        sr = GUI.SubRange (l, 10, 20)
        self.assertEquals (len (sr), 10)

        sr = GUI.SubRange (l, 0, 10)
        self.assertEquals (len (sr), 10)

        sr = GUI.SubRange (l, 5, 15)
        self.assertEquals (len (sr), 10)

    def test_iter (self):

        l = range (20)

        sr = GUI.SubRange (l, 0, 20)
        self.assertEquals (list (sr), l)

        sr = GUI.SubRange (l, 10, 20)
        self.assertEquals (list (sr), range (10, 20))

        sr = GUI.SubRange (l, 0, 10)
        self.assertEquals (list (sr), range (0, 10))

        sr = GUI.SubRange (l, 5, 15)
        self.assertEquals (list (sr), range (5, 15))

class Model (GUI.LogModelBase):

    def __init__ (self):

        GUI.LogModelBase.__init__ (self)

        for i in range (20):
            self.line_offsets.append (i * 100)
            self.line_levels.append (Data.debug_level_debug)

    def ensure_cached (self, line_offset):

        pid = line_offset // 100
        if pid % 2 == 0:
            category = "EVEN"
        else:
            category = "ODD"

        line_fmt = ("0:00:00.000000000 %5i 0x0000000 DEBUG "
                    "%20s dummy.c:1:dummy: dummy")
        line_str = line_fmt % (pid, category,)
        log_line = Data.LogLine.parse_full (line_str)
        self.line_cache[line_offset] = log_line

    def access_offset (self, line_offset):

        return ""

class IdentityFilter (GUI.Filter):

    def __init__ (self):

        def filter_func (row):
            return True
        self.filter_func = filter_func

class RandomFilter (GUI.Filter):

    def __init__ (self, seed):

        import random
        rand = random.Random ()
        rand.seed (seed)
        def filter_func (row):
            return rand.choice ((True, False,))
        self.filter_func = filter_func

class TestDynamicFilter (TestCase):

    def test_unset_filter_rerange (self):

        full_model = Model ()
        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)
        row_list = self.__row_list

        self.assertEquals (row_list (full_model), range (20))
        self.assertEquals (row_list (ranged_model), range (20))
        self.assertEquals (row_list (filtered_model), range (20))

        ranged_model.set_range (5, 16)
        filtered_model.super_model_changed_range ()

        self.assertEquals (row_list (ranged_model), range (5, 16))
        self.assertEquals (row_list (filtered_model), range (5, 16))

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (5, 16)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (11)],
                           range (5, 16))

    def test_identity_filter_rerange (self):

        full_model = Model ()
        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)
        row_list = self.__row_list

        self.assertEquals (row_list (full_model), range (20))
        self.assertEquals (row_list (ranged_model), range (20))
        self.assertEquals (row_list (filtered_model), range (20))

        filtered_model.add_filter (IdentityFilter (),
                                   Common.Data.DefaultDispatcher ())
        ranged_model.set_range (5, 16)
        filtered_model.super_model_changed_range ()

        self.assertEquals (row_list (ranged_model), range (5, 16))
        self.assertEquals (row_list (filtered_model), range (5, 16))

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (5, 16)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (11)],
                           range (5, 16))

    def test_filtered_range_refilter_skip (self):

        full_model = Model ()
        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)

        row_list = self.__row_list

        filtered_model.add_filter (GUI.CategoryFilter ("EVEN"),
                                   Common.Data.DefaultDispatcher ())
        self.__dump_model (filtered_model, "filtered")

        self.assertEquals (row_list (filtered_model), range (1, 20, 2))
        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (1, 20, 2)],
                           range (10))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (10)],
                           range (1, 20, 2))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (1, 20, 2)],
                           range (10))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (10)],
                           range (1, 20, 2))

        ranged_model.set_range (1, 20)
        self.__dump_model (ranged_model, "ranged (1, 20)")
        filtered_model.super_model_changed_range ()
        self.__dump_model (filtered_model, "filtered range")

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (0, 19, 2)],
                           range (10))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (10)],
                           range (0, 19, 2))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (1, 20, 2)],
                           range (10))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (10)],
                           range (1, 20, 2))

        ranged_model.set_range (2, 20)
        self.__dump_model (ranged_model, "ranged (2, 20)")
        filtered_model.super_model_changed_range ()
        self.__dump_model (filtered_model, "filtered range")

        self.assertEquals (row_list (filtered_model), range (3, 20, 2))
        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (1, 18, 2)],
                           range (9))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (9)],
                           range (1, 18, 2))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (3, 20, 2)],
                           range (9))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (9)],
                           range (3, 20, 2))

    def test_filtered_range_refilter (self):

        full_model = Model ()
        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)

        row_list = self.__row_list
        rows = row_list (full_model)
        rows_ranged = row_list (ranged_model)
        rows_filtered = row_list (filtered_model)

        self.__dump_model (full_model, "full model")
        ## self.__dump_model (ranged_model, "ranged model")
        ## self.__dump_model (filtered_model, "filtered model")

        self.assertEquals (rows, rows_ranged)
        self.assertEquals (rows, rows_filtered)

        self.assertEquals ([ranged_model.line_index_from_super (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([ranged_model.line_index_to_super (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([ranged_model.line_index_from_top (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([ranged_model.line_index_to_top (i)
                            for i in range (20)],
                           range (20))

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (20)],
                           range (20))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (20)],
                           range (20))

        ranged_model.set_range (5, 16)
        self.__dump_model (ranged_model, "ranged model (5, 16)")
        filtered_model.super_model_changed_range ()

        rows_ranged = row_list (ranged_model)
        self.assertEquals (rows_ranged, range (5, 16))

        self.__dump_model (filtered_model, "filtered model (nofilter, 5, 15)")
        
        rows_filtered = row_list (filtered_model)
        self.assertEquals (rows_ranged, rows_filtered)

        self.assertEquals ([ranged_model.line_index_from_super (i)
                            for i in range (5, 16)],
                           range (11))
        self.assertEquals ([ranged_model.line_index_to_super (i)
                            for i in range (11)],
                           range (5, 16))
        self.assertEquals ([ranged_model.line_index_from_top (i)
                            for i in range (5, 16)],
                           range (11))
        self.assertEquals ([ranged_model.line_index_to_top (i)
                            for i in range (11)],
                           range (5, 16))

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (11)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (5, 16)],
                           range (11))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (11)],
                           range (5, 16))

        filtered_model.add_filter (GUI.CategoryFilter ("EVEN"),
                                   Common.Data.DefaultDispatcher ())
        rows_filtered = row_list (filtered_model)
        self.assertEquals (rows_filtered, range (5, 16, 2))

        self.__dump_model (filtered_model, "filtered model")

        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (0, 11, 2)],
                           range (6))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (5, 16, 2)],
                           range (6))

        ranged_model.set_range (7, 13)
        self.__dump_model (ranged_model, "ranged model (7, 13)")
        filtered_model.super_model_changed_range ()

        self.assertEquals (row_list (ranged_model), range (7, 13))
        self.assertEquals ([ranged_model.line_index_from_super (i)
                            for i in range (7, 13)],
                           range (6))
        self.assertEquals ([ranged_model.line_index_to_super (i)
                            for i in range (6)],
                           range (7, 13))
        self.assertEquals ([ranged_model.line_index_from_top (i)
                            for i in range (7, 13)],
                           range (6))
        self.assertEquals ([ranged_model.line_index_to_top (i)
                            for i in range (6)],
                           range (7, 13))

        self.__dump_model (filtered_model, "filtered model (ranged 7, 12)")
        self.assertEquals ([filtered_model.line_index_from_super (i)
                            for i in range (0, 6, 2)],
                           range (3))
        self.assertEquals ([filtered_model.line_index_to_super (i)
                            for i in range (3)],
                           range (0, 6, 2))
        self.assertEquals ([filtered_model.line_index_from_top (i)
                            for i in range (7, 12, 2)],
                           range (3))
        self.assertEquals ([filtered_model.line_index_to_top (i)
                            for i in range (3)],
                           range (7, 12, 2))

        rows_filtered = row_list (filtered_model)
        self.assertEquals (rows_filtered, range (7, 13, 2))

    def test_random_filtered_range_refilter (self):

        full_model = Model ()
        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)
        row_list = self.__row_list

        self.assertEquals (row_list (full_model), range (20))
        self.assertEquals (row_list (ranged_model), range (20))
        self.assertEquals (row_list (filtered_model), range (20))

        filtered_model.add_filter (RandomFilter (538295943),
                                   Common.Data.DefaultDispatcher ())
        random_rows = row_list (filtered_model)

        self.__dump_model (filtered_model)
        ranged_model.set_range (10, 20)
        self.__dump_model (ranged_model, "ranged_model (10, 20)")
        self.assertEquals (row_list (ranged_model), range (10, 20))
        filtered_model.super_model_changed_range ()
        self.__dump_model (filtered_model)
        self.assertEquals (row_list (filtered_model), [x for x in range (10, 20) if x in random_rows])

        ranged_model.set_range (0, 20)
        self.assertEquals (row_list (ranged_model), range (0, 20))

        ranged_model = GUI.RangeFilteredLogModel (full_model)
        # FIXME: Call to .reset should not be needed.
        ranged_model.reset ()
        filtered_model = GUI.FilteredLogModel (ranged_model)
        filtered_model.add_filter (RandomFilter (538295943),
                                   Common.Data.DefaultDispatcher ())
        self.__dump_model (filtered_model, "filtered model")
        self.assertEquals (row_list (filtered_model), random_rows)

        ranged_model.set_range (0, 10)
        self.__dump_model (ranged_model, "ranged model (0, 10)")
        filtered_model.super_model_changed_range ()
        self.assertEquals (row_list (ranged_model), range (0, 10))
        self.__dump_model (filtered_model)
        self.assertEquals (row_list (filtered_model), [x for x in range (0, 10) if x in random_rows])

    def __row_list (self, model):

        return [row[Model.COL_PID] for row in model]

    def __dump_model (self, model, comment = None):

        # TODO: Provide a command line option to turn this on and off.

        return

        if not hasattr (model, "super_model"):
            # Top model.
            print "\t(%s)" % ("|".join ([str (i).rjust (2) for i in self.__row_list (model)]),),
        else:
            top_model = model.super_model
            if hasattr (top_model, "super_model"):
                top_model = top_model.super_model
            top_indices = self.__row_list (top_model)
            positions = self.__row_list (model)
            output = ["  "] * len (top_indices)
            for i, position in enumerate (positions):
                output[position] = str (i).rjust (2)
            print "\t(%s)" % ("|".join (output),),

        if comment is None:
            print
        else:
            print comment
            
if __name__ == "__main__":
    test_main ()
