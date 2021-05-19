/*
 * GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifndef __GST_D3D11_COMPOSITOR_H__
#define __GST_D3D11_COMPOSITOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_COMPOSITOR_PAD (gst_d3d11_compositor_pad_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11CompositorPad, gst_d3d11_compositor_pad,
    GST, D3D11_COMPOSITOR_PAD, GstVideoAggregatorPad)

#define GST_TYPE_D3D11_COMPOSITOR (gst_d3d11_compositor_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11Compositor, gst_d3d11_compositor,
    GST, D3D11_COMPOSITOR, GstVideoAggregator)

typedef enum
{
  GST_D3D11_COMPOSITOR_BLEND_OP_ADD,
  GST_D3D11_COMPOSITOR_BLEND_OP_SUBTRACT,
  GST_D3D11_COMPOSITOR_BLEND_OP_REV_SUBTRACT,
  GST_D3D11_COMPOSITOR_BLEND_OP_MIN,
  GST_D3D11_COMPOSITOR_BLEND_OP_MAX
} GstD3D11CompositorBlendOperation;

#define GST_TYPE_D3D11_COMPOSITOR_BLEND_OPERATION (gst_d3d11_compositor_blend_operation_get_type())
GType gst_d3d11_compositor_blend_operation_get_type (void);

typedef enum
{
  GST_D3D11_COMPOSITOR_BLEND_ZERO,
  GST_D3D11_COMPOSITOR_BLEND_ONE,
  GST_D3D11_COMPOSITOR_BLEND_SRC_COLOR,
  GST_D3D11_COMPOSITOR_BLEND_INV_SRC_COLOR,
  GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA,
  GST_D3D11_COMPOSITOR_BLEND_INV_SRC_ALPHA,
  GST_D3D11_COMPOSITOR_BLEND_DEST_ALPHA,
  GST_D3D11_COMPOSITOR_BLEND_INV_DEST_ALPHA,
  GST_D3D11_COMPOSITOR_BLEND_DEST_COLOR,
  GST_D3D11_COMPOSITOR_BLEND_INV_DEST_COLOR,
  GST_D3D11_COMPOSITOR_BLEND_SRC_ALPHA_SAT,
  GST_D3D11_COMPOSITOR_BLEND_BLEND_FACTOR,
  GST_D3D11_COMPOSITOR_BLEND_INV_BLEND_FACTOR,
} GstD3D11CompositorBlend;

#define GST_TYPE_D3D11_COMPOSITOR_BLEND (gst_d3d11_compositor_blend_get_type())
GType gst_d3d11_compositor_blend_get_type (void);

typedef enum
{
  GST_D3D11_COMPOSITOR_BACKGROUND_CHECKER,
  GST_D3D11_COMPOSITOR_BACKGROUND_BLACK,
  GST_D3D11_COMPOSITOR_BACKGROUND_WHITE,
  GST_D3D11_COMPOSITOR_BACKGROUND_TRANSPARENT,
} GstD3D11CompositorBackground;

#define GST_TYPE_D3D11_COMPOSITOR_BACKGROUND (gst_d3d11_compositor_background_get_type())
GType gst_d3d11_compositor_background_get_type (void);

typedef enum
{
  GST_D3D11_COMPOSITOR_SIZING_POLICY_NONE,
  GST_D3D11_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO,
} GstD3D11CompositorSizingPolicy;

#define GST_TYPE_D3D11_COMPOSITOR_SIZING_POLICY (gst_d3d11_compositor_sizing_policy_get_type())
GType gst_d3d11_compositor_sizing_policy_get_type (void);

G_END_DECLS

#endif /* __GST_D3D11_COMPOSITOR_H__ */
