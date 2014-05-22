# -*- Mode: Python; py-indent-offset: 4 -*-
# vim: tabstop=4 shiftwidth=4 expandtab
#
#       Gst.py
#
# Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
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
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3, or (at your option)
# any later version.

import sys
from ..overrides import override
from ..importer import modules

if sys.version_info >= (3, 0):
    _basestring = str
    _callable = lambda c: hasattr(c, '__call__')
else:
    _basestring = basestring
    _callable = callable

Gst = modules['Gst']._introspection_module
__all__ = []

if Gst._version == '0.10':
    import warnings
    warn_msg = "You have imported the Gst 0.10 module.  Because Gst 0.10 \
was not designed for use with introspection some of the \
interfaces and API will fail.  As such this is not supported \
by the GStreamer development team and we encourage you to \
port your app to Gst 1 or greater. gst-python is the recomended \
python module to use with Gst 0.10"

    warnings.warn(warn_msg, RuntimeWarning)

class Bin(Gst.Bin):
    def __init__(self, name=None):
        Gst.Bin.__init__(self, name=name)

    def add(self, *args):
        for arg in args:
            if not Gst.Bin.add(self, arg):
                raise AddError(arg)

Bin = override(Bin)
__all__.append('Bin')

class Caps(Gst.Caps):

    def __nonzero__(self):
        return not self.is_empty()

    def __new__(cls, *kwargs):
        if not kwargs:
            return Caps.new_empty()
        elif len(kwargs) > 1:
            raise TypeError("wrong arguments when creating GstCaps object")
        elif isinstance(kwargs[0], str):
            return Caps.from_string(kwargs[0])
        elif isinstance(kwargs[0], Caps):
            return kwargs[0].copy()

        raise TypeError("wrong arguments when creating GstCaps object")

    def __init__(self, *args, **kwargs):
        return super(Caps, self).__init__()

    def __str__(self):
        return self.to_string()

    def __getitem__(self, index):
        if index >= self.get_size():
            raise IndexError('structure index out of range')
        return self.get_structure(index)

    def __len__(self):
        return self.get_size()

Caps = override(Caps)
__all__.append('Caps')

class Pad(Gst.Pad):
    def query_caps(self, filter=None):
        return Gst.Pad.query_caps(self, filter)

    def link(self, pad):
        ret = Gst.Pad.link(self, pad)
        if ret != Gst.PadLinkReturn.OK:
            raise LinkError(ret)
        return ret

Pad = override(Pad)
__all__.append('Pad')

class GhostPad(Gst.GhostPad):
    def __init__(self, name, target=None, direction=None):
        if direction is None:
            if target is None:
                raise TypeError('you must pass at least one of target'
                        'and direction')
            direction = target.props.direction

        Gst.GhostPad.__init__(self, name=name, direction=direction)
        self.construct()
        if target is not None:
            self.set_target(target)

    def query_caps(self, filter=None):
        return Gst.GhostPad.query_caps(self, filter)

GhostPad = override(GhostPad)
__all__.append('GhostPad')

class IteratorError(Exception):
    pass
__all__.append('IteratorError')

class AddError(Exception):
    pass
__all__.append('AddError')

class LinkError(Exception):
    pass
__all__.append('LinkError')

class Iterator(Gst.Iterator):
    def __iter__(self):
        while True:
            result, value = self.next()
            if result == Gst.IteratorResult.DONE:
                break

            if result != Gst.IteratorResult.OK:
                raise IteratorError(result)

            yield value

Iterator = override(Iterator)
__all__.append('Iterator')


class ElementFactory(Gst.ElementFactory):

    # ElementFactory
    def get_longname(self):
        return self.get_metadata("long-name")

    def get_description(self):
        return self.get_metadata("description")

    def get_klass(self):
        return self.get_metadata("klass")

    @classmethod
    def make(cls, factory_name, instance_name=None):
        return Gst.ElementFactory.make(factory_name, instance_name)

class Pipeline(Gst.Pipeline):
    def __init__(self, name=None):
        Gst.Pipeline.__init__(self, name=name)

Pipeline = override(Pipeline)
__all__.append('Pipeline')

class Structure(Gst.Structure):
    def __getitem__(self, key):
        return self.get_value(key)
    def __setitem__(self, key, value):
        return self.set_value(key, value)

Structure = override(Structure)
__all__.append('Structure')

ElementFactory = override(ElementFactory)
__all__.append('ElementFactory')

class Fraction(Gst.Fraction):
    def __init__(self, num, denom=1):
        def __gcd(a, b):
            while b != 0:
                tmp = a
                a = b
                b = tmp % b
            return abs(a)

        def __simplify():
            num = self.num
            denom = self.denom

            if num < 0:
                num = -num
                denom = -denom

            # Compute greatest common divisor
            gcd = __gcd(num, denom)
            if gcd != 0:
                num /= gcd
                denom /= gcd

            self.num = num
            self.denom = denom

        self.num = num
        self.denom = denom

        __simplify()
        self.type = "fraction"

    def __repr__(self):
        return '<Gst.Fraction %d/%d>' % (self.num, self.denom)

    def __value__(self):
        return self.num / self.denom

    def __eq__(self, other):
        if isinstance(other, Fraction):
            return self.num * other.denom == other.num * self.denom
        return False

    def __ne__(self, other):
        return not self.__eq__(other)

    def __mul__(self, other):
        if isinstance(other, Fraction):
            return Fraction(self.num * other.num,
                            self.denom * other.denom)
        elif isinstance(other, int):
            return Fraction(self.num * other, self.denom)
        raise TypeError

    __rmul__ = __mul__

    def __truediv__(self, other):
        if isinstance(other, Fraction):
            return Fraction(self.num * other.denom,
                            self.denom * other.num)
        elif isinstance(other, int):
            return Fraction(self.num, self.denom * other)
        return TypeError

    __div__ = __truediv__

    def __rtruediv__(self, other):
        if isinstance(other, int):
            return Fraction(self.denom * other, self.num)
        return TypeError

    __rdiv__ = __rtruediv__

    def __float__(self):
        return float(self.num) / float(self.denom)

Fraction = override(Fraction)
__all__.append('Fraction')

def TIME_ARGS(time):
    if time == Gst.CLOCK_TIME_NONE:
        return "CLOCK_TIME_NONE"

    return "%u:%02u:%02u.%09u" % (time / (Gst.SECOND * 60 * 60),
                                  (time / (Gst.SECOND * 60)) % 60,
                                  (time / Gst.SECOND) % 60,
                                  time % Gst.SECOND)
__all__.append('TIME_ARGS')

from . import _gi_gst
_gi_gst

# maybe more python and less C some day if core turns a bit more introspection
# and binding friendly in the debug area
Gst.log = _gi_gst.log
Gst.debug = _gi_gst.debug
Gst.info = _gi_gst.info
Gst.warning = _gi_gst.warning
Gst.error = _gi_gst.error
Gst.fixme = _gi_gst.fixme
Gst.memdump = _gi_gst.memdump
