import dl
import os
import sys
import unittest

devloc = os.path.join('..', 'gst', '.libs')
if os.path.exists(devloc):
   sys.path.insert(0, devloc)

# Load GST and make sure we load it from the current build

sys.setdlopenflags(dl.RTLD_LAZY | dl.RTLD_GLOBAL)

# We're importing _gst, since we don't have access to __init__.py
# during distcheck where builddir != srcdir
import _gst as gst

# Put the fake module in sys.modules, otherwise the C modules
# Can't find the classes accordingly
sys.modules['gst'] = gst

try:
    import interfaces
    gst.interfaces = interfaces
    sys.modules['gst.interfaces'] = interfaces
except ImportError:
    pass

try:
    import play
    gst.play = play
    sys.modules['gst.play'] = play
except ImportError:
    pass

assert sys.modules.has_key('_gst')
assert os.path.basename(sys.modules['_gst'].__file__), \
       os.path.join('..', 'gst', 'libs')

del devloc, sys, os, dl
