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

import sys
import typing

from gi.overrides import override
from gi.overrides import _gi_gst_analytics  # type: ignore[attr-defined]
_gi_gst_analytics

if typing.TYPE_CHECKING:
    # Import stubs for type checking this file.
    from gi.repository import GstAnalytics
else:
    from gi.module import get_introspection_module
    GstAnalytics = get_introspection_module('GstAnalytics')


__all__ = []
__mtd_types__ = {}


class Mtd(GstAnalytics.Mtd):
    def __eq__(self, other):
        if not hasattr(other, 'meta') or not hasattr(other, 'id'):
            return False
        return self.meta == other.meta and self.id == other.id

    def iter_direct_related(self, relation, mtd_type=GstAnalytics.Mtd):
        if mtd_type != GstAnalytics.Mtd:
            mtd_type = mtd_type.get_mtd_type()
        else:
            mtd_type = GstAnalytics.MTD_TYPE_ANY

        return _gi_gst_analytics.AnalyticsMtdDirectRelatedIterator(
            sys.modules[__name__], self, relation, mtd_type)

    def relation_path(self, mtd, max_span=0, reltype=GstAnalytics.RelTypes.ANY):
        return _gi_gst_analytics.AnalyticsMtdRelationPath(
            sys.modules[__name__], self, mtd.get_id(), max_span, reltype)


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
        raise Gst.AddError('Mtd with id={mtd_id} of rmeta={rmeta} is not known.')
    return res[1]


class RelationMeta(GstAnalytics.RelationMeta):
    def __iter__(self):
        return _gi_gst_analytics.AnalyticsRelationMetaIterator(sys.modules[__name__], self)

    def iter_on_type(self, filter):
        if filter == GstAnalytics.Mtd:
            return self.__iter__()

        mtdtype = filter.get_mtd_type()
        if mtdtype in __mtd_types__:
            return _gi_gst_analytics.AnalyticsRelationMetaIteratorWithMtdTypeFilter(
                sys.modules[__name__], self, mtdtype)
        else:
            raise TypeError('Wrong filter type is used for iter_on_type method.')


__all__.append('RelationMeta')
