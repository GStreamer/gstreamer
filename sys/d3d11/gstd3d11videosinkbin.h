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

G_BEGIN_DECLS

#define GST_TYPE_D3D11_VIDEO_SINK_BIN (gst_d3d11_video_sink_bin_get_type())
G_DECLARE_FINAL_TYPE (GstD3D11VideoSinkBin,
    gst_d3d11_video_sink_bin, GST, D3D11_VIDEO_SINK_BIN, GstBin);

G_END_DECLS

#endif /* __GST_D3D11_VIDEO_SINK_BIN_H__ */
