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


#ifndef __AUDIOSCALE_H__
#define __AUDIOSCALE_H__


#include <gst/gst.h>

#include <gst/resample/resample.h>


G_BEGIN_DECLS


#define GST_TYPE_AUDIOSCALE \
  (audioscale_get_type())
#define GST_AUDIOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOSCALE,Audioscale))
#define GST_AUDIOSCALE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOSCALE,Audioscale))
#define GST_IS_AUDIOSCALE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOSCALE))
#define GST_IS_AUDIOSCALE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOSCALE))

typedef enum {
  GST_AUDIOSCALE_NEAREST,
  GST_AUDIOSCALE_BILINEAR,
  GST_AUDIOSCALE_SINC,
} GstAudioScaleMethod;

typedef struct _Audioscale Audioscale;
typedef struct _AudioscaleClass AudioscaleClass;

struct _Audioscale {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* audio state */
  gboolean passthru;
  gint64 offset;

  resample_t *resample;

  GstBuffer *outbuf;
};

struct _AudioscaleClass {
  GstElementClass parent_class;
};

GType gst_audioscale_get_type(void);


G_END_DECLS


#endif /* __AUDIOSCALE_H__ */
