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

"""GStreamer Debug Viewer Data module."""

import os
import logging
import re

# Nanosecond resolution (like gst.SECOND)
SECOND = 1000000000

def time_args (ts):

    secs = ts // SECOND

    return "%i:%02i:%02i.%09i" % (secs // 60**2,
                                  secs // 60 % 60,
                                  secs % 60,
                                  ts % SECOND,)

def time_args_no_hours (ts):

    secs = ts // SECOND

    return "%02i:%02i.%09i" % (secs // 60,
                               secs % 60,
                               ts % SECOND,)

def parse_time (st):

    """Parse time strings that look like "0:00:00.0000000"."""

    h, m, s = st.split (":")
    secs, subsecs = s.split (".")

    return (long ((int (h) * 60**2 + int (m) * 60) * SECOND) +
            long (secs) * SECOND + long (subsecs))

class DebugLevel (int):

    __names = ["NONE", "ERROR", "WARN", "INFO", "DEBUG", "LOG"]
    __instances = {}

    def __new__ (cls, level):

        try:
            level_int = int (level)
        except (ValueError, TypeError,):
            try:
                level_int = cls.__names.index (level.upper ())
            except ValueError:
                raise ValueError ("no debug level named %r" % (level,))
        if level_int in cls.__instances:
            return cls.__instances[level_int]
        else:
            new_instance = int.__new__ (cls, level_int)
            new_instance.name = cls.__names[level_int]
            cls.__instances[level_int] = new_instance
            return new_instance

    def __repr__ (self):

        return "<%s %s (%i)>" % (type (self).__name__, self.__names[self], self,)

    def higher_level (self):

        if self == len (self.__names) - 1:
            raise ValueError ("already the highest debug level")

        return DebugLevel (self + 1)

    def lower_level (self):

        if self == 0:
            raise ValueError ("already the lowest debug level")

        return DebugLevel (self - 1)

debug_level_none = DebugLevel ("NONE")
debug_level_error = DebugLevel ("ERROR")
debug_level_warning = DebugLevel ("WARN")
debug_level_info = DebugLevel ("INFO")
debug_level_debug = DebugLevel ("DEBUG")
debug_level_log = DebugLevel ("LOG")

# For stripping color codes:
_escape = re.compile ("\x1b\\[[0-9;]*m")
def strip_escape (s):

    # FIXME: This can be optimized further!

    while "\x1b" in s:
        s = _escape.sub ("", s)
    return s

def default_log_line_regex_ ():

    # "DEBUG             "
    LEVEL = "([A-Z]+) +"
    # "0x8165430 "
    THREAD = r"(0x[0-9a-f]+) +" #r"\((0x[0-9a-f]+) - "
    # "0:00:00.777913000  "
    TIME = r"([0-9]+:[0-9][0-9]:[0-9][0-9]\.[0-9]+) +"
    CATEGORY = "([A-Za-z_-]+) +" # "GST_REFCOUNTING ", "flacdec "
    # "  3089 "
    PID = r"([0-9]+) +"
    FILENAME = r"([^:]+):"
    LINE = r"([0-9]+):"
    FUNCTION = "([A-Za-z0-9_]+):"
    # FIXME: When non-g(st)object stuff is logged with *_OBJECT (like
    # buffers!), the address is printed *without* <> brackets!
    OBJECT = "(?:<([^>]+)>)?"
    MESSAGE = " (.+)"

    expressions = [" *", CATEGORY, FILENAME, LINE, FUNCTION, OBJECT]
    # New log format:
    ## expressions = [TIME, PID, THREAD, LEVEL, CATEGORY, FILENAME, LINE, FUNCTION,
    ##                OBJECT, MESSAGE]
    # Old log format:
    ## expressions = [LEVEL, THREAD, TIME, CATEGORY, PID, FILENAME, LINE,
    ##                FUNCTION, OBJECT, MESSAGE]

    return expressions

def default_log_line_regex ():
    
    expressions = default_log_line_regex_ ()
    return re.compile ("".join (expressions))

class Producer (object):

    def __init__ (self):

        self.consumers = []

    def have_load_started (self):

        for consumer in self.consumers:
            consumer.handle_load_started ()

    def have_load_finished (self):

        for consumer in self.consumers:
            consumer.handle_load_finished ()

class LineCache (Producer):

    _lines_per_iteration = 50000

    def __init__ (self, fileobj, dispatcher):

        Producer.__init__ (self)

        self.logger = logging.getLogger ("linecache")
        self.dispatcher = dispatcher

        import mmap
        self.__fileobj = mmap.mmap (fileobj.fileno (), 0, prot = mmap.PROT_READ)

        self.__fileobj.seek (0, 2)
        self.__file_size = self.__fileobj.tell ()
        self.__fileobj.seek (0)

        self.offsets = []
        self.levels = [] # FIXME

    def start_loading (self):

        self.logger.debug ("dispatching load process")
        self.have_load_started ()
        self.dispatcher (self.__process ())

    def get_progress (self):

        return float (self.__fileobj.tell ()) / self.__file_size

    def __process (self):

        offsets = self.offsets
        levels = self.levels

        # FIXME: Duplicated from GUI.LazyLogModel!
        ts_len = 17
        pid_len = 5
        # Need to find out thread len below...
        level_len = 5
        thread_start = ts_len + 1 + pid_len + 1
        dict_levels = {"D" : debug_level_debug, "L" : debug_level_log,
                       "I" : debug_level_info, "E" : debug_level_error,
                       "W" : debug_level_warning, " " : debug_level_none}

        readline = self.__fileobj.readline
        tell = self.__fileobj.tell

        self.__fileobj.seek (0)
        limit = self._lines_per_iteration
        i = 0
        # TODO: Remove the checks inside this loop.  Instead, let exceptions
        # raise, catch them outside (for performance) and resume the iteration.
        while True:
            offset = tell ()
            line = readline ()
            if not line:
                break
            if not line.strip ():
                # Ignore empty lines, especially the one established by the
                # final newline at the end:
                continue
            if len (line) < ts_len:
                continue
            # FIXME: We need to handle foreign lines separately!
            if line[1] != ":" or line[4] != ":" or line[7] != ".":
                # No timestamp at start, ignore line:
                continue

            thread_end = line.find (" ", thread_start)
            if thread_end == -1 or thread_end + 1 >= len (line):
                continue
            offsets.append (offset)
            level_start = line[thread_end + 1:thread_end + 2]
            levels.append (dict_levels.get (level_start, debug_level_none))
            i += 1
            if i >= limit:
                i = 0
                yield True

        self.have_load_finished ()
        yield False

class LogLine (list):

    _line_regex = default_log_line_regex ()

    @classmethod
    def parse_full (cls, line_string):

        ts_len = 17
        pid_len = 5

        thread_pos = ts_len + 1 + pid_len + 1
        thread_len = line_string[thread_pos:thread_pos + 32].find (" ")
        level_len = 5

        non_regex_len = ts_len + 1 + pid_len + thread_len + 1 + level_len + 1
        non_regex_line = line_string[:non_regex_len]
        regex_line = line_string[non_regex_len:]

        prefix = non_regex_line.rstrip ()
        while "  " in prefix:
            prefix = prefix.replace ("  ", " ")
        ts_s, pid_s, thread_s = prefix.split (" ")[:-1] # Omits level.
        ts = parse_time (ts_s)
        pid = int (pid_s)
        thread = int (thread_s, 16)
        try:
            ## level = DebugLevel (level_s)
            match = cls._line_regex.match (regex_line[:-len (os.linesep)])
        except ValueError:
            level = debug_level_none
            match = None

        if match is None:
            # FIXME?
            groups = [ts, pid, thread, 0, "", "", 0, "", "", non_regex_len]
        else:
            # FIXME: Level (the 0 after thread) needs to be moved out of here!
            groups = [ts, pid, thread, 0] + list (match.groups ()) + [non_regex_len + match.end ()]

            for col_id in (4,   # COL_CATEGORY
                           5,   # COL_FILENAME
                           7,   # COL_FUNCTION,
                           8,): # COL_OBJECT
                groups[col_id] = intern (groups[col_id] or "")
            
            groups[6] = int (groups[6]) # line
            # groups[8] = groups[8] or "" # object (optional)

        return cls (groups)

    def line_string (self, message = None):

        # Replicates gstreamer/gst/gstinfo.c:gst_debug_log_default.

        ts, pid, thread, level, category, filename, line, function, object_, message_offset = self

        if isinstance (message_offset, str):
            message = message_offset

        return "%s %5d 0x%x %s %20s %s:%d:%s:<%s> %s" % (time_args (ts), pid, thread, level.name.ljust (5),
                                                         category, filename, line, function,
                                                         object_, message,)

class LogLines (object):

    def __init__ (self, fileobj, line_cache):

        self.__fileobj = fileobj
        self.__line_cache = line_cache

    def __len__ (self):

        return len (self.__line_cache.offsets)

    def __getitem__ (self, line_index):

        offset = self.__line_cache.offsets[line_index]
        self.__fileobj.seek (offset)
        line_string = self.__fileobj.readline ()
        line = LogLine.parse_full (line_string)
        msg = line_string[line[-1]:]
        line[-1] = msg
        return line

    def __iter__ (self):

        l = len (self)
        i = 0
        while i < l:
            yield self[i]
            i += 1

class LogFile (Producer):

    def __init__ (self, filename, dispatcher):

        Producer.__init__ (self)

        self.logger = logging.getLogger ("logfile")

        self.path = os.path.normpath (os.path.abspath (filename))
        self.fileobj = file (filename, "rb")
        self.line_cache = LineCache (self.fileobj, dispatcher)
        self.line_cache.consumers.append (self)

    def get_full_line (self, line_index):

        offset = self.line_cache.offsets[line_index]
        self.fileobj.seek (offset)
        line_string = self.fileobj.readline ()
        line = LogLine.parse_full (line_string)
        msg = line_string[line[-1]:]
        line[-1] = msg
        return line

    def start_loading (self):

        self.logger.debug ("starting load")
        self.line_cache.start_loading ()

    def get_load_progress (self):

        return self.line_cache.get_progress ()

    def handle_load_started (self):

        # Chain up to our consumers:
        self.have_load_started ()

    def handle_load_finished (self):

        self.lines = LogLines (self.fileobj, self.line_cache)

        # Chain up to our consumers:
        self.have_load_finished ()

