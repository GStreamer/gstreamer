# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Development Utilities
#
#  Copyright (C) 2007 René Stadler <mail@renestadler.de>
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 3 of the License, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program.  If not, see <http://www.gnu.org/licenses/>.

"""GStreamer development utilities common Data module"""

import pygtk
pygtk.require ("2.0")

import gobject

class Dispatcher (object):

    def __call__ (self, iterator):

        raise NotImplementedError ("derived classes must override this method")

class DefaultDispatcher (Dispatcher):

    def __call__ (self, iterator):

        for x in iterator:
            pass

class GSourceDispatcher (Dispatcher):

    def __init__ (self):

        Dispatcher.__init__ (self)

        self.source_id = None

    def __call__ (self, iterator):

        if self.source_id is not None:
            gobject.source_remove (self.source_id)

        self.source_id = gobject.idle_add (iterator.next)
