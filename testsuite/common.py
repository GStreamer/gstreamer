import dl
import os
import sys
import unittest

# Don't insert before .
sys.path.insert(1, os.path.join('..', 'gst'))

import ltihooks

# Load GST and make sure we load it from the current build
sys.setdlopenflags(dl.RTLD_LAZY | dl.RTLD_GLOBAL)

path = os.path.abspath(os.path.join('..', 'gst'))
import gst
assert gst.__path__ != path, 'bad path'

try:
   import gst.interfaces
   assert os.path.basename(gst.interfaces.__file__) != path, 'bad path'
except ImportError:
   pass

try:
   import gst.play
   assert os.path.basename(gst.play.__file__) != path, 'bad path'
except ImportError:
   pass
   


