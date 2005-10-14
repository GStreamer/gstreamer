# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

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
import gc
import unittest

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

class TestCase(unittest.TestCase):

    _types = [gst.Object, gst.MiniObject]

    def gccollect(self):
        # run the garbage collector
        ret = 0
        gst.debug('garbage collecting')
        while True:
            c = gc.collect()
            ret += c
            if c == 0: break
        gst.debug('done garbage collecting, %d objects' % ret)
        return ret

    def gctrack(self):
        # store all gst objects in the gc in a tracking dict
        # call before doing any allocation in your test, from setUp
        gst.debug('tracking gc GstObjects for types %r' % self._types)
        self.gccollect()
        self._tracked = {}
        for c in self._types:
            self._tracked[c] = [o for o in gc.get_objects() if isinstance(o, c)]

    def gcverify(self):
        # verify no new gst objects got added to the gc
        # call after doing all cleanup in your test, from tearDown
        gst.debug('verifying gc GstObjects for types %r' % self._types)
        new = []
        for c in self._types:
            objs = [o for o in gc.get_objects() if isinstance(o, c)]
            new.extend([o for o in objs if o not in self._tracked[c]])

        self.failIf(new, new)
        #self.failIf(new, ["%r:%d" % (type(o), id(o)) for o in new])
        del self._tracked

    def setUp(self):
        """
        Override me by chaining up to me at the start of your setUp.
        """
        gst.debug('%s.%s' % (self.__class__.__name__, 
            self.__testMethodName))
        self.gctrack()

    def tearDown(self):
        """
        Override me by chaining up to me at the end of your tearDown.
        """
        gst.debug('%s.%s' % (self.__class__.__name__, 
            self.__testMethodName))
        self.gccollect()
        self.gcverify()
