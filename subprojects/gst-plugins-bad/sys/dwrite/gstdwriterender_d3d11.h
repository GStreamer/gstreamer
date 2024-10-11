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

#include "gstdwriterender.h"

G_BEGIN_DECLS

#define GST_TYPE_DWRITE_D3D11_RENDER (gst_dwrite_d3d11_render_get_type ())
G_DECLARE_FINAL_TYPE (GstDWriteD3D11Render,
    gst_dwrite_d3d11_render, GST, DWRITE_D3D11_RENDER, GstDWriteRender);

GstDWriteRender * gst_dwrite_d3d11_render_new (GstD3D11Device * device,
                                               const GstVideoInfo * info,
                                               ID2D1Factory * d2d_factory,
                                               IDWriteFactory * dwrite_factory);

G_END_DECLS
