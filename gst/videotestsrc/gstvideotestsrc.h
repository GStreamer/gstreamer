/* GStreamer
 * Copyright (C) <2002> David A. Schleef <ds@schleef.org>
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


#ifndef __GST_VIDEOTESTSRC_H__
#define __GST_VIDEOTESTSRC_H__


#include <config.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEOTESTSRC \
  (gst_videotestsrc_get_type())
#define GST_VIDEOTESTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VIDEOTESTSRC,GstVideotestsrc))
#define GST_VIDEOTESTSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VIDEOTESTSRC,GstVideotestsrc))
#define GST_IS_VIDEOTESTSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VIDEOTESTSRC))
#define GST_IS_VIDEOTESTSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VIDEOTESTSRC))

typedef enum {
  GST_VIDEOTESTSRC_POINT_SAMPLE,
  GST_VIDEOTESTSRC_NEAREST,
  GST_VIDEOTESTSRC_BILINEAR,
  GST_VIDEOTESTSRC_BICUBIC
} GstVideoTestSrcMethod;

typedef struct _GstVideotestsrc GstVideotestsrc;
typedef struct _GstVideotestsrcClass GstVideotestsrcClass;

struct _GstVideotestsrc {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* video state */
  guint32 format;
  gint width;
  gint height;
  gint forced_format;
  
  /* private */
  gint64 timestamp;
  gint64 interval;
  gint bpp;
  int rate;

  GstBufferPool *pool;

  void (*make_image)(GstVideotestsrc *v, unsigned char *dest, int w, int h);
};

struct _GstVideotestsrcClass {
  GstElementClass parent_class;
};

static GType gst_videotestsrc_get_type(void);

static void gst_videotestsrc_setup(GstVideotestsrc *);

G_END_DECLS

#endif /* __GST_VIDEOTESTSRC_H__ */
