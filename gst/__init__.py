import sys
import os

"libtool lib location"
devloc = os.path.join(__path__[0],'.libs')

if os.path.exists(devloc):
   sys.path.append(devloc)

sys.setdlopenflags(1)
del devloc, sys, os

from _gstreamer import *
