/* -*- c-basic-offset: 2 -*-
 * GStreamer
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


#ifndef __GST_PASSTHROUGH_H__
#define __GST_PASSTHROUGH_H__


#include <config.h>
#include <gst/gst.h>
// #include <gst/meta/audioraw.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_PASSTHROUGH \
  (gst_passthrough_get_type())
#define GST_PASSTHROUGH(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PASSTHROUGH,GstPassthrough))
#define GST_PASSTHROUGH_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstPassthrough))
#define GST_IS_PASSTHROUGH(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PASSTHROUGH))
#define GST_IS_PASSTHROUGH_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PASSTHROUGH))

typedef struct _GstPassthrough GstPassthrough;
typedef struct _GstPassthroughClass GstPassthroughClass;
typedef enum _GstPassthroughFormat GstPassthroughFormat;

enum _GstPassthroughFormat {
  GST_PASSTHROUGH_FORMAT_INT,
  GST_PASSTHROUGH_FORMAT_FLOAT
};

struct _GstPassthrough {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean silent;
  
  /* the next three are valid for both int and float */
  
  GstPassthroughFormat format;
  
  guint rate;
  
  guint channels;
  
  /* the next five are valid only for format==GST_PASSTHROUGH_FORMAT_INT */
  
  guint width;
  
  guint depth;

  guint endianness;
  
  guint law;
  
  gboolean is_signed;
  
  /* the next three are valid only for format==GST_PASSTHROUGH_FORMAT_FLOAT */
  
  const gchar *layout;
  
  gfloat slope;
  
  gfloat intercept;
};

struct _GstPassthroughClass {
  GstElementClass parent_class;
};

GType gst_passthrough_get_type(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PASSTHROUGH_H__ */
