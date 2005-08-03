try:
    from dl import RTLD_LAZY, RTLD_GLOBAL
except ImportError:
    # dl doesn't seem to be available on 64bit systems
    try:
        from DLFCN import RTLD_LAZY, RTLD_GLOBAL
    except ImportError:
        pass

import os
import sys

import pygtk
pygtk.require('2.0')

import gobject
try:
    gobject.threads_init()
except:
    print "WARNING: gobject doesn't have threads_init, no threadsafety"

# Don't insert before .
# sys.path.insert(1, os.path.join('..'))

# Load GST and make sure we load it from the current build
sys.setdlopenflags(RTLD_LAZY | RTLD_GLOBAL)

# Hack
sys.argv.append('--gst-debug-no-color')

topbuilddir = os.path.abspath(os.path.join('..'))
topsrcdir = os.path.abspath(os.path.join('..'))
if topsrcdir.endswith('_build'):
    topsrcdir = os.path.dirname(topsrcdir)

# gst's __init__.py is in topsrcdir/gst
path = os.path.abspath(os.path.join(topsrcdir, 'gst'))
import gst
file = gst.__file__
assert file.startswith(path), 'bad gst path: %s' % file

# gst's interfaces and play are in topbuilddir/gst
path = os.path.abspath(os.path.join(topbuilddir, 'gst'))
try:
   import gst.interfaces
except ImportError:
   # hack: we import it from our builddir/gst/.libs instead; ugly
   import interfaces
   gst.interfaces = interfaces
file = gst.interfaces.__file__
assert file.startswith(path), 'bad gst.interfaces path: %s' % file

try:
   import gst.play
   assert os.path.basename(gst.play.__file__) != path, 'bad path'
except ImportError:
   # hack: we import it from our builddir/gst/.libs instead; ugly
   import play
   gst.play = play
   pass
file = gst.play.__file__
assert file.startswith(path), 'bad gst.play path: %s' % file

# testhelper needs ltihooks
import ltihooks
import testhelper
ltihooks.uninstall()

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
