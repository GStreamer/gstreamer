#!/usr/bin/env python
# -*- Mode: Python; py-indent-offset: 4 -*-

import sys
import defsparser

if len(sys.argv) < 3:
    sys.stderr.write("Usage: mergedefs.py generated-defs old-defs\n")
    sys.exit(1)

newp = defsparser.DefsParser(sys.argv[1])
oldp = defsparser.DefsParser(sys.argv[2])

newp.startParsing()
oldp.startParsing()

newp.merge(oldp)

newp.write_defs()
