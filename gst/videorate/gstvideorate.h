/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_VIDEO_RATE_H__
#define __GST_VIDEO_RATE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_RATE \
  (gst_video_rate_get_type())
#define GST_VIDEO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEO_RATE,GstVideoRate))
#define GST_VIDEO_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEO_RATE,GstVideoRateClass))
#define GST_IS_VIDEO_RATE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEO_RATE))
#define GST_IS_VIDEO_RATE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEO_RATE))

typedef struct _GstVideoRate GstVideoRate;
typedef struct _GstVideoRateClass GstVideoRateClass;

/**
 * GstVideoRate:
 *
 * Opaque data structure.
 */
struct _GstVideoRate
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  /* video state */
  gint from_rate_numerator, from_rate_denominator;
  gint to_rate_numerator, to_rate_denominator;
  guint64 next_ts;              /* Timestamp of next buffer to output */
  GstBuffer *prevbuf;
  guint64 prev_ts;              /* Previous buffer timestamp */
  guint64 segment_out;          /* in-segment counting */
  gboolean discont;
  guint64 last_ts;              /* Timestamp of last input buffer */

  /* segment handling */
  GstSegment segment;

  /* properties */
  guint64 in, out, dup, drop;
  gboolean silent;
  gdouble new_pref;
  gboolean skip_to_first;
};

struct _GstVideoRateClass
{
  GstElementClass parent_class;
};

GType gst_video_rate_get_type (void);

G_END_DECLS

#endif /* __GST_VIDEO_RATE_H__ */
