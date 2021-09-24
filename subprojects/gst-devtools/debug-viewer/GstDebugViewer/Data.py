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
import sys

# Nanosecond resolution (like Gst.SECOND)
SECOND = 1000000000


def time_args(ts):

    secs = ts // SECOND

    return "%i:%02i:%02i.%09i" % (secs // 60 ** 2,
                                  secs // 60 % 60,
                                  secs % 60,
                                  ts % SECOND,)


def time_diff_args(time_diff):

    if time_diff >= 0:
        sign = "+"
    else:
        sign = "-"

    secs = abs(time_diff) // SECOND

    return "%s%02i:%02i.%09i" % (sign,
                                 secs // 60,
                                 secs % 60,
                                 abs(time_diff) % SECOND,)


def time_args_no_hours(ts):

    secs = ts // SECOND

    return "%02i:%02i.%09i" % (secs // 60,
                               secs % 60,
                               ts % SECOND,)


def parse_time(st):
    """Parse time strings that look like "0:00:00.0000000"."""

    h, m, s = st.split(":")
    secs, subsecs = s.split(".")

    return int((int(h) * 60 ** 2 + int(m) * 60) * SECOND) + \
        int(secs) * SECOND + int(subsecs)


class DebugLevel (int):

    __names = ["NONE", "ERROR", "WARN", "FIXME",
               "INFO", "DEBUG", "LOG", "TRACE", "MEMDUMP"]
    __instances = {}

    def __new__(cls, level):

        try:
            level_int = int(level)
        except (ValueError, TypeError,):
            try:
                level_int = cls.__names.index(level.upper())
            except ValueError:
                raise ValueError("no debug level named %r" % (level,))
        if level_int in cls.__instances:
            return cls.__instances[level_int]
        else:
            new_instance = int.__new__(cls, level_int)
            new_instance.name = cls.__names[level_int]
            cls.__instances[level_int] = new_instance
            return new_instance

    def __repr__(self):

        return "<%s %s (%i)>" % (type(self).__name__, self.__names[self], self,)

    def higher_level(self):

        if self == len(self.__names) - 1:
            raise ValueError("already the highest debug level")

        return DebugLevel(self + 1)

    def lower_level(self):

        if self == 0:
            raise ValueError("already the lowest debug level")

        return DebugLevel(self - 1)


debug_level_none = DebugLevel("NONE")
debug_level_error = DebugLevel("ERROR")
debug_level_warning = DebugLevel("WARN")
debug_level_info = DebugLevel("INFO")
debug_level_debug = DebugLevel("DEBUG")
debug_level_log = DebugLevel("LOG")
debug_level_fixme = DebugLevel("FIXME")
debug_level_trace = DebugLevel("TRACE")
debug_level_memdump = DebugLevel("MEMDUMP")
debug_levels = [debug_level_none,
                debug_level_trace,
                debug_level_fixme,
                debug_level_log,
                debug_level_debug,
                debug_level_info,
                debug_level_warning,
                debug_level_error,
                debug_level_memdump]

# For stripping color codes:
_escape = re.compile(b"\x1b\\[[0-9;]*m")


def strip_escape(s):

    # FIXME: This can be optimized further!

    while b"\x1b" in s:
        s = _escape.sub(b"", s)
    return s


def default_log_line_regex_():

    # "DEBUG             "
    LEVEL = "([A-Z]+)\s*"
    # "0x8165430 "
    THREAD = r"(0x[0-9a-f]+)\s+"  # r"\((0x[0-9a-f]+) - "
    # "0:00:00.777913000  "
    TIME = r"(\d+:\d\d:\d\d\.\d+)\s+"
    CATEGORY = "([A-Za-z0-9_-]+)\s+"  # "GST_REFCOUNTING ", "flacdec "
    # "  3089 "
    PID = r"(\d+)\s*"
    FILENAME = r"([^:]*):"
    LINE = r"(\d+):"
    FUNCTION = "(~?[A-Za-z0-9_\s\*,\(\)]*):"
    # FIXME: When non-g(st)object stuff is logged with *_OBJECT (like
    # buffers!), the address is printed *without* <> brackets!
    OBJECT = "(?:<([^>]+)>)?"
    MESSAGE = "(.+)"

    ANSI = "(?:\x1b\\[[0-9;]*m\\s*)*\\s*"

    # New log format:
    expressions = [TIME, ANSI, PID, ANSI, THREAD, ANSI, LEVEL, ANSI,
                   CATEGORY, FILENAME, LINE, FUNCTION, ANSI,
                   OBJECT, ANSI, MESSAGE]
    # Old log format:
    # expressions = [LEVEL, THREAD, TIME, CATEGORY, PID, FILENAME, LINE,
    # FUNCTION, OBJECT, MESSAGE]

    return expressions


def default_log_line_regex():

    return re.compile("".join(default_log_line_regex_()))


class Producer (object):

    def __init__(self):

        self.consumers = []

    def have_load_started(self):

        for consumer in self.consumers:
            consumer.handle_load_started()

    def have_load_finished(self):

        for consumer in self.consumers:
            consumer.handle_load_finished()


class SortHelper (object):

    def __init__(self, fileobj, offsets):

        self._gen = self.__gen(fileobj, offsets)
        next(self._gen)

        # Override in the instance, for performance (this gets called in an
        # inner loop):
        self.find_insert_position = self._gen.send

    @staticmethod
    def find_insert_position(insert_time_string):

        # Stub for documentary purposes.

        pass

    @staticmethod
    def __gen(fileobj, offsets):

        from math import floor
        tell = fileobj.tell
        seek = fileobj.seek
        read = fileobj.read
        time_len = len(time_args(0))

        # We remember the previous insertion point. This gives a nice speed up
        # for larger bubbles which are already sorted. TODO: In practice, log
        # lines only get out of order across threads. Need to check if it pays
        # to parse the thread here and maintain multiple insertion points for
        # heavily interleaved parts of the log.
        pos = 0
        pos_time_string = ""

        insert_pos = None
        while True:
            insert_time_string = (yield insert_pos)

            save_offset = tell()

            if pos_time_string <= insert_time_string:
                lo = pos
                hi = len(offsets)
            else:
                lo = 0
                hi = pos

            # This is a bisection search, except we don't cut the range in the
            # middle each time, but at the 90th percentile. This is because
            # logs are "mostly sorted", so the insertion point is much more
            # likely to be at the end anyways:
            while lo < hi:
                mid = int(floor(lo * 0.1 + hi * 0.9))
                seek(offsets[mid])
                mid_time_string = read(time_len)
                if insert_time_string.encode('utf8') < mid_time_string:
                    hi = mid
                else:
                    lo = mid + 1
            pos = lo
            # Caller will replace row at pos with the new one, so this is
            # correct:
            pos_time_string = insert_time_string

            insert_pos = pos

            seek(save_offset)


class LineCache (Producer):
    """
    offsets: file position for each line
    levels: the debug level for each line
    """

    _lines_per_iteration = 50000

    def __init__(self, fileobj, dispatcher):

        Producer.__init__(self)

        self.logger = logging.getLogger("linecache")
        self.dispatcher = dispatcher

        self.__fileobj = fileobj
        self.__fileobj.seek(0, 2)
        self.__file_size = self.__fileobj.tell()
        self.__fileobj.seek(0)

        self.offsets = []
        self.levels = []  # FIXME

    def start_loading(self):

        self.logger.debug("dispatching load process")
        self.have_load_started()
        self.dispatcher(self.__process())

    def get_progress(self):

        return float(self.__fileobj.tell()) / self.__file_size

    def __process(self):

        offsets = self.offsets
        levels = self.levels

        dict_levels = {"T": debug_level_trace, "F": debug_level_fixme,
                       "L": debug_level_log, "D": debug_level_debug,
                       "I": debug_level_info, "W": debug_level_warning,
                       "E": debug_level_error, " ": debug_level_none,
                       "M": debug_level_memdump, }
        ANSI = "(?:\x1b\\[[0-9;]*m)?"
        ANSI_PATTERN = r"\d:\d\d:\d\d\.\d+ " + ANSI + \
                       r" *\d+" + ANSI + \
                       r" +0x[0-9a-f]+ +" + ANSI + \
                       r"([TFLDIEWM ])"
        BARE_PATTERN = ANSI_PATTERN.replace(ANSI, "")
        rexp_bare = re.compile(BARE_PATTERN)
        rexp_ansi = re.compile(ANSI_PATTERN)
        rexp = rexp_bare

        # Moving attribute lookups out of the loop:
        readline = self.__fileobj.readline
        tell = self.__fileobj.tell
        rexp_match = rexp.match
        levels_append = levels.append
        offsets_append = offsets.append
        dict_levels_get = dict_levels.get

        self.__fileobj.seek(0)
        limit = self._lines_per_iteration
        last_line = ""
        i = 0
        sort_helper = SortHelper(self.__fileobj, offsets)
        find_insert_position = sort_helper.find_insert_position
        while True:
            i += 1
            if i >= limit:
                i = 0
                yield True

            offset = tell()
            line = readline().decode('utf-8', errors='replace')
            if not line:
                break
            match = rexp_match(line)
            if match is None:
                if rexp is rexp_ansi or "\x1b" not in line:
                    continue

                match = rexp_ansi.match(line)
                if match is None:
                    continue
                # Switch to slower ANSI parsing:
                rexp = rexp_ansi
                rexp_match = rexp.match

            # Timestamp is in the very beginning of the row, and can be sorted
            # by lexical comparison. That's why we don't bother parsing the
            # time to integer. We also don't have to take a substring here,
            # which would be a useless memcpy.
            if line >= last_line:
                levels_append(
                    dict_levels_get(match.group(1), debug_level_none))
                offsets_append(offset)
                last_line = line
            else:
                pos = find_insert_position(line)
                levels.insert(
                    pos, dict_levels_get(match.group(1), debug_level_none))
                offsets.insert(pos, offset)

        self.have_load_finished()
        yield False


class LogLine (list):

    _line_regex = default_log_line_regex()

    @classmethod
    def parse_full(cls, line_string):
        match = cls._line_regex.match(line_string.decode('utf8', errors='replace'))
        if match is None:
            # raise ValueError ("not a valid log line (%r)" % (line_string,))
            groups = [0, 0, 0, 0, "", "", 0, "", "", 0]
            return cls(groups)

        line = cls(match.groups())
        # Timestamp.
        line[0] = parse_time(line[0])
        # PID.
        line[1] = int(line[1])
        # Thread.
        line[2] = int(line[2], 16)
        # Level (this is handled in LineCache).
        line[3] = 0
        # Line.
        line[6] = int(line[6])
        # Message start offset.
        line[9] = match.start(9 + 1)

        for col_id in (4,   # COL_CATEGORY
                       5,   # COL_FILENAME
                       7,   # COL_FUNCTION,
                       8,):  # COL_OBJECT
            line[col_id] = sys.intern(line[col_id] or "")

        return line


class LogLines (object):

    def __init__(self, fileobj, line_cache):

        self.__fileobj = fileobj
        self.__line_cache = line_cache

    def __len__(self):

        return len(self.__line_cache.offsets)

    def __getitem__(self, line_index):

        offset = self.__line_cache.offsets[line_index]
        self.__fileobj.seek(offset)
        line_string = self.__fileobj.readline()
        line = LogLine.parse_full(line_string)
        msg = line_string[line[-1]:]
        line[-1] = msg
        return line

    def __iter__(self):

        size = len(self)
        i = 0
        while i < size:
            yield self[i]
            i += 1


class LogFile (Producer):

    def __init__(self, filename, dispatcher):

        import mmap

        Producer.__init__(self)

        self.logger = logging.getLogger("logfile")

        self.path = os.path.normpath(os.path.abspath(filename))
        self.__real_fileobj = open(filename, "rb")
        self.fileobj = mmap.mmap(
            self.__real_fileobj.fileno(), 0, access=mmap.ACCESS_READ)
        self.line_cache = LineCache(self.fileobj, dispatcher)
        self.line_cache.consumers.append(self)

    def start_loading(self):

        self.logger.debug("starting load")
        self.line_cache.start_loading()

    def get_load_progress(self):

        return self.line_cache.get_progress()

    def handle_load_started(self):

        # Chain up to our consumers:
        self.have_load_started()

    def handle_load_finished(self):
        self.logger.debug("finish loading")
        self.lines = LogLines(self.fileobj, self.line_cache)

        # Chain up to our consumers:
        self.have_load_finished()
