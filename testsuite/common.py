import dl
import os
import sys
import unittest

import pygtk
pygtk.require('2.0')

import gobject
try:
    gobject.threads_init()
except:
    print "WARNING: gobject doesn't have threads_init, no threadsafety"

# Don't insert before .
sys.path.insert(1, os.path.join('..'))

# Load GST and make sure we load it from the current build
sys.setdlopenflags(dl.RTLD_LAZY | dl.RTLD_GLOBAL)

# Hack
sys.argv.append('--gst-debug-no-color')

path = os.path.abspath(os.path.join('..', 'gst'))
import gst

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

_stderr = None

def disable_stderr():
    global _stderr
    _stderr = file('/tmp/stderr', 'w+')
    sys.stderr = os.fdopen(os.dup(2), 'w')
    os.close(2)
    os.dup(_stderr.fileno())

def enable_stderr():
    global _stderr
    
    os.close(2)
    os.dup(sys.stderr.fileno())
    _stderr.seek(0, 0)
    data = _stderr.read()
    _stderr.close()
    os.remove('/tmp/stderr')
    return data

def run_silent(function, *args, **kwargs):
   disable_stderr()

   try:
      function(*args, **kwargs)
   except Exception, exc:
      enable_stderr()
      raise exc
   
   output = enable_stderr()

   return output
