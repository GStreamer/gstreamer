/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
 */

#pragma once

#include "gstcodecccinserter.h"

G_BEGIN_DECLS

#define GST_TYPE_H265_CC_INSERTER (gst_h265_cc_inserter_get_type())
G_DECLARE_FINAL_TYPE (GstH265CCInserter, gst_h265_cc_inserter,
    GST, H265_CC_INSERTER, GstCodecCCInserter);

GST_ELEMENT_REGISTER_DECLARE (h265ccinserter);

G_END_DECLS
