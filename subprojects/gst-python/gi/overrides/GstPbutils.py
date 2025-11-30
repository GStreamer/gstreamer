# -*- Mode: Python; py-indent-offset: 4 -*-
# vim: tabstop=4 shiftwidth=4 expandtab
#
#       Gst.py
#
# Copyright (C) 2012 Alessandro Decina <alessandro.d@gmail.com>
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

import typing
import gi

gi.require_version('Gst', '1.0')
from gi.repository import Gst
from gi.overrides import override as override_

if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    from gi.repository import GstPbutils
else:
    from gi.module import get_introspection_module
    GstPbutils = get_introspection_module('GstPbutils')

__all__ = []


def override(cls):
    name = cls.__name__
    globals()[name] = override_(cls)
    __all__.append(name)

    return cls


real_init = GstPbutils.pb_utils_init


def init():
    if not Gst.is_initialized():
        raise RuntimeError("Gst.init() needs to be called before importing GstPbutils")

    real_init()

    @override
    class EncodingVideoProfile(GstPbutils.EncodingVideoProfile):
        def __init__(self, format, preset=None, restriction=None, presence=0):
            GstPbutils.EncodingVideoProfile.__init__(self)
            self.set_format(format)
            if preset is not None:
                self.set_preset(preset)
            if restriction is None:
                restriction = Gst.Caps('ANY')
            self.set_restriction(restriction)
            self.set_presence(presence)

    @override
    class EncodingAudioProfile(GstPbutils.EncodingAudioProfile):
        def __init__(self, format, preset=None, restriction=None, presence=0):
            GstPbutils.EncodingAudioProfile.__init__(self)
            self.set_format(format)
            if preset is not None:
                self.set_preset(preset)
            if restriction is None:
                restriction = Gst.Caps('ANY')
            self.set_restriction(restriction)
            self.set_presence(presence)

    @override
    class EncodingContainerProfile(GstPbutils.EncodingContainerProfile):
        def __init__(self, name, description, format, preset=None):
            GstPbutils.EncodingContainerProfile.__init__(self)
            self.set_format(format)
            if name is not None:
                self.set_name(name)
            if description is not None:
                self.set_description(description)
            if preset is not None:
                self.set_preset(preset)


GstPbutils.pb_utils_init = init
GstPbutils.init = init
if Gst.is_initialized():
    init()
