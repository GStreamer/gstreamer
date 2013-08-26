# -*- Mode: Python; py-indent-offset: 4 -*-
# vim: tabstop=4 shiftwidth=4 expandtab
#
#       GES.py
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
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
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


try:
    from gi.repository import Gst
    Gst
except:
    raise RuntimeError("GSt couldn't be imported, make sure you have gst-python installed")
