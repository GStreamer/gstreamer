#!/usr/bin/env python
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

import glob
import os
import sys
import unittest

SKIP_FILES = ['common', 'runtests']

def gettestnames(which):
    if not which:
        dir = os.path.split(os.path.abspath(__file__))[0]
        which = [os.path.basename(p) for p in glob.glob('%s/*.py' % dir)]
    
    names = map(lambda x: x[:-3], which)
    for f in SKIP_FILES:
        if f in names:
            names.remove(f)
    return names
        
suite = unittest.TestSuite()
loader = unittest.TestLoader()

for name in gettestnames(sys.argv[1:]):
    suite.addTest(loader.loadTestsFromName(name))
    
descriptions = 1
verbosity = 1
if os.environ.has_key('VERBOSE'):
    descriptions = 2
    verbosity = 2

testRunner = unittest.TextTestRunner(descriptions=descriptions,
    verbosity=verbosity)
result = testRunner.run(suite)
if result.failures or result.errors:
    sys.exit(1)
