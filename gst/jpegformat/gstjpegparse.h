/* GStreamer
 *
 * jpegparse: a parser for JPEG streams
 *
 * Copyright (C) <2009> Arnout Vandecappelle (Essensium/Mind) <arnout@mind.be>
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

#ifndef __GST_JPEG_PARSE_H__
#define __GST_JPEG_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbaseparse.h>

#include "gstjpegformat.h"

G_BEGIN_DECLS

#define GST_TYPE_JPEG_PARSE \
  (gst_jpeg_parse_get_type())
#define GST_JPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JPEG_PARSE,GstJpegParse))
#define GST_JPEG_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JPEG_PARSE,GstJpegParseClass))
#define GST_IS_JPEG_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JPEG_PARSE))
#define GST_IS_JPEG_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JPEG_PARSE))
#define GST_JPEG_PARSE_CAST(obj) ((GstJpegParse *)obj)

typedef struct _GstJpegParse           GstJpegParse;
typedef struct _GstJpegParseClass      GstJpegParseClass;

struct _GstJpegParse {
  GstBaseParse parse;

  guint last_offset;
  guint last_entropy_len;
  gboolean last_resync;

  /* negotiated state */
  gint caps_width, caps_height;
  gint caps_framerate_numerator;
  gint caps_framerate_denominator;

  /* the parsed frame size */
  guint16 width, height;

  /* format color space */
  const gchar *format;

  /* TRUE if the src caps sets a specific framerate */
  gboolean has_fps;

  /* the (expected) timestamp of the next frame */
  guint64 next_ts;

  /* duration of the current frame */
  guint64 duration;

  /* video state */
  gint framerate_numerator;
  gint framerate_denominator;

  /* tags */
  GstTagList *tags;
};

struct _GstJpegParseClass {
  GstBaseParseClass  parent_class;
};

GType gst_jpeg_parse_get_type (void);

G_END_DECLS

#endif /* __GST_JPEG_PARSE_H__ */
