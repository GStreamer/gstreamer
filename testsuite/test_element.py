#!/usr/bin/python
#
# testsuite for gstreamer.Element

import sys
sys.path.insert(0, '..')

import gst

def fail(message):
    'print reason for failing and leave'
    print "FAILED: %s" % message
    sys.exit(-1)

# create an element we know exists
src = gst.Element("fakesrc", "source")
if not src:
    fail("Can't create fakesrc Element")

# create an element we know doesn't exist
nope = None
result = 0
try:
    nope = gst.Element("idontexist", "none")
except RuntimeError: result = 1
if result == 0:
    fail("creating an unexistant element didn't generate a RuntimeError")

# create a sink
sink = gst.Element("fakesink", "sink")

# link
if not src.link(sink):
    fail("could not link")

sys.exit(0)
