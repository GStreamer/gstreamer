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


#ifndef __GST_AUDIOFILTER_H__
#define __GST_AUDIOFILTER_H__


#include <gst/gst.h>


G_BEGIN_DECLS

typedef struct _GstAudiofilter GstAudiofilter;
typedef struct _GstAudiofilterClass GstAudiofilterClass;

typedef void (*GstAudiofilterFilterFunc)(GstAudiofilter *filter,
    GstBuffer *outbuf, GstBuffer *inbuf);
typedef void (*GstAudiofilterInplaceFilterFunc)(GstAudiofilter *filter,
    GstBuffer *buffer);

typedef void (*GstAudiofilterSetupFunc) (GstAudiofilter *filter);


#define GST_TYPE_AUDIOFILTER \
  (gst_audiofilter_get_type())
#define GST_AUDIOFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIOFILTER,GstAudiofilter))
#define GST_AUDIOFILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIOFILTER,GstAudiofilterClass))
#define GST_IS_AUDIOFILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIOFILTER))
#define GST_IS_AUDIOFILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIOFILTER))

struct _GstAudiofilter {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  /* audio state */
  gboolean inited;
  gboolean passthru;

  int rate;
  int width;
  int channels;
  int depth;

  int n_samples;
  int size;
  int bytes_per_sample;
};

struct _GstAudiofilterClass {
  GstElementClass parent_class;

  GstCaps *caps;
  GstAudiofilterSetupFunc setup;
  GstAudiofilterInplaceFilterFunc filter_inplace;
  GstAudiofilterFilterFunc filter;
};

GType gst_audiofilter_get_type(void);

void gst_audiofilter_class_add_pad_templates (GstAudiofilterClass *audiofilterclass, const GstCaps *caps);

G_END_DECLS

#endif /* __GST_AUDIOFILTER_H__ */

