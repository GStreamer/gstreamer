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

from GstDebugViewer.GUI.models import LogModelBase


def get_comparison_function(all_but_this):

    if (all_but_this):
        return lambda x, y: x == y
    else:
        return lambda x, y: x != y


class Filter (object):

    pass


class DebugLevelFilter (Filter):

    only_this, all_but_this, this_and_above = range(3)

    def __init__(self, debug_level, mode=0):

        col_id = LogModelBase.COL_LEVEL
        if mode == self.this_and_above:
            def comparison_function(x, y):
                return x < y
        else:
            comparison_function = get_comparison_function(
                mode == self.all_but_this)

        def filter_func(row):
            return comparison_function(row[col_id], debug_level)
        self.filter_func = filter_func


class CategoryFilter (Filter):

    def __init__(self, category, all_but_this=False):

        col_id = LogModelBase.COL_CATEGORY
        comparison_function = get_comparison_function(all_but_this)

        def category_filter_func(row):
            return comparison_function(row[col_id], category)
        self.filter_func = category_filter_func


class ObjectFilter (Filter):

    def __init__(self, object_, all_but_this=False):

        col_id = LogModelBase.COL_OBJECT
        comparison_function = get_comparison_function(all_but_this)

        def object_filter_func(row):
            return comparison_function(row[col_id], object_)
        self.filter_func = object_filter_func


class FunctionFilter (Filter):

    def __init__(self, function_, all_but_this=False):

        col_id = LogModelBase.COL_FUNCTION
        comparison_function = get_comparison_function(all_but_this)

        def function_filter_func(row):
            return comparison_function(row[col_id], function_)
        self.filter_func = function_filter_func


class ThreadFilter (Filter):

    def __init__(self, thread_, all_but_this=False):

        col_id = LogModelBase.COL_THREAD
        comparison_function = get_comparison_function(all_but_this)

        def thread_filter_func(row):
            return comparison_function(row[col_id], thread_)
        self.filter_func = thread_filter_func


class FilenameFilter (Filter):

    def __init__(self, filename, all_but_this=False):

        col_id = LogModelBase.COL_FILENAME
        comparison_function = get_comparison_function(all_but_this)

        def filename_filter_func(row):
            return comparison_function(row[col_id], filename)
        self.filter_func = filename_filter_func
