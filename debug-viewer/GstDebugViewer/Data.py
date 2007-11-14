# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Debug Viewer
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

"""GStreamer debug viewer data module"""

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

    __names = ["NONE", "ERROR", "WARNING", "INFO", "DEBUG", "LOG"]
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

DebugLevelNone = DebugLevel ("NONE")
DebugLevelError = DebugLevel ("ERROR")
DebugLevelWarning = DebugLevel ("WARNING")
DebugLevelInfo = DebugLevel ("INFO")
DebugLevelDebug = DebugLevel ("DEBUG")
DebugLevelLog = DebugLevel ("LOG")

# For stripping color codes:
_escape = re.compile ("\x1b\\[[0-9;]*m")
def strip_escape (s):

    return _escape.sub ("", s)

def default_log_line_regex_ ():

    # "DEBUG             "
    LEVEL = "([A-Z]+) +"
    # "0x8165430 "
    THREAD = r"(0x[0-9a-f]+) +" #r"\((0x[0-9a-f]+) - "
    # "0:00:00.777913000  "
    #TIME = r"([0-9]+:[0-9][0-9]:[0-9][0-9]\.[0-9]+) +"
    TIME = " +" # Only eating whitespace before PID away, we parse timestamps
                # without regex.  
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

    expressions = [TIME, PID, THREAD, LEVEL, CATEGORY, FILENAME, LINE, FUNCTION,
                   OBJECT, MESSAGE]
##     expressions = [LEVEL, THREAD, TIME, CATEGORY, PID, FILENAME, LINE,
##                    FUNCTION, OBJECT, MESSAGE]

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

        self.offsets = []
        self.dispatcher = dispatcher

        import mmap
        self.__fileobj = mmap.mmap (fileobj.fileno (), 0, prot = mmap.PROT_READ)

        self.__fileobj.seek (0, 2)
        self.__file_size = self.__fileobj.tell ()
        self.__fileobj.seek (0)

    def start_loading (self):

        self.logger.debug ("dispatching load process")
        self.have_load_started ()
        self.dispatcher (self.__process ())

    def get_progress (self):

        return float (self.__fileobj.tell ()) / self.__file_size

    def __process (self):

        offsets = self.offsets
        readline = self.__fileobj.readline
        tell = self.__fileobj.tell

        self.__fileobj.seek (0)
        limit = self._lines_per_iteration
        i = 0
        while True:
            offset = tell ()
            line = readline ()
            if not line:
                break
            if not line.strip ():
                # Ignore empty lines, especially the one established by the
                # final newline at the end:
                continue
            # FIXME: We need to handle foreign lines separately!
            if line[1] != ":" or line[4] != ":" or line[7] != ".":
                # No timestamp at start, ignore line:
                continue
            offsets.append (offset)
            i += 1
            if i >= limit:
                i = 0
                yield True

        self.have_load_finished ()
        yield False

class LogFile (Producer):

    def __init__ (self, filename, dispatcher):

        Producer.__init__ (self)

        self.logger = logging.getLogger ("logfile")

        self.fileobj = file (filename, "rb")
        self.line_cache = LineCache (self.fileobj, dispatcher)
        self.line_cache.consumers.append (self)

    def start_loading (self):

        self.logger.debug ("starting load")
        self.line_cache.start_loading ()

    def get_load_progress (self):

        return self.line_cache.get_progress ()

    def handle_load_started (self):

        # Chain up to our consumers:
        self.have_load_started ()

    def handle_load_finished (self):

        # Chain up to our consumers:
        self.have_load_finished ()

