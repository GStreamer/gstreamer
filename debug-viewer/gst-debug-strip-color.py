#!/usr/bin/env python

import sys
import re

def strip_color (input, output):

    _escape = re.compile ("\x1b\\[[0-9;]*m")
    # TODO: This can be optimized further!

    for line in input:
        while "\x1b" in line:
            line = _escape.sub ("", line)
        output.write (line)

def main ():

    if len (sys.argv) == 1 or sys.argv[1] == "-":
        strip_color (sys.stdin, sys.stdout)
    else:
        strip_color (file (sys.argv[1], "rb"), sys.stdout)

if __name__ == "__main__":
    main ()
