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

#ifndef __GST_D3D11_COMPOSITOR_BIN_H__
#define __GST_D3D11_COMPOSITOR_BIN_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoaggregator.h>
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_COMPOSITOR_BIN_PAD (gst_d3d11_compositor_bin_pad_get_type())
#define GST_D3D11_COMPOSITOR_BIN_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_COMPOSITOR_BIN_PAD, GstD3D11CompositorBinPad))
#define GST_D3D11_COMPOSITOR_BIN_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_COMPOSITOR_BIN_PAD, GstD3D11CompositorBinPadClass))
#define GST_IS_D3D11_COMPOSITOR_BIN_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_COMPOSITOR_BIN_PAD))
#define GST_IS_D3D11_COMPOSITOR_BIN_PAD_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_COMPOSITOR_BIN_PAD))
#define GST_D3D11_COMPOSITOR_BIN_PAD_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_D3D11_COMPOSITOR_BIN_PAD,GstD3D11CompositorBinPadClass))

typedef struct _GstD3D11CompositorBinPad GstD3D11CompositorBinPad;
typedef struct _GstD3D11CompositorBinPadClass GstD3D11CompositorBinPadClass;

struct _GstD3D11CompositorBinPadClass
{
  GstGhostPadClass parent_class;

  void (*set_target) (GstD3D11CompositorBinPad * pad, GstPad * target);
};

GType gst_d3d11_compositor_bin_pad_get_type (void);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstD3D11CompositorBinPad, gst_object_unref)

#define GST_TYPE_D3D11_COMPOSITOR_BIN_INPUT (gst_d3d11_compositor_bin_input_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11CompositorBinInput, gst_d3d11_compositor_bin_input,
    GST, D3D11_COMPOSITOR_BIN_INPUT, GstD3D11CompositorBinPad);

#define GST_TYPE_D3D11_COMPOSITOR_BIN (gst_d3d11_compositor_bin_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11CompositorBin, gst_d3d11_compositor_bin,
    GST, D3D11_COMPOSITOR_BIN, GstBin)

G_END_DECLS

#endif /* __GST_D3D11_COMPOSITOR_BIN_H__ */
