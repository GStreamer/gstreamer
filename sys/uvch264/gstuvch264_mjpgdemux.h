/* GStreamer
 *
 * uvch264_mjpg_demux: a demuxer for muxed stream in UVC H264 compliant MJPG
 *
 * Copyright (C) 2012 Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_UVC_H264_MJPG_DEMUX_H__
#define __GST_UVC_H264_MJPG_DEMUX_H__

#include <gst/gst.h>


G_BEGIN_DECLS

#define GST_TYPE_UVC_H264_MJPG_DEMUX             \
  (gst_uvc_h264_mjpg_demux_get_type())
#define GST_UVC_H264_MJPG_DEMUX(obj)             \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),            \
      GST_TYPE_UVC_H264_MJPG_DEMUX,              \
      GstUvcH264MjpgDemux))
#define GST_UVC_H264_MJPG_DEMUX_CLASS(klass)     \
  (G_TYPE_CHECK_CLASS_CAST((klass),             \
      GST_TYPE_UVC_H264_MJPG_DEMUX,              \
      GstUvcH264MjpgDemuxClass))
#define GST_IS_UVC_H264_MJPG_DEMUX(obj)          \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),            \
      GST_TYPE_UVC_H264_MJPG_DEMUX))
#define GST_IS_UVC_H264_MJPG_DEMUX_CLASS(klass)  \
  (G_TYPE_CHECK_CLASS_TYPE((klass),             \
      GST_TYPE_UVC_H264_MJPG_DEMUX))

typedef struct _GstUvcH264MjpgDemux           GstUvcH264MjpgDemux;
typedef struct _GstUvcH264MjpgDemuxPrivate    GstUvcH264MjpgDemuxPrivate;
typedef struct _GstUvcH264MjpgDemuxClass      GstUvcH264MjpgDemuxClass;

struct _GstUvcH264MjpgDemux {
  GstElement element;
  GstUvcH264MjpgDemuxPrivate *priv;
};

struct _GstUvcH264MjpgDemuxClass {
  GstElementClass  parent_class;
};

GType gst_uvc_h264_mjpg_demux_get_type (void);

G_END_DECLS

#endif /* __GST_UVC_H264_MJPG_DEMUX_H__ */
