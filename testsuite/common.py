# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

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

# imported from here by other tests
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
