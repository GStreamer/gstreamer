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
#
# SPDX-License-Identifier: LGPL-2.0-or-later

import sys
import inspect
import itertools
import weakref
from ..overrides import override
from ..module import get_introspection_module

from gi.repository import GLib


Gst = get_introspection_module('Gst')

__all__ = []

if Gst._version == '0.10':
    import warnings
    warn_msg = "You have imported the Gst 0.10 module.  Because Gst 0.10 \
was not designed for use with introspection some of the \
interfaces and API will fail.  As such this is not supported \
by the GStreamer development team and we encourage you to \
port your app to Gst 1 or greater. gst-python is the recommended \
python module to use with Gst 0.10"

    warnings.warn(warn_msg, RuntimeWarning)


# Ensuring that PyGObject loads the URIHandler interface
# so we can force our own implementation soon enough (in gstmodule.c)
class URIHandler(Gst.URIHandler):
    pass


URIHandler = override(URIHandler)
__all__.append('URIHandler')


class Element(Gst.Element):
    @staticmethod
    def link_many(*args):
        '''
        @raises: Gst.LinkError
        '''
        for pair in pairwise(args):
            if not pair[0].link(pair[1]):
                raise LinkError(
                    'Failed to link {} and {}'.format(pair[0], pair[1]))


Element = override(Element)
__all__.append('Element')


class Bin(Gst.Bin):
    def __init__(self, name=None):
        Gst.Bin.__init__(self, name=name)

    def add(self, *args):
        for arg in args:
            if not Gst.Bin.add(self, arg):
                raise AddError(arg)

    def make_and_add(self, factoryname, name=None):
        '''
        @raises: Gst.AddError
        '''
        elem = Gst.ElementFactory.make(factoryname, name)
        if not elem:
            raise AddError(
                'No such element: {}'.format(factoryname))
        self.add(elem)
        return elem


Bin = override(Bin)
__all__.append('Bin')


class Caps(Gst.Caps):

    def __nonzero__(self):
        return not self.is_empty()

    def __new__(cls, *args):
        if not args:
            return Caps.new_empty()
        elif len(args) > 1:
            raise TypeError("wrong arguments when creating GstCaps object")
        elif isinstance(args[0], str):
            return Caps.from_string(args[0])
        elif isinstance(args[0], Caps):
            return args[0].copy()
        elif isinstance(args[0], Structure):
            res = Caps.new_empty()
            res.append_structure(args[0])
            return res
        elif isinstance(args[0], (list, tuple)):
            res = Caps.new_empty()
            for e in args[0]:
                res.append_structure(e)
            return res

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


class PadFunc:
    def __init__(self, func):
        self.func = func

    def __call__(self, pad, parent, obj):
        if isinstance(self.func, weakref.WeakMethod):
            func = self.func()
        else:
            func = self.func

        try:
            res = func(pad, obj)
        except TypeError:
            try:
                res = func(pad, parent, obj)
            except TypeError:
                raise TypeError("Invalid method %s, 2 or 3 arguments required"
                                % func)

        return res


class Pad(Gst.Pad):
    def __init__(self, *args, **kwargs):
        super(Gst.Pad, self).__init__(*args, **kwargs)

    def set_chain_function(self, func):
        self.set_chain_function_full(PadFunc(func), None)

    def set_event_function(self, func):
        self.set_event_function_full(PadFunc(func), None)

    def set_query_function(self, func):
        self.set_query_function_full(PadFunc(func), None)

    def query_caps(self, filter=None):
        return Gst.Pad.query_caps(self, filter)

    def set_caps(self, caps):
        if not isinstance(caps, Gst.Caps):
            raise TypeError("%s is not a Gst.Caps." % (type(caps)))

        if not caps.is_fixed():
            return False

        event = Gst.Event.new_caps(caps)

        if self.direction == Gst.PadDirection.SRC:
            res = self.push_event(event)
        else:
            res = self.send_event(event)

        return res

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


class MapError(Exception):
    pass


__all__.append('MapError')


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
    def make(cls, factoryname, name=None):
        return Gst.ElementFactory.make(factoryname, name)


class Pipeline(Gst.Pipeline):
    def __init__(self, name=None):
        Gst.Pipeline.__init__(self, name=name)


Pipeline = override(Pipeline)
__all__.append('Pipeline')


class Structure(Gst.Structure):
    def __new__(cls, *args, **kwargs):
        if not args:
            if kwargs:
                raise TypeError("wrong arguments when creating GstStructure, first argument"
                                " must be the structure name.")
            return Structure.new_empty()
        elif len(args) > 1:
            raise TypeError("wrong arguments when creating GstStructure object")
        elif isinstance(args[0], str):
            if not kwargs:
                return Structure.from_string(args[0])[0]
            struct = Structure.new_empty(args[0])
            for k, v in kwargs.items():
                struct[k] = v

            return struct
        elif isinstance(args[0], Structure):
            return args[0].copy()

        raise TypeError("wrong arguments when creating GstStructure object")

    def __init__(self, *args, **kwargs):
        pass

    def __getitem__(self, key):
        return self.get_value(key)

    def keys(self):
        keys = set()

        def foreach(fid, value, unused1, udata):
            keys.add(GLib.quark_to_string(fid))
            return True

        self.foreach(foreach, None, None)
        return keys

    def __setitem__(self, key, value):
        return self.set_value(key, value)

    def __str__(self):
        return self.to_string()


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

    def __eq__(self, other):
        if isinstance(other, range):
            return self.range == other
        elif isinstance(other, IntRange):
            return self.range == other.range
        return False


if sys.version_info >= (3, 0):
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

    def __eq__(self, other):
        if isinstance(other, range):
            return self.range == other
        elif isinstance(other, IntRange):
            return self.range == other.range
        return False


class Bitmask(Gst.Bitmask):
    def __init__(self, v):
        if not isinstance(v, int):
            raise TypeError("%s is not an int." % (type(v)))

        self.v = int(v)

    def __str__(self):
        return hex(self.v)

    def __eq__(self, other):
        return self.v == other


Bitmask = override(Bitmask)
__all__.append('Bitmask')


if sys.version_info >= (3, 0):
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
        return '<' + ','.join(map(str, self.array)) + '>'

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
        return '{' + ','.join(map(str, self.array)) + '}'

    def __repr__(self):
        return '<Gst.ValueList %s>' % (str(self))


ValueList = override(ValueList)
__all__.append('ValueList')

# From https://docs.python.org/3/library/itertools.html


def pairwise(iterable):
    a, b = itertools.tee(iterable)
    next(b, None)
    return zip(a, b)


class MapInfo:
    def __init__(self):
        self.memory = None
        self.flags = Gst.MapFlags(0)
        self.size = 0
        self.maxsize = 0
        self.data = None
        self.user_data = None
        self.__parent__ = None

    def __iter__(self):
        # Make it behave like a tuple similar to the PyGObject generated API for
        # the `Gst.Buffer.map()` and friends.
        for i in (self.__parent__ is not None, self):
            yield i

    def __enter__(self):
        if not self.__parent__:
            raise MapError('MappingError', 'Mapping was not successful')

        return self

    def __exit__(self, type, value, tb):
        if not self.__parent__.unmap(self):
            raise MapError('MappingError', 'Unmapping was not successful')


__all__.append("MapInfo")


class Buffer(Gst.Buffer):

    def map_range(self, idx, length, flags):
        mapinfo = MapInfo()
        if (_gi_gst.buffer_override_map_range(self, mapinfo, idx, length, int(flags))):
            mapinfo.__parent__ = self

        return mapinfo

    def map(self, flags):
        mapinfo = MapInfo()
        if _gi_gst.buffer_override_map(self, mapinfo, int(flags)):
            mapinfo.__parent__ = self

        return mapinfo

    def unmap(self, mapinfo):
        mapinfo.__parent__ = None
        return _gi_gst.buffer_override_unmap(self, mapinfo)


Buffer = override(Buffer)
__all__.append('Buffer')


class Memory(Gst.Memory):

    def map(self, flags):
        mapinfo = MapInfo()
        if (_gi_gst.memory_override_map(self, mapinfo, int(flags))):
            mapinfo.__parent__ = self

        return mapinfo

    def unmap(self, mapinfo):
        mapinfo.__parent__ = None
        return _gi_gst.memory_override_unmap(self, mapinfo)


Memory = override(Memory)
__all__.append('Memory')


def TIME_ARGS(time):
    if time == Gst.CLOCK_TIME_NONE:
        return "CLOCK_TIME_NONE"

    return "%u:%02u:%02u.%09u" % (time / (Gst.SECOND * 60 * 60),
                                  (time / (Gst.SECOND * 60)) % 60,
                                  (time / Gst.SECOND) % 60,
                                  time % Gst.SECOND)


__all__.append('TIME_ARGS')

from gi.overrides import _gi_gst
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

pre_init_functions = set([
    "init",
    "init_check",
    "deinit",
    "is_initialized",
    "debug_add_log_function",
    "debug_add_ring_buffer_logger",
    "debug_remove_log_function",
    "debug_remove_log_function_by_data",
    "debug_remove_ring_buffer_logger",
    "debug_set_active",
    "debug_set_color_mode",
    "debug_set_color_mode_from_string",
    "debug_set_colored",
    "debug_set_default_threshold",
])


def init_pygst():
    for fname, function in real_functions:
        if fname not in ["init", "init_check", "deinit"]:
            setattr(Gst, fname, function)

    for cname_class, methods in class_methods:
        for mname, method in methods:
            setattr(cname_class[1], mname, method)


def deinit_pygst():
    for fname, func in real_functions:
        if fname not in pre_init_functions:
            setattr(Gst, fname, fake_method)
    for cname_class, methods in class_methods:
        for mname, method in methods:
            setattr(cname_class[1], mname, fake_method)


real_init = Gst.init


def init(argv=None):
    init_pygst()

    if Gst.is_initialized():
        return True

    return real_init(argv)


Gst.init = init

real_init_check = Gst.init_check


def init_check(argv):
    init_pygst()
    if Gst.is_initialized():
        return True

    return real_init_check(argv)


Gst.init_check = init_check

real_deinit = Gst.deinit


def deinit():
    deinit_pygst()
    return real_deinit()


def init_python():
    if not Gst.is_initialized():
        raise NotInitialized("Gst.init_python should never be called before GStreamer itself is initialized")

    init_pygst()


Gst.deinit = deinit
Gst.init_python = init_python

if not Gst.is_initialized():
    deinit_pygst()
