/* ASF muxer plugin for GStreamer
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_ASFMUX_H__
#define __GST_ASFMUX_H__

#include <gst/gst.h>

#include "asfheaders.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_ASFMUX \
  (gst_asfmux_get_type())
#define GST_ASFMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ASFMUX,GstAsfMux))
#define GST_ASFMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ASFMUX,GstAsfMuxClass))
#define GST_IS_ASFMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ASFMUX))
#define GST_IS_ASFMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ASFMUX))

#define MAX_ASF_OUTPUTS 16

typedef struct _GstAsfMuxStream {
  guint index;

  gint type; /* ASF_STREAM_VIDEO/AUDIO */
  GstPad *pad;
  guint64 time;
  GstBuffer *queue;
  gboolean connected, eos;
  guint seqnum;
  guint bitrate;

  union {
    asf_stream_audio        audio;
    struct {
      asf_stream_video        stream;
      asf_stream_video_format format;
    } video;
  } header;
} GstAsfMuxStream;

typedef struct _GstAsfMux {
  GstElement element;

  /* pads */
  GstPad *srcpad;
  GstAsfMuxStream output[MAX_ASF_OUTPUTS];
  guint num_outputs, num_video, num_audio;
  gboolean write_header;

  /* packet */
  GstBuffer *packet;
  guint num_packets, packet_frames;
  guint sequence;
  guint64 data_offset;
} GstAsfMux;

typedef struct _GstAsfMuxClass {
  GstElementClass parent_class;
} GstAsfMuxClass;

GType gst_asfmux_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_ASFMUX_H__ */
