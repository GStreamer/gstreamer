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
import inspect
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
    def __init__(self, *args, **kwargs):
        self._real_chain_func = None
        self._real_event_func = None
        self._real_query_func = None
        super(Gst.Pad, self).__init__(*args, **kwargs)

    def _chain_override(self, pad, parent, buf):
        return self._real_chain_func(pad, buf)

    def _event_override(self, pad, parent, event):
        return self._real_event_func(pad, event)

    def _query_override(self, pad, parent, query):
        query.mini_object.refcount -= 1
        try:
            res = self._real_query_func(pad, query)
        except TypeError:
            try:
                res = self._real_query_func(pad, parent, query)
            except TypeError:
                raise TypeError("Invalid query method %s, 2 or 3 arguments required"
                                % self._real_query_func)
        query.mini_object.refcount += 1

        return res

    def set_chain_function(self, func):
        self._real_chain_func = func
        self.set_chain_function_full(self._chain_override, None)

    def set_event_function(self, func):
        self._real_event_func = func
        self.set_event_function_full(self._event_override, None)

    def set_query_function(self, func):
        self._real_query_func = func
        self.set_query_function_full(self._chain_override, None)

    def set_query_function_full(self, func, udata):
        self._real_query_func = func
        self._real_set_query_function_full(self._query_override, None)

    def query_caps(self, filter=None):
        return Gst.Pad.query_caps(self, filter)

    def link(self, pad):
        ret = Gst.Pad.link(self, pad)
        if ret != Gst.PadLinkReturn.OK:
            raise LinkError(ret)
        return ret

Pad._real_set_query_function_full = Gst.Pad.set_query_function_full
Pad = override(Pad)
__all__.append('Pad')

class GhostPad(Gst.GhostPad):
    def __init__(self, name, target=None, direction=None):
        if direction is None:
            if target is None:
                raise TypeError('you must pass at least one of target '
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
        return '<Gst.Fraction %s>' % (str(self))

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
        raise TypeError("%s is not supported, use Gst.Fraction or int." %
                (type(other)))

    __rmul__ = __mul__

    def __truediv__(self, other):
        if isinstance(other, Fraction):
            return Fraction(self.num * other.denom,
                            self.denom * other.num)
        elif isinstance(other, int):
            return Fraction(self.num, self.denom * other)
        return TypeError("%s is not supported, use Gst.Fraction or int." %
                (type(other)))

    __div__ = __truediv__

    def __rtruediv__(self, other):
        if isinstance(other, int):
            return Fraction(self.denom * other, self.num)
        return TypeError("%s is not an int." % (type(other)))

    __rdiv__ = __rtruediv__

    def __float__(self):
        return float(self.num) / float(self.denom)

    def __str__(self):
        return '%d/%d' % (self.num, self.denom)

Fraction = override(Fraction)
__all__.append('Fraction')


class IntRange(Gst.IntRange):
    def __init__(self, r):
        if not isinstance(r, range):
            raise TypeError("%s is not a range." % (type(r)))

        if (r.start >= r.stop):
            raise TypeError("Range start must be smaller then stop")

        if r.start % r.step != 0:
            raise TypeError("Range start must be a multiple of the step")

        if r.stop % r.step != 0:
            raise TypeError("Range stop must be a multiple of the step")

        self.range = r

    def __repr__(self):
        return '<Gst.IntRange [%d,%d,%d]>' % (self.range.start,
                self.range.stop, self.range.step)

    def __str__(self):
        if self.range.step == 1:
            return '[%d,%d]' % (self.range.start, self.range.stop)
        else:
            return '[%d,%d,%d]' % (self.range.start, self.range.stop,
                    self.range.step)

IntRange = override(IntRange)
__all__.append('IntRange')


class Int64Range(Gst.Int64Range):
    def __init__(self, r):
        if not isinstance(r, range):
            raise TypeError("%s is not a range." % (type(r)))

        if (r.start >= r.stop):
            raise TypeError("Range start must be smaller then stop")

        if r.start % r.step != 0:
            raise TypeError("Range start must be a multiple of the step")

        if r.stop % r.step != 0:
            raise TypeError("Range stop must be a multiple of the step")

        self.range = r

    def __repr__(self):
        return '<Gst.Int64Range [%d,%d,%d]>' % (self.range.start,
                self.range.stop, self.range.step)

    def __str__(self):
        if self.range.step == 1:
            return '(int64)[%d,%d]' % (self.range.start, self.range.stop)
        else:
            return '(int64)[%d,%d,%d]' % (self.range.start, self.range.stop,
                    self.range.step)


Int64Range = override(Int64Range)
__all__.append('Int64Range')


class DoubleRange(Gst.DoubleRange):
    def __init__(self, start, stop):
        self.start = float(start)
        self.stop = float(stop)

        if (start >= stop):
            raise TypeError("Range start must be smaller then stop")

    def __repr__(self):
        return '<Gst.DoubleRange [%s,%s]>' % (str(self.start), str(self.stop))

    def __str__(self):
        return '(double)[%s,%s]' % (str(self.range.start), str(self.range.stop))


DoubleRange = override(DoubleRange)
__all__.append('DoubleRange')


class FractionRange(Gst.FractionRange):
    def __init__(self, start, stop):
        if not isinstance(start, Gst.Fraction):
            raise TypeError("%s is not a Gst.Fraction." % (type(start)))

        if not isinstance(stop, Gst.Fraction):
            raise TypeError("%s is not a Gst.Fraction." % (type(stop)))

        if (float(start) >= float(stop)):
            raise TypeError("Range start must be smaller then stop")

        self.start = start
        self.stop = stop

    def __repr__(self):
        return '<Gst.FractionRange [%s,%s]>' % (str(self.start),
                str(self.stop))

    def __str__(self):
        return '(fraction)[%s,%s]' % (str(self.start), str(self.stop))

FractionRange = override(FractionRange)
__all__.append('FractionRange')


class ValueArray(Gst.ValueArray):
    def __init__(self, array):
        self.array = list(array)

    def __getitem__(self, index):
        return self.array[index]

    def __setitem__(self, index, value):
        self.array[index] = value

    def __len__(self):
        return len(self.array)

    def __str__(self):
        return '<' + ','.join(map(str,self.array)) + '>'

    def __repr__(self):
        return '<Gst.ValueArray %s>' % (str(self))

ValueArray = override(ValueArray)
__all__.append('ValueArray')


class ValueList(Gst.ValueList):
    def __init__(self, array):
        self.array = list(array)

    def __getitem__(self, index):
        return self.array[index]

    def __setitem__(self, index, value):
        self.array[index] = value

    def __len__(self):
        return len(self.array)

    def __str__(self):
        return '{' + ','.join(map(str,self.array)) + '}'

    def __repr__(self):
        return '<Gst.ValueList %s>' % (str(self))

ValueList = override(ValueList)
__all__.append('ValueList')


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
Gst.trace = _gi_gst.trace
Gst.log = _gi_gst.log
Gst.debug = _gi_gst.debug
Gst.info = _gi_gst.info
Gst.warning = _gi_gst.warning
Gst.error = _gi_gst.error
Gst.fixme = _gi_gst.fixme
Gst.memdump = _gi_gst.memdump

# Make sure PyGst is not usable if GStreamer has not been initialized
class NotInitialized(Exception):
    pass
__all__.append('NotInitialized')

def fake_method(*args):
    raise NotInitialized("Please call Gst.init(argv) before using GStreamer")


real_functions = [o for o in inspect.getmembers(Gst) if isinstance(o[1], type(Gst.init))]

class_methods = []
for cname_klass in [o for o in inspect.getmembers(Gst) if isinstance(o[1], type(Gst.Element)) or isinstance(o[1], type(Gst.Caps))]:
    class_methods.append((cname_klass,
                         [(o, cname_klass[1].__dict__[o])
                          for o in cname_klass[1].__dict__
                          if isinstance(cname_klass[1].__dict__[o], type(Gst.init))]))

def init_pygst():
    for fname, function in real_functions:
        if fname not in ["init", "init_check", "deinit"]:
            setattr(Gst, fname, function)

    for cname_class, methods in class_methods:
        for mname, method in methods:
            setattr(cname_class[1], mname, method)


def deinit_pygst():
    for fname, func in real_functions:
        if fname not in ["init", "init_check", "deinit"]:
            setattr(Gst, fname, fake_method)
    for cname_class, methods in class_methods:
        for mname, method in methods:
            setattr(cname_class[1], mname, fake_method)

real_init = Gst.init
def init(argv):
    init_pygst()
    return real_init(argv)
Gst.init = init

real_init_check = Gst.init_check
def init_check(argv):
    init_pygst()
    return real_init_check(argv)
Gst.init_check = init_check

real_deinit = Gst.deinit
def deinit():
    deinit_pygst()
    return real_deinit()

Gst.deinit = deinit

deinit_pygst()
