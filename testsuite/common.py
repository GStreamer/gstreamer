#
import os
import sys
import unittest

sys.path.insert(0, '..')

# Load GST and make sure we load it from the current build
import gst
assert sys.modules.has_key('_gst')
assert os.path.basename(sys.modules['_gst'].__file__), \
       os.path.join('..', 'gst', 'libs')
