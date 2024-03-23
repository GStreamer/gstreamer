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
#include <gst/video/video.h>
#include <dxgi.h>

G_BEGIN_DECLS

typedef enum
{
  GST_D3D_PLUGIN_PS_CHECKER_LUMA,
  GST_D3D_PLUGIN_PS_CHECKER_RGB,
  GST_D3D_PLUGIN_PS_CHECKER_VUYA,
  GST_D3D_PLUGIN_PS_CHECKER,
  GST_D3D_PLUGIN_PS_COLOR,
  GST_D3D_PLUGIN_PS_SAMPLE_PREMULT,
  GST_D3D_PLUGIN_PS_SAMPLE,
  GST_D3D_PLUGIN_PS_SNOW,

  GST_D3D_PLUGIN_PS_LAST
} GstD3DPluginPS;

typedef enum
{
  GST_D3D_PLUGIN_VS_COLOR,
  GST_D3D_PLUGIN_VS_COORD,
  GST_D3D_PLUGIN_VS_POS,

  GST_D3D_PLUGIN_VS_LAST,
} GstD3DPluginVS;


typedef enum
{
  GST_D3D_SM_4_0,
  GST_D3D_SM_5_0,
  GST_D3D_SM_5_1,

  GST_D3D_SM_LAST
} GstD3DShaderModel;

typedef struct _GstD3DShaderByteCode
{
  gconstpointer byte_code;
  gsize byte_code_len;
} GstD3DShaderByteCode;

typedef enum
{
  GST_D3D_CONVERTER_IDENTITY,
  GST_D3D_CONVERTER_SIMPLE,
  GST_D3D_CONVERTER_RANGE,
  GST_D3D_CONVERTER_GAMMA,
  GST_D3D_CONVERTER_PRIMARY,
} GstD3DConverterType;

typedef struct _GstD3DConverterCSByteCode
{
  GstD3DShaderByteCode byte_code;
  guint x_unit;
  guint y_unit;
  DXGI_FORMAT srv_format;
  DXGI_FORMAT uav_format;
} GstD3DConverterCSByteCode;

typedef struct _GstD3DConverterPSByteCode
{
  GstD3DShaderByteCode byte_code;
  guint num_rtv;
} GstD3DConverterPSByteCode;

GST_D3D_SHADER_API
gboolean gst_d3d_plugin_shader_get_vs_blob (GstD3DPluginVS type,
                                            GstD3DShaderModel shader_model,
                                            GstD3DShaderByteCode * byte_code);

GST_D3D_SHADER_API
gboolean gst_d3d_plugin_shader_get_ps_blob (GstD3DPluginPS type,
                                            GstD3DShaderModel shader_model,
                                            GstD3DShaderByteCode * byte_code);

GST_D3D_SHADER_API
gboolean gst_d3d_converter_shader_get_vs_blob (GstD3DShaderModel shader_model,
                                               GstD3DShaderByteCode * byte_code);

GST_D3D_SHADER_API
gboolean gst_d3d_converter_shader_get_cs_blob (GstVideoFormat in_format,
                                               GstVideoFormat out_format,
                                               GstD3DShaderModel shader_model,
                                               GstD3DConverterCSByteCode * byte_code);

GST_D3D_SHADER_API
guint   gst_d3d_converter_shader_get_ps_blob (GstVideoFormat in_format,
                                               GstVideoFormat out_format,
                                               gboolean in_premul,
                                               gboolean out_premul,
                                               GstD3DConverterType conv_type,
                                               GstD3DShaderModel shader_model,
                                               GstD3DConverterPSByteCode byte_code[4]);

G_END_DECLS
