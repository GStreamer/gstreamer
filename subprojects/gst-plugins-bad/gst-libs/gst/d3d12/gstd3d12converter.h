/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_CONVERTER            (gst_d3d12_converter_get_type ())
#define GST_D3D12_CONVERTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_CONVERTER, GstD3D12Converter))
#define GST_D3D12_CONVERTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_CONVERTER, GstD3D12ConverterClass))
#define GST_IS_D3D12_CONVERTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_CONVERTER))
#define GST_IS_D3D12_CONVERTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_CONVERTER))
#define GST_D3D12_CONVERTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_CONVERTER, GstD3D12ConverterClass))
#define GST_D3D12_CONVERTER_CAST(obj)       ((GstD3D12Converter*)(obj))

/**
 * GST_D3D12_CONVERTER_OPT_GAMMA_MODE:
 *
 * #GstVideoGammaMode, set the gamma mode.
 * Default is #GST_VIDEO_GAMMA_MODE_NONE
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_GAMMA_MODE "GstD3D12Converter.gamma-mode"

/**
 * GST_D3D12_CONVERTER_OPT_PRIMARIES_MODE:
 *
 * #GstVideoPrimariesMode, set the primaries conversion mode.
 * Default is #GST_VIDEO_PRIMARIES_MODE_NONE.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_PRIMARIES_MODE "GstD3D12Converter.primaries-mode"

/**
 * GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER:
 *
 * #D3D12_FILTER, set sampler filter.
 *
 * Supported values are:
 * @D3D12_FILTER_MIN_MAG_MIP_POINT
 * @D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT
 * @D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT
 * @D3D12_FILTER_MIN_MAG_MIP_LINEAR
 * @D3D12_FILTER_ANISOTROPIC
 *
 * Default is #D3D12_FILTER_MIN_MAG_MIP_LINEAR.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_SAMPLER_FILTER "GstD3D12Converter.sampler-filter"

GST_D3D12_API
GType gst_d3d12_converter_sampler_filter_get_type (void);
#define GST_TYPE_D3D12_CONVERTER_SAMPLER_FILTER (gst_d3d12_converter_sampler_filter_get_type())

/**
 * GstD3D12ConverterAlphaMode:
 * @GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED: Unspecified alpha mode
 * @GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED: Premultiplied alpha
 * @GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT: Straight alpha
 *
 * Alpha mode. Enum values are idnetical to DXGI_ALPHA_MODE
 *
 * Since: 1.26
 */
typedef enum
{
  GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED = 0,
  GST_D3D12_CONVERTER_ALPHA_MODE_PREMULTIPLIED = 1,
  GST_D3D12_CONVERTER_ALPHA_MODE_STRAIGHT = 2,
} GstD3D12ConverterAlphaMode;

GST_D3D12_API
GType gst_d3d12_converter_alpha_mode_get_type (void);
#define GST_TYPE_D3D12_CONVERTER_ALPHA_MODE (gst_d3d12_converter_alpha_mode_get_type())

/**
 * GST_D3D12_CONVERTER_OPT_SRC_ALPHA_MODE:
 *
 * Set the source alpha mode.
 * Default is #GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_SRC_ALPHA_MODE "GstD3D12Converter.src-alpha-mode"

/**
 * GST_D3D12_CONVERTER_OPT_DEST_ALPHA_MODE:
 *
 * Set the source alpha mode.
 * Default is #GST_D3D12_CONVERTER_ALPHA_MODE_UNSPECIFIED.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_DEST_ALPHA_MODE "GstD3D12Converter.dest-alpha-mode"

/**
 * GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_COUNT:
 *
 * #G_TYPE_UINT, D3D12_GRAPHICS_PIPELINE_STATE_DESC.SampleDesc.Count value to use.
 * Default is 1.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_COUNT "GstD3D12Converter.pso-sample-desc-count"

/**
 * GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_QUALITY:
 *
 * #G_TYPE_UINT, D3D12_GRAPHICS_PIPELINE_STATE_DESC.SampleDesc.Quality value to use.
 * Default is 0.
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_PSO_SAMPLE_DESC_QUALITY "GstD3D12Converter.pso-sample-desc-quality"

/**
 * GstD3D12ConverterColorBalance:
 * @GST_D3D12_CONVERTER_COLOR_BALANCE_DISABLED: Disable color-balance feature
 * @GST_D3D12_CONVERTER_COLOR_BALANCE_ENABLED: Enable color-balance feature
 *
 * Since: 1.26
 */
typedef enum
{
  GST_D3D12_CONVERTER_COLOR_BALANCE_DISABLED,
  GST_D3D12_CONVERTER_COLOR_BALANCE_ENABLED,
} GstD3D12ConverterColorBalance;

GST_D3D12_API
GType gst_d3d12_converter_color_balance_get_type (void);
#define GST_TYPE_D3D12_CONVERTER_COLOR_BALANCE (gst_d3d12_converter_color_balance_get_type())

/**
 * GST_D3D12_CONVERTER_OPT_COLOR_BALANCE:
 *
 * #GstD3D12ConverterColorBalance, an option to enable color-balance feature
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_COLOR_BALANCE "GstD3D12Converter.color-balance"

/**
 * GstD3D12ConverterMipGen:
 * @GST_D3D12_CONVERTER_MIP_GEN_DISABLED: Disable mipmap generating feature
 * @GST_D3D12_CONVERTER_MIP_GEN_ENABLED: Enable mipmap generating feature
 *
 * Since: 1.26
 */
typedef enum
{
  GST_D3D12_CONVERTER_MIP_GEN_DISABLED,
  GST_D3D12_CONVERTER_MIP_GEN_ENABLED,
} GstD3D12ConverterMipGen;

GST_D3D12_API
GType gst_d3d12_converter_mip_gen_get_type (void);
#define GST_TYPE_D3D12_CONVERTER_MIP_GEN (gst_d3d12_converter_mip_gen_get_type())

/**
 * GST_D3D12_CONVERTER_OPT_MIP_GEN:
 *
 * #GstD3D12ConverterMipGen, an option to enable mipmap genarating feature
 *
 * Since: 1.26
 */
#define GST_D3D12_CONVERTER_OPT_MIP_GEN "GstD3D12Converter.mip-gen"

/**
 * GstD3D12Converter:
 *
 * Opaque GstD3D12Converter struct
 *
 * Since: 1.26
 */
struct _GstD3D12Converter
{
  GstObject parent;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12ConverterPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12ConverterClass:
 *
 * Opaque GstD3D12ConverterClass struct
 *
 * Since: 1.26
 */
struct _GstD3D12ConverterClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType               gst_d3d12_converter_get_type (void);

GST_D3D12_API
GstD3D12Converter * gst_d3d12_converter_new  (GstD3D12Device * device,
                                              GstD3D12CmdQueue * queue,
                                              const GstVideoInfo * in_info,
                                              const GstVideoInfo * out_info,
                                              const D3D12_BLEND_DESC * blend_desc,
                                              const gfloat blend_factor[4],
                                              GstStructure * config);

GST_D3D12_API
gboolean            gst_d3d12_converter_convert_buffer (GstD3D12Converter * converter,
                                                        GstBuffer * in_buf,
                                                        GstBuffer * out_buf,
                                                        GstD3D12FenceData * fence_data,
                                                        ID3D12GraphicsCommandList * command_list,
                                                        gboolean execute_gpu_wait);

GST_D3D12_API
gboolean            gst_d3d12_converter_update_blend_state (GstD3D12Converter * converter,
                                                            const D3D12_BLEND_DESC * blend_desc,
                                                            const gfloat blend_factor[4]);

G_END_DECLS
