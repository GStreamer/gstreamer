/* GStreamer Jasper based j2k image decoder
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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

#ifndef __GST_JASPER_DEC_H__
#define __GST_JASPER_DEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include <jasper/jasper.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_JASPER_DEC			\
  (gst_jasper_dec_get_type())
#define GST_JASPER_DEC(obj)						\
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JASPER_DEC,GstJasperDec))
#define GST_JASPER_DEC_CLASS(klass)					\
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JASPER_DEC,GstJasperDecClass))
#define GST_IS_JASPER_DEC(obj)					\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JASPER_DEC))
#define GST_IS_JASPER_DEC_CLASS(klass)				\
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JASPER_DEC))

typedef struct _GstJasperDec      GstJasperDec;
typedef struct _GstJasperDecClass GstJasperDecClass;

#define GST_JASPER_DEC_MAX_COMPONENT  4

struct _GstJasperDec
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstBuffer *codec_data;

  /* jasper image fmt */
  gint fmt;
  jas_clrspc_t clrspc;
  gint strip;

  /* stream/image properties */
  GstVideoFormat format;
  gint width;
  gint height;
  gint channels;
  guint image_size;
  gint stride[GST_JASPER_DEC_MAX_COMPONENT];
  gint offset[GST_JASPER_DEC_MAX_COMPONENT];
  gint inc[GST_JASPER_DEC_MAX_COMPONENT];
  gboolean alpha;
  glong *buf;

  /* image cmpt indexed */
  gint cwidth[GST_JASPER_DEC_MAX_COMPONENT];
  gint cheight[GST_JASPER_DEC_MAX_COMPONENT];
  /* standard video_format indexed */
  gint cmpt[GST_JASPER_DEC_MAX_COMPONENT];

  gint framerate_numerator;
  gint framerate_denominator;

  GstSegment segment;
  gboolean discont;

  /* QoS stuff *//* with LOCK */
  gdouble proportion;
  GstClockTime earliest_time;
};

struct _GstJasperDecClass
{
  GstElementClass parent_class;
};

GType gst_jasper_dec_get_type (void);

G_END_DECLS

#endif /* __GST_JASPER_DEC_H__ */
