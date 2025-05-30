# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2025 Netflix Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

from gi.overrides import _gi_gst
from ..overrides import override
from ..module import get_introspection_module


GstApp = get_introspection_module('GstApp')
__all__ = []


class AppSink(GstApp.AppSink):
    def pull_object(self):
        obj = super().pull_object()
        return _gi_gst.mini_object_to_subclass(obj) if obj else None

    def try_pull_object(self, timeout):
        obj = super().try_pull_object(timeout)
        return _gi_gst.mini_object_to_subclass(obj) if obj else None


AppSink = override(AppSink)
__all__.append('AppSink')
