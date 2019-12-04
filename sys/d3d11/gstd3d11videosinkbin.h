/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_VIDEO_SINK_BIN_H__
#define __GST_D3D11_VIDEO_SINK_BIN_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>

#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

#define GST_TYPE_D3D11_VIDEO_SINK_BIN             (gst_d3d11_video_sink_bin_get_type())
#define GST_D3D11_VIDEO_SINK_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_VIDEO_SINK_BIN,GstD3D11VideoSinkBin))
#define GST_D3D11_VIDEO_SINK_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_VIDEO_SINK_BIN,GstD3D11VideoSinkBinClass))
#define GST_D3D11_VIDEO_SINK_BIN_GET_CLASS(obj)   (GST_D3D11_VIDEO_SINK_BIN_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D11_VIDEO_SINK_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_VIDEO_SINK_BIN))
#define GST_IS_D3D11_VIDEO_SINK_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_VIDEO_SINK_BIN))

typedef struct _GstD3D11VideoSinkBin GstD3D11VideoSinkBin;
typedef struct _GstD3D11VideoSinkBinClass GstD3D11VideoSinkBinClass;

struct _GstD3D11VideoSinkBin
{
  GstBin parent;

  GstPad *sinkpad;

  GstElement *upload;
  GstElement *sink;
};

struct _GstD3D11VideoSinkBinClass
{
  GstBinClass parent_class;
};

GType    gst_d3d11_video_sink_bin_get_type (void);

G_END_DECLS


#endif /* __GST_D3D11_VIDEO_SINK_BIN_H__ */
