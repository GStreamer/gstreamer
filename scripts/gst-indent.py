#!/usr/bin/env python3

from sys import argv
from gst_indent_common import indent


if __name__ == '__main__':
    indent(*argv[1:])
