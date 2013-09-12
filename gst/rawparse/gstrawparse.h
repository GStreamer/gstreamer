/* GStreamer
 * Copyright (C) 2006 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstrawparse.h:
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

#ifndef __GST_RAW_PARSE_H__
#define __GST_RAW_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RAW_PARSE \
  (gst_raw_parse_get_type())
#define GST_RAW_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RAW_PARSE,GstRawParse))
#define GST_RAW_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RAW_PARSE,GstRawParseClass))
#define GST_RAW_PARSE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_RAW_PARSE,GstRawParseClass))
#define GST_IS_RAW_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RAW_PARSE))
#define GST_IS_RAW_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RAW_PARSE))

typedef struct _GstRawParse GstRawParse;
typedef struct _GstRawParseClass GstRawParseClass;

struct _GstRawParse
{
  GstElement parent;

  /* <private> */
  GstPad *sinkpad;
  GstPad *srcpad;

  GstPadMode mode;
  GstAdapter *adapter;

  gint framesize;
  gint fps_d;
  gint fps_n;

  gboolean discont;
  guint64 n_frames;

  gint64 upstream_length;
  gint64 offset;

  GstSegment segment;
  GstEvent *start_segment;

  gboolean negotiated;
  gboolean push_stream_start;
};

struct _GstRawParseClass
{
  GstElementClass parent_class;

  GstCaps * (*get_caps) (GstRawParse *rp);
  void (*set_buffer_flags) (GstRawParse *rp, GstBuffer *buffer);

  gboolean multiple_frames_per_buffer;
};

GType gst_raw_parse_get_type (void);

void gst_raw_parse_class_set_src_pad_template (GstRawParseClass *klass, const GstCaps * allowed_caps);
void gst_raw_parse_class_set_multiple_frames_per_buffer (GstRawParseClass *klass, gboolean multiple_frames);

void gst_raw_parse_set_framesize (GstRawParse *rp, int framesize);
void gst_raw_parse_set_fps (GstRawParse *rp, int fps_n, int fps_d);
void gst_raw_parse_get_fps (GstRawParse *rp, int *fps_n, int *fps_d);
gboolean gst_raw_parse_is_negotiated (GstRawParse *rp);

G_END_DECLS

#endif /* __GST_RAW_PARSE_H__ */
