# Copyright (c) 2018, Matthew Waters <matthew@centricular.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

class Signal(object):
    def __init__(self, cont_func=None, accum_func=None):
        self._handlers = []
        if not cont_func:
            # by default continue when None/no return value is provided or
            # True is returned
            cont_func = lambda x: x is None or x
        self.cont_func = cont_func
        # default to accumulating truths
        if not accum_func:
            accum_func = lambda prev, v: prev and v
        self.accum_func = accum_func

    def connect(self, handler):
        self._handlers.append(handler)

    def disconnect(self, handler):
        self._handlers.remove(handler)

    def fire(self, *args):
        ret = None
        for handler in self._handlers:
            ret = self.accum_func(ret, handler(*args))
            if not self.cont_func(ret):
                break
        return ret
