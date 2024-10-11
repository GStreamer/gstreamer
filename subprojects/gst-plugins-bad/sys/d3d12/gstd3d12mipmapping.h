/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include "gstd3d12basefilter.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D12_MIP_MAPPING (gst_d3d12_mip_mapping_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12MipMapping, gst_d3d12_mip_mapping,
    GST, D3D12_MIP_MAPPING, GstD3D12BaseFilter)

G_END_DECLS

