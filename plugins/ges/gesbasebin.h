/* GStreamer GES plugin
 *
 * Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
 *
 * gesbasebin.h
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

#include <gst/gst.h>
#include <gst/base/gstflowcombiner.h>
#include <ges/ges.h>

G_BEGIN_DECLS

#define SUPRESS_UNUSED_WARNING(a) (void)a

G_DECLARE_DERIVABLE_TYPE(GESBaseBin, ges_base_bin, GES, BASE_BIN, GstBin)
struct _GESBaseBinClass
{
  GstBinClass parent_class;
};

gboolean ges_base_bin_set_timeline (GESBaseBin * self, GESTimeline * timeline);
GESTimeline * ges_base_bin_get_timeline (GESBaseBin * self);

G_END_DECLS
