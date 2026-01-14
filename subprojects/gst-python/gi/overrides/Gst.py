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

from __future__ import annotations

import sys
import inspect
import itertools
import weakref
import typing
import gi

gi.require_version('GLib', '2.0')
gi.require_version('GObject', '2.0')
from gi.repository import GLib, GObject
from gi.overrides import override

# Typing relies on https://github.com/pygobject/pygobject-stubs.
if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    #
    # This causes some weirdness because stubs contains overridden APIs
    # signatures. For example when using Gst.Bin.add() here, we mean to call the
    # g-i generated API which does not have the same signature as our override
    # Bin.add(). The type checker will use signature from stubs which is our
    # override signature.
    from gi.repository import Gst
    from typing_extensions import Self

    # Type annotations cannot have `Gst.` prefix because they are copied into
    # Gst stubs module which cannot refer to itself. Use type aliases.
    MiniObject = Gst.MiniObject
    MiniObjectFlags = Gst.MiniObjectFlags
    FlowReturn = Gst.FlowReturn
    PadDirection = Gst.PadDirection
    PadLinkReturn = Gst.PadLinkReturn
    MapFlags = Gst.MapFlags
    BufferFlags = Gst.BufferFlags
else:
    from gi.module import get_introspection_module
    Gst = get_introspection_module('Gst')


__all__ = []


if Gst.VERSION_MAJOR < 1:
    import warnings
    warn_msg = "You have imported the Gst 0.10 module.  Because Gst 0.10 \
was not designed for use with introspection some of the \
interfaces and API will fail.  As such this is not supported \
by the GStreamer development team and we encourage you to \
port your app to Gst 1 or greater. gst-python is the recommended \
python module to use with Gst 0.10"

    warnings.warn(warn_msg, RuntimeWarning)


class Float(float):
    '''
    A wrapper to force conversion to G_TYPE_FLOAT instead of G_TYPE_DOUBLE when
    used in e.g. Gst.ValueArray.
    '''
    __gtype__ = GObject.TYPE_FLOAT


__all__.append('Float')


# Ensuring that PyGObject loads the URIHandler interface
# so we can force our own implementation soon enough (in gstmodule.c)
class URIHandler(Gst.URIHandler):
    pass


override(URIHandler)
__all__.append('URIHandler')


class Element(Gst.Element):
    @staticmethod
    def link_many(*args: Element) -> None:  # type: ignore[override]
        '''
        :raises Gst.LinkError
        '''
        for pair in pairwise(args):
            if not pair[0].link(pair[1]):
                raise LinkError(f'Failed to link {pair[0]} and {pair[1]}')


override(Element)
__all__.append('Element')


class Bin(Gst.Bin):
    def __init__(self, name: typing.Optional[str] = None):
        Gst.Bin.__init__(self, name=name)

    def add(self, *args: Element) -> None:  # type: ignore[override]
        for arg in args:
            if not Gst.Bin.add(self, arg):  # type: ignore[func-returns-value]
                raise AddError(arg)

    def make_and_add(self, factoryname: str, name: typing.Optional[str] = None) -> Element:
        '''
        :raises Gst.AddError:
        :raises Gst.MissingPluginError:
        '''
        elem = ElementFactory.make(factoryname, name)
        self.add(elem)
        return elem


override(Bin)
__all__.append('Bin')


class NotWritableMiniObject(Exception):
    pass


__all__.append('NotWritableMiniObject')


class MiniObjectMixin:
    def make_writable(self) -> bool:
        return _gi_gst.mini_object_make_writable(self)

    def is_writable(self) -> bool:
        return _gi_gst.mini_object_is_writable(self)

    @property
    def flags(self) -> MiniObjectFlags:
        return _gi_gst.mini_object_flags(self)

    @flags.setter
    def flags(self, flags: MiniObjectFlags) -> None:
        _gi_gst.mini_object_set_flags(self, flags)

    def __ptr__(self):
        return _gi_gst._get_object_ptr(self)


__all__.append('MiniObjectMixin')


class NotWritableQuery(Exception):
    pass


__all__.append('NotWritableQuery')


class Query(MiniObjectMixin, Gst.Query):  # type: ignore[misc]
    def get_structure(self) -> typing.Optional[Structure]:
        s = _gi_gst.query_get_structure(self)
        return s._set_parent(self) if s is not None else None

    def writable_structure(self) -> StructureContextManager:  # type: ignore[override]
        return StructureContextManager(_gi_gst.query_writable_structure(self), self)  # type: ignore[arg-type]


override(Query)
__all__.append('Query')


class NotWritableEvent(Exception):
    pass


__all__.append('NotWritableEvent')


class Event(MiniObjectMixin, Gst.Event):  # type: ignore[misc]
    def get_structure(self) -> typing.Optional[Structure]:
        s = _gi_gst.event_get_structure(self)
        return s._set_parent(self) if s is not None else None

    def writable_structure(self) -> StructureContextManager:  # type: ignore[override]
        return StructureContextManager(_gi_gst.event_writable_structure(self), self)  # type: ignore[arg-type]


override(Event)
__all__.append('Event')


class NotWritableContext(Exception):
    pass


__all__.append('NotWritableContext')


class Context(MiniObjectMixin, Gst.Context):  # type: ignore[misc]
    def get_structure(self) -> Structure:
        s = _gi_gst.context_get_structure(self)
        return s._set_parent(self)

    def writable_structure(self) -> StructureContextManager:  # type: ignore[override]
        return StructureContextManager(_gi_gst.context_writable_structure(self), self)  # type: ignore[arg-type]


override(Context)
__all__.append('Context')


class NotWritableCaps(Exception):
    pass


__all__.append('NotWritableCaps')


class NotWritableStructure(Exception):
    pass


__all__.append('NotWritableStructure')


class Caps(MiniObjectMixin, Gst.Caps):  # type: ignore[misc]

    def __nonzero__(self):
        return not self.is_empty()

    def __new__(cls, *args):
        if not args:
            return Caps.new_empty()
        if len(args) > 1:
            raise TypeError("wrong arguments when creating GstCaps object")

        assert len(args) == 1
        if isinstance(args[0], str):
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

    def __str__(self) -> str:
        return self.to_string()

    def __getitem__(self, index: int) -> Structure:
        return self.get_structure(index)

    def __iter__(self) -> typing.Iterator[Structure]:
        for i in range(self.get_size()):
            yield self.get_structure(i)

    def __len__(self) -> int:
        return self.get_size()

    def get_structure(self, index: int) -> Structure:
        if index >= self.get_size():
            raise IndexError('structure index out of range')
        s = _gi_gst.caps_get_structure(self, index)
        return s._set_parent(self)

    def writable_structure(self, index: int) -> StructureContextManager:  # type: ignore[override]
        return StructureContextManager(_gi_gst.caps_writable_structure(self, index), self)  # type: ignore[arg-type]


override(Caps)
__all__.append('Caps')


class PadProbeInfoObjectContextManager:
    def __init__(self, object: MiniObject, info: PadProbeInfo):
        self.__object = object
        self.__info = info

    def __enter__(self) -> MiniObject:
        return self.__object

    def __exit__(self, _type, _value, _tb):
        self.__info.set_object(self.__object)
        self.__object = None
        self.__info = None


__all__.append('PadProbeInfoObjectContextManager')


class PadProbeInfo(Gst.PadProbeInfo):  # type: ignore[misc]
    def writable_object(self) -> PadProbeInfoObjectContextManager:  # type: ignore[override]
        '''Return writable object contained in this PadProbeInfo.
        It uses a context manager to steal the object from the PadProbeInfo,
        and set it back when exiting the context.
        '''
        return PadProbeInfoObjectContextManager(_gi_gst.pad_probe_info_writable_object(self), self)

    def set_object(self, obj: typing.Optional[MiniObject]) -> None:
        _gi_gst.pad_probe_info_set_object(self, obj)


setattr(sys.modules["gi.repository.Gst"], 'PadProbeInfo', PadProbeInfo)
__all__.append('PadProbeInfo')


class PadFunc:
    def __init__(self, func: typing.Callable[..., FlowReturn]):
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
                raise TypeError(f"Invalid method {func}, 2 or 3 arguments required")

        return res


class Pad(Gst.Pad):
    def __init__(self, *args, **kwargs):
        super(Gst.Pad, self).__init__(*args, **kwargs)

    def set_chain_function(self, func: typing.Callable[..., FlowReturn]) -> None:
        self.set_chain_function_full(PadFunc(func), None)

    def set_event_function(self, func: typing.Callable[..., FlowReturn]) -> None:
        self.set_event_function_full(PadFunc(func), None)

    def set_query_function(self, func: typing.Callable[..., FlowReturn]) -> None:
        self.set_query_function_full(PadFunc(func), None)

    def query_caps(self, filter=None):
        return Gst.Pad.query_caps(self, filter)

    def set_caps(self, caps: Caps) -> bool:  # type: ignore[override]
        if not isinstance(caps, Gst.Caps):
            raise TypeError(f"{type(caps)} is not a Gst.Caps.")

        if not caps.is_fixed():
            return False

        event = Gst.Event.new_caps(caps)

        if self.direction == Gst.PadDirection.SRC:
            res = self.push_event(event)
        else:
            res = self.send_event(event)

        return res

    def link(self, pad: Pad) -> PadLinkReturn:
        ret = Gst.Pad.link(self, pad)
        if ret != Gst.PadLinkReturn.OK:
            raise LinkError(ret)
        return ret


override(Pad)
__all__.append('Pad')


class GhostPad(Gst.GhostPad):
    def __init__(self, name: str, target: typing.Optional[Pad] = None, direction: typing.Optional[PadDirection] = None):
        if direction is None:
            if target is None:
                raise TypeError('you must pass at least one of target '
                                'and direction')
            direction = target.props.direction

        Gst.GhostPad.__init__(self, name=name, direction=direction)
        self.construct()
        if target is not None:
            self.set_target(target)

    def query_caps(self, filter: typing.Optional[Caps] = None) -> Caps:
        return Gst.GhostPad.query_caps(self, filter)


override(GhostPad)
__all__.append('GhostPad')


class IteratorError(Exception):
    pass


__all__.append('IteratorError')


class MissingPluginError(Exception):
    pass


__all__.append('MissingPluginError')


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
    def __iter__(self) -> typing.Iterator[typing.Any]:
        while True:
            result, value = self.next()
            if result == Gst.IteratorResult.DONE:
                break

            if result != Gst.IteratorResult.OK:
                raise IteratorError(result)

            yield value


override(Iterator)
__all__.append('Iterator')


class ElementFactory(Gst.ElementFactory):

    # ElementFactory
    def get_longname(self) -> typing.Optional[str]:
        return self.get_metadata("long-name")

    def get_description(self) -> typing.Optional[str]:
        return self.get_metadata("description")

    def get_klass(self) -> typing.Optional[str]:
        return self.get_metadata("klass")

    @staticmethod
    def make(factoryname: str, name: typing.Optional[str] = None) -> Element:  # type: ignore[override]
        '''
        :raises Gst.PluginMissingError:
        '''
        elem = Gst.ElementFactory.make(factoryname, name)
        if not elem:
            raise MissingPluginError(f'No such element: {factoryname}')
        return elem  # type: ignore[return-value]


class Pipeline(Gst.Pipeline):
    def __init__(self, name: typing.Optional[str] = None):
        Gst.Pipeline.__init__(self, name=name)


override(Pipeline)
__all__.append('Pipeline')


class StructureContextManager:
    """A Gst.Structure wrapper to force usage of a context manager.
    """
    def __init__(self, structure: Structure, parent: MiniObject):
        self.__structure = structure
        self.__parent = parent

    def __enter__(self) -> Structure:
        return self.__structure

    def __exit__(self, _type, _value, _tb):
        self.__structure = None
        self.__parent = None


__all__.append('StructureContextManager')


class Structure(Gst.Structure):
    def __new__(cls, *args, **kwargs):
        if not args:
            if kwargs:
                raise TypeError("wrong arguments when creating GstStructure, first argument"
                                " must be the structure name.")
            struct = Structure.new_empty()
            return struct
        elif len(args) > 1:
            raise TypeError("wrong arguments when creating GstStructure object")
        elif isinstance(args[0], str):
            if not kwargs:
                struct = Structure.from_string(args[0])[0]
                return struct
            struct = Structure.new_empty(args[0])
            for k, v in kwargs.items():
                struct[k] = v

            return struct
        elif isinstance(args[0], Structure):
            struct = args[0].copy()
            return struct

        raise TypeError("wrong arguments when creating GstStructure object")

    def __init__(self, *args, **kwargs):
        pass

    def __ptr__(self):
        return _gi_gst._get_object_ptr(self)

    def __getitem__(self, key: str) -> typing.Any:
        val = self.get_value(key)
        if val is None:
            raise KeyError(f"key {key} not found")
        return val

    def __setitem__(self, key: str, value: typing.Any) -> None:
        self.set_value(key, value)

    def __len__(self) -> int:
        return self.n_fields()

    def __iter__(self) -> typing.Iterator[str]:
        return self.keys()

    def items(self) -> typing.Iterator[typing.Tuple[str, typing.Any]]:
        pairs: typing.List[typing.Tuple[str, typing.Any]] = []

        def foreach(fid, value):
            pairs.append((GLib.quark_to_string(fid), value))
            return True

        self.foreach(foreach)
        return iter(pairs)

    def keys(self) -> typing.Iterator[str]:
        keys: list[str] = []

        def foreach(fid, value):
            keys.append(GLib.quark_to_string(fid))
            return True

        self.foreach(foreach)
        return iter(keys)

    def set_value(self, key: str, value: typing.Any) -> bool:
        if not _gi_gst.structure_is_writable(self):
            raise NotWritableStructure("Trying to write to a not writable structure."
                                       " Make sure to use the right APIs to have access to structure"
                                       " in a writable way.")

        return Gst.Structure.set_value(self, key, value)

    def __str__(self) -> str:
        return self.to_string()

    def _set_parent(self, parent):
        self.__parent__ = parent
        return self

    def __enter__(self) -> Self:
        return self

    def __exit__(self, _type, _value, _tb):
        self._set_parent(None)


override(Structure)
__all__.append('Structure')

override(ElementFactory)
__all__.append('ElementFactory')


class Fraction(Gst.Fraction):
    num: int
    denom: int

    def __init__(self, num: int, denom: int = 1):
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
                num //= gcd
                denom //= gcd

            self.num = num
            self.denom = denom

        self.num = num
        self.denom = denom

        __simplify()
        self.type = "fraction"

    def __repr__(self) -> str:
        return f'<Gst.Fraction {self}>'

    def __value__(self):
        return self.num / self.denom

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Fraction):
            return self.num * other.denom == other.num * self.denom
        return False

    def __ne__(self, other: object) -> bool:
        return not self.__eq__(other)

    def __mul__(self, other: object) -> Fraction:
        if isinstance(other, Fraction):
            return Fraction(self.num * other.num,
                            self.denom * other.denom)
        elif isinstance(other, int):
            return Fraction(self.num * other, self.denom)
        raise TypeError(f"{type(other)} is not supported, use Gst.Fraction or int.")

    __rmul__ = __mul__

    def __truediv__(self, other: object) -> Fraction:
        if isinstance(other, Fraction):
            return Fraction(self.num * other.denom,
                            self.denom * other.num)
        elif isinstance(other, int):
            return Fraction(self.num, self.denom * other)
        raise TypeError(f"{type(other)} is not supported, use Gst.Fraction or int.")

    __div__ = __truediv__

    def __rtruediv__(self, other: object) -> Fraction:
        if isinstance(other, int):
            return Fraction(self.denom * other, self.num)
        raise TypeError(f"{type(other)} is not an int.")

    __rdiv__ = __rtruediv__

    def __float__(self) -> float:
        return float(self.num) / float(self.denom)

    def __str__(self) -> str:
        return f'{self.num}/{self.denom}'


override(Fraction)
__all__.append('Fraction')


class IntRange(Gst.IntRange):
    def __init__(self, r: range):
        if not isinstance(r, range):
            raise TypeError(f"{type(r)} is not a range.")

        if (r.start >= r.stop):
            raise TypeError("Range start must be smaller then stop")

        if r.start % r.step != 0:
            raise TypeError("Range start must be a multiple of the step")

        if r.stop % r.step != 0:
            raise TypeError("Range stop must be a multiple of the step")

        self.range = r

    def __repr__(self) -> str:
        return f'<Gst.IntRange [{self.range.start},{self.range.stop},{self.range.step}]>'

    def __str__(self) -> str:
        if self.range.step == 1:
            return f'[{self.range.start},{self.range.stop}]'
        else:
            return f'[{self.range.start},{self.range.stop},{self.range.step}]'

    def __eq__(self, other: object) -> bool:
        if isinstance(other, range):
            return self.range == other
        elif isinstance(other, IntRange):
            return self.range == other.range
        return False


override(IntRange)
__all__.append('IntRange')


class Int64Range(Gst.Int64Range):
    def __init__(self, r: range):
        if not isinstance(r, range):
            raise TypeError(f"{type(r)} is not a range.")

        if (r.start >= r.stop):
            raise TypeError("Range start must be smaller then stop")

        if r.start % r.step != 0:
            raise TypeError("Range start must be a multiple of the step")

        if r.stop % r.step != 0:
            raise TypeError("Range stop must be a multiple of the step")

        self.range = r

    def __repr__(self) -> str:
        return f'<Gst.Int64Range [{self.range.start},{self.range.stop},{self.range.step}]>'

    def __str__(self) -> str:
        if self.range.step == 1:
            return f'(int64)[{self.range.start},{self.range.stop}]'
        else:
            return f'(int64)[{self.range.start},{self.range.stop},{self.range.step}]'

    def __eq__(self, other: object) -> bool:
        if isinstance(other, range):
            return self.range == other
        elif isinstance(other, IntRange):
            return self.range == other.range
        return False


class Bitmask(Gst.Bitmask):
    def __init__(self, v: int) -> None:
        if not isinstance(v, int):
            raise TypeError(f"{type(v)} is not an int.")

        self.v = int(v)

    def __str__(self) -> str:
        return hex(self.v)

    def __eq__(self, other: object):
        return self.v == other


override(Bitmask)
__all__.append('Bitmask')


override(Int64Range)
__all__.append('Int64Range')


class DoubleRange(Gst.DoubleRange):
    def __init__(self, start: int | float, stop: int | float):
        self.start = float(start)
        self.stop = float(stop)

        if (start >= stop):
            raise TypeError("Range start must be smaller then stop")

    def __repr__(self) -> str:
        return f'<Gst.DoubleRange [{self.start},{self.stop}]>'

    def __str__(self) -> str:
        return f'(double)[{self.start},{self.stop}]'


override(DoubleRange)
__all__.append('DoubleRange')


class FractionRange(Gst.FractionRange):
    def __init__(self, start: Fraction, stop: Fraction):
        if not isinstance(start, Fraction):
            raise TypeError(f"{type(start)} is not a Gst.Fraction.")

        if not isinstance(stop, Fraction):
            raise TypeError(f"{type(stop)} is not a Gst.Fraction.")

        if (float(start) >= float(stop)):
            raise TypeError("Range start must be smaller then stop")

        self.start = start
        self.stop = stop

    def __repr__(self) -> str:
        return f'<Gst.FractionRange [{self.start},{self.stop}]>'

    def __str__(self) -> str:
        return f'(fraction)[{self.start},{self.stop}]'


override(FractionRange)
__all__.append('FractionRange')


class ValueArray(Gst.ValueArray):
    def __init__(self, array: typing.Optional[typing.List[typing.Any]] = None):
        self.array = list(array or [])

    def append(self, item: typing.Any) -> None:
        self.array.append(item)

    def prepend(self, item: typing.Any) -> None:
        self.array = [item] + self.array

    @staticmethod
    def append_value(this: ValueArray, item: typing.Any) -> None:
        this.append(item)

    @staticmethod
    def prepend_value(this: ValueArray, item: typing.Any) -> None:
        this.prepend(item)

    @staticmethod
    def get_size(this: ValueArray) -> int:
        return len(this.array)

    def __iter__(self) -> typing.Iterator[typing.Any]:
        return iter(self.array)

    def __getitem__(self, index: int) -> typing.Any:
        return self.array[index]

    def __setitem__(self, index: int, value: typing.Any) -> None:
        self.array[index] = value

    def __len__(self) -> int:
        return len(self.array)

    def __str__(self) -> str:
        return '<' + ','.join(map(str, self.array)) + '>'

    def __repr__(self) -> str:
        return f'<Gst.ValueArray {self}>'


override(ValueArray)
__all__.append('ValueArray')


class ValueList(Gst.ValueList):
    def __init__(self, array: typing.Optional[typing.List[typing.Any]] = None):
        self.array = list(array or [])

    def append(self, item: typing.Any) -> None:
        self.array.append(item)

    def prepend(self, item: typing.Any) -> None:
        self.array = [item] + self.array

    @staticmethod
    def append_value(this: ValueList, item: typing.Any) -> None:
        this.append(item)

    @staticmethod
    def prepend_value(this: ValueList, item: typing.Any) -> None:
        this.prepend(item)

    @staticmethod
    def get_size(this: ValueList) -> int:
        return len(this.array)

    def __iter__(self) -> typing.Iterator[typing.Any]:
        return iter(self.array)

    def __getitem__(self, index: int) -> typing.Any:
        return self.array[index]

    def __setitem__(self, index: int, value: typing.Any) -> None:
        self.array[index] = value

    def __len__(self) -> int:
        return len(self.array)

    def __str__(self) -> str:
        return '{' + ','.join(map(str, self.array)) + '}'

    def __repr__(self) -> str:
        return f'<Gst.ValueList {self}>'


override(ValueList)
__all__.append('ValueList')


class TagList(Gst.TagList):
    def __init__(self):
        Gst.TagList.__init__(self)

    def __getitem__(self, index: int) -> typing.Any:
        if index >= self.n_tags():
            raise IndexError('taglist index out of range')

        key = self.nth_tag_name(index)
        (res, val) = Gst.TagList.copy_value(self, key)
        if not res:
            raise KeyError(f"tag {key} not found")
        return val

    def __setitem__(self, key: str, value: typing.Any) -> None:
        self.add_value(Gst.TagMergeMode.REPLACE, key, value)

    def keys(self) -> typing.Iterable[str]:
        keys = set()

        def foreach(list, fid: str, udata):
            keys.add(fid)
            return True

        self.foreach(foreach, None, None)
        return keys

    def enumerate(self) -> map[tuple[str, typing.Any]]:
        return map(lambda k: (k, Gst.TagList.copy_value(self, k)[1]), self.keys())

    def __len__(self) -> int:
        return self.n_tags()

    def __str__(self) -> str:
        return self.to_string()

    def __repr__(self) -> str:
        return f'<Gst.TagList {self}>'


override(TagList)
__all__.append('TagList')

# From https://docs.python.org/3/library/itertools.html


def pairwise(iterable: typing.Iterable[Element]) -> typing.Iterator[tuple[Element, Element]]:
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

    def __enter__(self) -> Self:
        if not self.__parent__:
            raise MapError('MappingError', 'Mapping was not successful')

        return self

    def __exit__(self, type, value, tb):
        if not self.__parent__.unmap(self):
            raise MapError('MappingError', 'Unmapping was not successful')

    def get_data(self):
        return self.data


__all__.append("MapInfo")


class Buffer(MiniObjectMixin, Gst.Buffer):
    @property  # type: ignore[override]
    def flags(self) -> BufferFlags:
        return _gi_gst.mini_object_flags(self)

    @flags.setter
    def flags(self, flags: BufferFlags) -> None:
        _gi_gst.mini_object_set_flags(self, flags)

    @property
    def dts(self) -> int:
        return _gi_gst.buffer_get_dts(self)

    @dts.setter
    def dts(self, dts: int) -> None:
        _gi_gst.buffer_set_dts(self, dts)

    @property
    def pts(self) -> int:
        return _gi_gst.buffer_get_pts(self)

    @pts.setter
    def pts(self, pts: int) -> None:
        _gi_gst.buffer_set_pts(self, pts)

    @property
    def duration(self) -> int:
        return _gi_gst.buffer_get_duration(self)

    @duration.setter
    def duration(self, duration: int) -> None:
        _gi_gst.buffer_set_duration(self, duration)

    @property
    def offset(self) -> int:
        return _gi_gst.buffer_get_offset(self)

    @offset.setter
    def offset(self, offset: int) -> None:
        _gi_gst.buffer_set_offset(self, offset)

    @property
    def offset_end(self) -> int:
        return _gi_gst.buffer_get_offset_end(self)

    @offset_end.setter
    def offset_end(self, offset_end: int) -> None:
        _gi_gst.buffer_set_offset_end(self, offset_end)

    def map_range(self, idx: int, length: int, flags: MapFlags) -> MapInfo:  # type: ignore[override]
        mapinfo = MapInfo()
        if (_gi_gst.buffer_override_map_range(self, mapinfo, idx, length, int(flags))):
            mapinfo.__parent__ = self

        return mapinfo

    def map(self, flags: MapFlags) -> MapInfo:  # type: ignore[override]
        mapinfo = MapInfo()
        if _gi_gst.buffer_override_map(self, mapinfo, int(flags)):
            mapinfo.__parent__ = self

        return mapinfo

    def unmap(self, mapinfo: MapInfo) -> bool:  # type: ignore[override]
        mapinfo.__parent__ = None
        return _gi_gst.buffer_override_unmap(self, mapinfo)


override(Buffer)
__all__.append('Buffer')


class Memory(Gst.Memory):

    def map(self, flags: MapFlags) -> MapInfo:  # type: ignore[override]
        mapinfo = MapInfo()
        if (_gi_gst.memory_override_map(self, mapinfo, int(flags))):
            mapinfo.__parent__ = self

        return mapinfo

    def unmap(self, mapinfo: MapInfo) -> bool:  # type: ignore[override]
        mapinfo.__parent__ = None
        return _gi_gst.memory_override_unmap(self, mapinfo)


override(Memory)
__all__.append('Memory')


def TIME_ARGS(time: int) -> str:
    if time == Gst.CLOCK_TIME_NONE:
        return "CLOCK_TIME_NONE"

    return "%u:%02u:%02u.%09u" % (time / (Gst.SECOND * 60 * 60),
                                  (time / (Gst.SECOND * 60)) % 60,
                                  (time / Gst.SECOND) % 60,
                                  time % Gst.SECOND)


__all__.append('TIME_ARGS')

from gi.overrides import _gi_gst  # type: ignore[attr-defined]
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


def find_gi_repository_parent(klass):
    """Find the gi.repository parent class in the MRO for introspection methods"""
    for parent in klass.__mro__:
        if parent.__module__.startswith('gi.repository.'):
            return parent
    return None


def _collect_methods_from_dict(source_dict, klass, seen):
    """Helper to collect methods from a class dictionary, avoiding dunder methods."""
    methods = []
    for attr_name in source_dict:
        if attr_name in seen:
            continue
        attr = source_dict[attr_name]
        if isinstance(attr, (type(Gst.init), staticmethod, classmethod)):
            # Skip dunder methods as they're Python special methods used for
            # object instantiation and other internals, replacing them breaks
            # class behavior
            if not attr_name.startswith('__'):
                methods.append((attr_name, getattr(klass, attr_name)))
            seen.add(attr_name)
    return methods


real_functions = [o for o in inspect.getmembers(Gst) if isinstance(o[1], type(Gst.init))]

class_methods = []
for cname_klass in [o for o in inspect.getmembers(Gst) if isinstance(o[1], type(Gst.Element)) or isinstance(o[1], type(Gst.Caps))]:
    klass = cname_klass[1]
    methods = []
    seen: set[str] = set()

    # Collect methods from the override class itself
    methods.extend(_collect_methods_from_dict(klass.__dict__, klass, seen))

    # Collect methods from the gi.repository introspection parent class
    gi_parent = find_gi_repository_parent(klass)
    if gi_parent:
        methods.extend(_collect_methods_from_dict(gi_parent.__dict__, klass, seen))

    class_methods.append((cname_klass, methods))

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


def init(argv: typing.Optional[list[str]] = None) -> None:
    init_pygst()

    if Gst.is_initialized():
        return

    # FIXME: Workaround for pygobject handling nullability wrong
    if argv is None:
        argv = []

    real_init(argv)


Gst.init = init

real_init_check = Gst.init_check


def init_check(argv: typing.Optional[list[str]] = None) -> typing.Tuple[bool, typing.Optional[list[str]]]:
    init_pygst()
    if Gst.is_initialized():
        return True, argv

    return real_init_check(argv)


Gst.init_check = init_check

real_deinit = Gst.deinit


def deinit() -> None:
    deinit_pygst()
    real_deinit()


def init_python():
    if not Gst.is_initialized():
        raise NotInitialized("Gst.init_python should never be called before GStreamer itself is initialized")

    init_pygst()


Gst.deinit = deinit
Gst.init_python = init_python

if not Gst.is_initialized():
    deinit_pygst()
