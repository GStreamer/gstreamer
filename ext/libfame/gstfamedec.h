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


#ifndef __GST_FAMEDEC_H__
#define __GST_FAMEDEC_H__

#include <gst/gst.h>
#include <fame.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_FAMEDEC \
  (gst_famedec_get_type())
#define GST_FAMEDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FAMEDEC,GstFameDec))
#define GST_FAMEDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FAMEDEC,GstFameDec))
#define GST_IS_FAMEDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FAMEDEC))
#define GST_IS_FAMEDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FAMEDEC))

typedef struct _GstFameDec GstFameDec;
typedef struct _GstFameDecClass GstFameDecClass;

struct _GstFameDec {
  GstElement element;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* the timestamp of the next frame */
  guint64 next_time;
  /* the interval between frames */
  guint64 time_interval;

  /* video state */
  gint format;
  /* the size of the output buffer */
  gint outsize;

  /* decoding pattern string */
  gchar *pattern;

  /* famedec stuff */
  gboolean verbose;
  fame_context_t *fc;
  fame_parameters_t fp;
  fame_yuv_t fy;
  gulong buffer_size;
  unsigned char *buffer;
  gboolean initialized;
};

struct _GstFameDecClass {
  GstElementClass parent_class;
};

GType gst_famedec_get_type (void);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_FAMEDEC_H__ */
