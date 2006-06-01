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


#ifndef __AUDIORESAMPLE_H__
#define __AUDIORESAMPLE_H__


#include <gst/gst.h>

#include <audioresample/resample.h>


G_BEGIN_DECLS


#define GST_TYPE_AUDIORESAMPLE \
  (audioresample_get_type())
#define GST_AUDIORESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIORESAMPLE,Audioresample))
#define GST_AUDIORESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIORESAMPLE,AudioresampleClass))
#define GST_IS_AUDIORESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIORESAMPLE))
#define GST_IS_AUDIORESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIORESAMPLE))

typedef struct _Audioresample Audioresample;
typedef struct _AudioresampleClass AudioresampleClass;

struct _Audioresample {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  gboolean passthru;

  gint64 offset;
  int channels;

  int i_rate;
  int o_rate;
  int filter_length;

  ResampleState * resample;
};

struct _AudioresampleClass {
  GstElementClass parent_class;
};

GType gst_audioresample_get_type(void);


G_END_DECLS


#endif /* __AUDIORESAMPLE_H__ */
