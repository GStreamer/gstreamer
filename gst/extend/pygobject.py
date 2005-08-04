#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2004 Johan Dahlin <johan at gnome dot org>

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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

"""
PyGTK helper functions
"""

import sys

import gobject

def gobject_set_property(object, property, value):
    """
    Set the given property to the given value on the given object.

    @type object:   L{gobject.GObject}
    @type property: string
    @param value:   value to set property to
    """
    for pspec in gobject.list_properties(object):
        if pspec.name == property:
            break
    else:
        raise errors.PropertyError(
            "Property '%s' in element '%s' does not exist" % (
                property, object.get_property('name')))
        
    if pspec.value_type in (gobject.TYPE_INT, gobject.TYPE_UINT,
                            gobject.TYPE_INT64, gobject.TYPE_UINT64):
        try:
            value = int(value)
        except ValueError:
            msg = "Invalid value given for property '%s' in element '%s'" % (
                property, object.get_property('name'))
            raise errors.PropertyError(msg)
        
    elif pspec.value_type == gobject.TYPE_BOOLEAN:
        if value == 'False':
            value = False
        elif value == 'True':
            value = True
        else:
            value = bool(value)
    elif pspec.value_type in (gobject.TYPE_DOUBLE, gobject.TYPE_FLOAT):
        value = float(value)
    elif pspec.value_type == gobject.TYPE_STRING:
        value = str(value)
    # FIXME: this is superevil ! we really need to find a better way
    # of checking if this property is a param enum  
    # also, we only allow int for now
    elif repr(pspec.__gtype__).startswith("<GType GParamEnum"):
        value = int(value)
    else:
        raise errors.PropertyError('Unknown property type: %s' %
            pspec.value_type)

    object.set_property(property, value)

def gsignal(name, *args):
    """
    Add a GObject signal to the current object.
    To be used from class definition scope.

    @type name: string
    @type args: mixed
    """
    frame = sys._getframe(1)
    _locals = frame.f_locals
    
    if not '__gsignals__' in _locals:
        _dict = _locals['__gsignals__'] = {}
    else:
        _dict = _locals['__gsignals__']

    _dict[name] = (gobject.SIGNAL_RUN_FIRST, None, args)

PARAM_CONSTRUCT = 1<<9

def with_construct_properties(__init__):
    """
    Wrap a class' __init__ method in a procedure that will construct
    gobject properties. This is necessary because pygtk's object
    construction is a bit broken.

    Usage::

        class Foo(GObject):
            def __init__(self):
                GObject.__init(self)
            __init__ = with_construct_properties(__init__)
    """
    frame = sys._getframe(1)
    _locals = frame.f_locals
    gproperties = _locals['__gproperties__']
    def hacked_init(self, *args, **kwargs):
        __init__(self, *args, **kwargs)
        self.__gproperty_values = {}
        for p, v in gproperties.items():
            if v[-1] & PARAM_CONSTRUCT:
                self.set_property(p, v[3])
    return hacked_init

def gproperty(type_, name, desc, *args, **kwargs):
    """
    Add a GObject property to the current object.
    To be used from class definition scope.

    @type type_: type object
    @type name: string
    @type desc: string
    @type args: mixed
    """
    frame = sys._getframe(1)
    _locals = frame.f_locals
    flags = 0
    
    def _do_get_property(self, prop):
        try:
            return self._gproperty_values[prop.name]
        except (AttributeError, KeyError):
            raise AttributeError('Property was never set', self, prop)

    def _do_set_property(self, prop, value):
        if not getattr(self, '_gproperty_values', None):
            self._gproperty_values = {}
        self._gproperty_values[prop.name] = value
    
    _locals['do_get_property'] = _do_get_property
    _locals['do_set_property'] = _do_set_property
    
    if not '__gproperties__' in _locals:
        _dict = _locals['__gproperties__'] = {}
    else:
        _dict = _locals['__gproperties__']
    
    for i in 'readable', 'writable':
        if not i in kwargs:
            kwargs[i] = True

    for k, v in kwargs.items():
        if k == 'construct':
            flags |= PARAM_CONSTRUCT
        elif k == 'construct_only':
            flags |= gobject.PARAM_CONSTRUCT_ONLY
        elif k == 'readable':
            flags |= gobject.PARAM_READABLE
        elif k == 'writable':
            flags |= gobject.PARAM_WRITABLE
        elif k == 'lax_validation':
            flags |= gobject.PARAM_LAX_VALIDATION
        else:
            raise Exception('Invalid GObject property flag: %r=%r' % (k, v))

    _dict[name] = (type_, name, desc) + args + tuple((flags,))
