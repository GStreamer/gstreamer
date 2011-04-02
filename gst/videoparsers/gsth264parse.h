/* GStreamer H.264 Parser
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
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

#ifndef __GST_H264_PARSE_H__
#define __GST_H264_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

#include "h264parse.h"

G_BEGIN_DECLS

typedef struct _H264Params H264Params;

#define GST_TYPE_H264_PARSE \
  (gst_h264_parse_get_type())
#define GST_H264_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264_PARSE,GstH264Parse))
#define GST_H264_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264_PARSE,GstH264ParseClass))
#define GST_IS_H264_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264_PARSE))
#define GST_IS_H264_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264_PARSE))

GType gst_h264_parse_get_type (void);

typedef struct _GstH264Parse GstH264Parse;
typedef struct _GstH264ParseClass GstH264ParseClass;

struct _GstH264Parse
{
  GstBaseParse baseparse;

  GstPadChainFunction parse_chain;

  /* stream */
  gint width, height;
  gint fps_num, fps_den;
  GstBuffer *codec_data;
  guint nal_length_size;
  gboolean packetized;

  /* state */
  GstH264Params *params;
  guint align;
  guint format;

  GstClockTime last_report;
  gboolean push_codec;

  /* frame parsing */
  guint last_nal_pos;
  guint next_sc_pos;
  gint idr_pos;
  gboolean update_caps;
  GstAdapter *frame_out;
  gboolean keyframe;
  gboolean frame_start;
  /* AU state */
  gboolean picture_start;

  /* props */
  gboolean split_packetized;
  guint interval;
};

struct _GstH264ParseClass
{
  GstBaseParseClass parent_class;
};

G_END_DECLS
#endif
