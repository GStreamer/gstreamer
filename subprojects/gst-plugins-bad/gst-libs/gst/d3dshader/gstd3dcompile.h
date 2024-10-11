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
#include <gst/d3dshader/d3dshader-prelude.h>
#include <d3dcompiler.h>

G_BEGIN_DECLS

GST_D3D_SHADER_API
gboolean gst_d3d_compile_init (void);

GST_D3D_SHADER_API
HRESULT gst_d3d_compile (LPCVOID src_data,
                         SIZE_T src_data_size,
                         LPCSTR source_name,
                         CONST D3D_SHADER_MACRO * defines,
                         ID3DInclude * include,
                         LPCSTR entry_point,
                         LPCSTR target,
                         UINT flags1,
                         UINT flags2,
                         ID3DBlob ** code,
                         ID3DBlob ** error_msgs);

G_END_DECLS
