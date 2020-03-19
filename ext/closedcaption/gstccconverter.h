/*
 * GStreamer
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_CCCONVERTER_H__
#define __GST_CCCONVERTER_H__

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>

G_BEGIN_DECLS
#define GST_TYPE_CCCONVERTER \
  (gst_cc_converter_get_type())
#define GST_CCCONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CCCONVERTER,GstCCConverter))
#define GST_CCCONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CCCONVERTER,GstCCConverterClass))
#define GST_IS_CCCONVERTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CCCONVERTER))
#define GST_IS_CCCONVERTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CCCONVERTER))

typedef struct _GstCCConverter GstCCConverter;
typedef struct _GstCCConverterClass GstCCConverterClass;

#define MAX_CDP_PACKET_LEN 256
#define MAX_CEA608_LEN 32

struct _GstCCConverter
{
  GstBaseTransform parent;

  GstVideoCaptionType input_caption_type;
  GstVideoCaptionType output_caption_type;

  /* CDP sequence numbers when outputting CDP */
  guint16 cdp_hdr_sequence_cntr;

  gint in_fps_n, in_fps_d;
  gint out_fps_n, out_fps_d;

  /* for framerate differences, we need to keep previous/next frames in order
   * to split/merge data across multiple input or output buffers.  The data is
   * stored as cc_data */
  guint8    scratch_cea608_1[MAX_CEA608_LEN];
  guint     scratch_cea608_1_len;
  guint8    scratch_cea608_2[MAX_CEA608_LEN];
  guint     scratch_cea608_2_len;
  guint8    scratch_ccp[MAX_CDP_PACKET_LEN];
  guint     scratch_ccp_len;

  guint     input_frames;
  guint     output_frames;
  GstVideoTimeCode current_output_timecode;
  /* previous buffer for copying metas onto */
  GstBuffer *previous_buffer;
};

struct _GstCCConverterClass
{
  GstBaseTransformClass parent_class;
};

GType gst_cc_converter_get_type (void);

G_END_DECLS
#endif /* __GST_CCCONVERTER_H__ */
