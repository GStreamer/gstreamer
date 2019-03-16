import os
import re
import sys


def _log_line_regex():

    # "0:00:00.777913000  "
    TIME = r"(\d+:\d\d:\d\d\.\d+)\s+"
    # "DEBUG             "
    # LEVEL = "([A-Z]+)\s+"
    LEVEL = "(TRACE)\s+"
    # "0x8165430 "
    THREAD = r"(0x[0-9a-f]+)\s+"
    # "GST_REFCOUNTING ", "flacdec "
    CATEGORY = "([A-Za-z0-9_-]+)\s+"
    # "  3089 "
    PID = r"(\d+)\s*"
    FILENAME = r"([^:]*):"
    LINE = r"(\d+):"
    FUNCTION = r"([A-Za-z0-9_]*):"
    # FIXME: When non-g(st)object stuff is logged with *_OBJECT (like
    # buffers!), the address is printed *without* <> brackets!
    OBJECT = "(?:<([^>]+)>)?"
    MESSAGE = "(.+)"

    ANSI = "(?:\x1b\\[[0-9;]*m\\s*)*\\s*"

    return [TIME, ANSI, PID, ANSI, THREAD, ANSI, LEVEL, ANSI, CATEGORY,
            FILENAME, LINE, FUNCTION, ANSI, OBJECT, ANSI, MESSAGE]


class Parser(object):
    """
    Helper to parse a tracer log.

    Implements context manager and iterator.
    """

    # record fields
    F_TIME = 0
    F_PID = 1
    F_THREAD = 2
    F_LEVEL = 3
    F_CATEGORY = 4
    F_FILENAME = 5
    F_LINE = 6
    F_FUNCTION = 7
    F_OBJECT = 8
    F_MESSAGE = 9

    def __init__(self, filename):
        self.filename = filename
        self.log_regex = re.compile(''.join(_log_line_regex()))
        self.file = None

    def __enter__(self):
        if self.filename != '-':
            self.file = open(self.filename, 'rt')
        else:
            self.file = sys.stdin
        return self

    def __exit__(self, *args):
        if self.filename != '-':
            self.file.close()
            self.file = None

    def __iter__(self):
        return self

    def __next__(self):
        log_regex = self.log_regex
        data = self.file
        while True:
            line = next(data)
            match = log_regex.match(line)
            if match:
                g = list(match.groups())
                g[Parser.F_PID] = int(g[Parser.F_PID])
                g[Parser.F_LINE] = int(g[Parser.F_LINE])
                return g
