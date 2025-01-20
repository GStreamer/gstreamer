# -*- Mode: Python; py-indent-offset: 4 -*-
# vim: tabstop=4 shiftwidth=4 expandtab
#
#       GstAnalytics.py
#
# Copyright (C) 2024 Daniel Morin <daniel.morin@dmohub.org>
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

from ..overrides import override
from ..module import get_introspection_module
import sys

GstAnalytics = get_introspection_module('GstAnalytics')
__all__ = []

from gi.overrides import _gi_gst_analytics
_gi_gst_analytics

__mtd_types__ = {}


class Mtd(GstAnalytics.Mtd):
    def __eq__(self, other):
        if not hasattr(other, 'meta') or not hasattr(other, 'id'):
            return False
        return self.meta == other.meta and self.id == other.id


__all__.append('Mtd')


def _wrap_mtd(module, name, getter):
    baseclass = getattr(module, name)
    wrapper = type(name, (baseclass, Mtd), {})
    globals()[name] = wrapper

    __mtd_types__[baseclass.get_mtd_type()] = getter
    __all__.append(name)


for c in dir(GstAnalytics):
    if c.endswith('Mtd') and c != 'Mtd':
        lower_c = c[:-3].lower()
        getter = getattr(GstAnalytics.RelationMeta, 'get_' + lower_c + '_mtd')
        _wrap_mtd(GstAnalytics, c, getter)


def _get_mtd(mtd_type, rmeta, mtd_id):
    res = __mtd_types__[mtd_type](rmeta, mtd_id)
    if not res[0]:
        raise AddError('Mtd with id={mtd_id} of rmeta={rmeta} is not known.')
    return res[1]


class RelationMeta(GstAnalytics.RelationMeta):
    def __iter__(self):
        return _gi_gst_analytics.AnalyticsRelationMetaIterator(sys.modules[__name__], self)


__all__.append('RelationMeta')
