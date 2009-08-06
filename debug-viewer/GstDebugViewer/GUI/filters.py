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

class Filter (object):

    pass

class DebugLevelFilter (Filter):

    def __init__ (self, debug_level):

        col_id = LogModelBase.COL_LEVEL
        def filter_func (row):
            return row[col_id] != debug_level
        self.filter_func = filter_func

class CategoryFilter (Filter):

    def __init__ (self, category):

        col_id = LogModelBase.COL_CATEGORY
        def category_filter_func (row):
            return row[col_id] != category
        self.filter_func = category_filter_func

class ObjectFilter (Filter):

    def __init__ (self, object_):

        col_id = LogModelBase.COL_OBJECT
        def object_filter_func (row):
            return row[col_id] != object_
        self.filter_func = object_filter_func

class FilenameFilter (Filter):

    def __init__ (self, filename):

        col_id = LogModelBase.COL_FILENAME
        def filename_filter_func (row):
            return row[col_id] != filename
        self.filter_func = filename_filter_func

