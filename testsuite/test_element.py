#!/usr/bin/python
#
# testsuite for gstreamer.Element

import sys
from gstreamer import *

def fail (message):
    'print reason for failing and leave'
    print "FAILED: %s" % message
    sys.exit (-1)

# create an element we know exists
src = Element ("fakesrc", "source")
if not src: fail ("Can't create fakesrc Element")

# create an element we know doesn't exist
nope = None
result = 0
try:
    nope = Element ("idontexist", "none")
except RuntimeError: result = 1
if result == 0: fail ("creating an unexistant element didn't generate a RuntimeError")

# create a sink
sink = Element ("fakesink", "sink")

# link
src.link (sink)

sys.exit (0)
