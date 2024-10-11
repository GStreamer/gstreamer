/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d11/gstd3d11.h>
#include "gstqsvallocator.h"

G_BEGIN_DECLS

#define GST_TYPE_QSV_D3D11_ALLOCATOR (gst_qsv_d3d11_allocator_get_type())
G_DECLARE_FINAL_TYPE (GstQsvD3D11Allocator, gst_qsv_d3d11_allocator,
    GST, QSV_D3D11_ALLOCATOR, GstQsvAllocator);

GstQsvAllocator * gst_qsv_d3d11_allocator_new (GstD3D11Device * device);

void gst_qsv_d3d11_allocator_set_d3d12_import_allowed (GstQsvAllocator * allocator,
                                                       gboolean allowed);

G_END_DECLS
