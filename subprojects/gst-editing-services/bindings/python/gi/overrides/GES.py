# -*- Mode: Python; py-indent-offset: 4 -*-
# vim: tabstop=4 shiftwidth=4 expandtab
#
#       GES.py
#
# Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import sys
from ..overrides import override
from ..importer import modules
from gi.repository import GObject


if sys.version_info >= (3, 0):
    _basestring = str
    _callable = lambda c: hasattr(c, '__call__')
else:
    _basestring = basestring
    _callable = callable

GES = modules['GES']._introspection_module
__all__ = []

if GES._version == '0.10':
    import warnings
    warn_msg = "You have imported the GES 0.10 module.  Because GES 0.10 \
was not designed for use with introspection some of the \
interfaces and API will fail.  As such this is not supported \
by the GStreamer development team and we encourage you to \
port your app to GES 1 or greater. static python bindings is the recomended \
python module to use with GES 0.10"

    warnings.warn(warn_msg, RuntimeWarning)

def __timeline_element__repr__(self):
    return "%s [%s (%s) %s]" % (
        self.props.name,
        Gst.TIME_ARGS(self.props.start),
        Gst.TIME_ARGS(self.props.in_point),
        Gst.TIME_ARGS(self.props.duration),
    )

__prev_set_child_property = GES.TimelineElement.set_child_property
def __timeline_element_set_child_property(self, prop_name, prop_value):
    res, _, pspec = GES.TimelineElement.lookup_child(self, prop_name)
    if not res:
        return res

    v = GObject.Value()
    v.init(pspec.value_type)
    v.set_value(prop_value)

    return __prev_set_child_property(self, prop_name, v)


GES.TimelineElement.__repr__ = __timeline_element__repr__
GES.TimelineElement.set_child_property = __timeline_element_set_child_property
GES.TrackElement.set_child_property = GES.TimelineElement.set_child_property
GES.Container.edit = GES.TimelineElement.edit

__prev_asset_repr = GES.Asset.__repr__
def __asset__repr__(self):
    return "%s(%s)" % (__prev_asset_repr(self), self.props.id)

GES.Asset.__repr__ = __asset__repr__

def __timeline_iter_clips(self):
    """Iterate all clips in a timeline"""
    for layer in self.get_layers():
        for clip in layer.get_clips():
            yield clip

GES.Timeline.iter_clips = __timeline_iter_clips

try:
    from gi.repository import Gst
    Gst
except:
    raise RuntimeError("GSt couldn't be imported, make sure you have gst-python installed")
