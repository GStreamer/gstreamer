# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
# Copyright (C) 2005 Thomas Vander Stichele
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

from common import gobject, gst, unittest

# see
# http://www.sicem.biz/personal/lgs/docs/gobject-python/gobject-tutorial.html
class MyBin(gst.Bin):
    _state_changed = False

    def __init__(self, name):
        # we need to call GObject's init to be able to do self.do_*
        gobject.GObject.__init__(self)
        # since we can't chain up to our parent's __init__, we set the
        # name manually 
        self.set_property('name', name)

    def do_change_state(self, state_change):
        if state_change == gst.STATE_CHANGE_PAUSED_TO_PLAYING:
            self._state_changed = True

        # chain up to parent
        return gst.Bin.do_change_state(self, state_change)

# we need to register the type for PyGTK < 2.8
gobject.type_register(MyBin)

class BinSubclassTest(unittest.TestCase):
    def testStateChange(self):
        bin = MyBin("mybin")
        self.assertEquals(bin.get_name(), "mybin")
        bin.set_state(gst.STATE_PLAYING)
        self.failUnless(bin._state_changed)

if __name__ == "__main__":
    unittest.main()
