/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include <gst/d3d12/gstd3d12.h>
#include <onnxruntime_c_api.h>

G_BEGIN_DECLS

typedef struct _GstOnnxDmlCtx GstOnnxDmlCtx;

GstOnnxDmlCtx * gst_onnx_dml_create_context (GstD3D12Device * device12,
                                             OrtSessionOptions * opt);

void gst_onnx_dml_free_context (GstOnnxDmlCtx * ctx);

G_END_DECLS
