# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python
# Copyright (C) 2002 David I. Lehn
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>

__ltihooks_used__ = False
try:
    import ltihooks
    __ltihooks_used__ = True
except:
   pass

import sys

# we always require 2.0 of pygtk; so if pygtk is not imported anywhere
# yet, we import pygtk here and .require
if 'gobject' not in sys.modules:
    import pygtk
    pygtk.require('2.0')

class Value:
   def __init__(self, type):
      assert type in ('fourcc', 'intrange', 'doublerange', 'fractionrange', 'fraction')
      self.type = type

class Fourcc(Value):
   def __init__(self, string):
      Value.__init__(self, 'fourcc')
      self.fourcc = string
   def __repr__(self):
      return '<gst.Fourcc %s>' % self.fourcc

class IntRange(Value):
   def __init__(self, low, high):
      Value.__init__(self, 'intrange')
      self.low = low
      self.high = high
   def __repr__(self):
      return '<gst.IntRange [%d, %d]>' % (self.low, self.high)

class DoubleRange(Value):
   def __init__(self, low, high):
      Value.__init__(self, 'doublerange')
      self.low = low
      self.high = high
   def __repr__(self):
      return '<gst.DoubleRange [%f, %f]>' % (self.low, self.high)

class FractionRange(Value):
   def __init__(self, low, high):
      Value.__init__(self, 'fractionrange')
      self.low = low
      self.high = high
   def __repr__(self):
      return '<gst.FractionRange [%d/%d, %d/%d]>' % (self.low.num,
                                                     self.low.denom,
                                                     self.high.num,
                                                     self.high.denom)

class Fraction(Value):
   def __init__(self, num, denom):
      Value.__init__(self, 'fraction')
      self.num = num
      self.denom = denom
   def __repr__(self):
      return '<gst.Fraction %d/%d>' % (self.num, self.denom)

import DLFCN, sys
dlsave = sys.getdlopenflags()
sys.setdlopenflags(DLFCN.RTLD_LAZY | DLFCN.RTLD_GLOBAL)

from _gst import *
import interfaces

version = get_gst_version

sys.setdlopenflags(dlsave)
del DLFCN, sys

# this restores previously installed importhooks, so we don't interfere
# with other people's module importers
# it also clears out the module completely as if it were never loaded,
# so that if anyone else imports ltihooks the hooks get installed
if __ltihooks_used__:
    ltihooks.uninstall()
    __ltihooks_used__ = False
    del ltihooks
    import sys
    del sys.modules['ltihooks']

