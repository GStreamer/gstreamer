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

"""GStreamer Development Utilities Common Data module."""

import gi

from gi.repository import GObject


class Dispatcher (object):

    def __call__(self, iterator):

        raise NotImplementedError("derived classes must override this method")

    def cancel(self):

        pass


class DefaultDispatcher (Dispatcher):

    def __call__(self, iterator):

        for x in iterator:
            pass


class GSourceDispatcher (Dispatcher):

    def __init__(self):

        Dispatcher.__init__(self)

        self.source_id = None

    def __call__(self, iterator):

        if self.source_id is not None:
            GObject.source_remove(self.source_id)

        def iteration():
            r = iterator.__next__()
            if not r:
                self.source_id = None
            return r

        self.source_id = GObject.idle_add(
            iteration, priority=GObject.PRIORITY_LOW)

    def cancel(self):

        if self.source_id is None:
            return

        GObject.source_remove(self.source_id)
        self.source_id = None
