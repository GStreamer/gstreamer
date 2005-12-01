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


#ifndef __GST_AUDIO_FILTER_H__
#define __GST_AUDIO_FILTER_H__


#include <gst/gst.h>


G_BEGIN_DECLS

typedef struct _GstAudioFilter GstAudioFilter;
typedef struct _GstAudioFilterClass GstAudioFilterClass;

typedef void (*GstAudioFilterFilterFunc)(GstAudioFilter *filter,
    GstBuffer *outbuf, GstBuffer *inbuf);
typedef void (*GstAudioFilterInplaceFilterFunc)(GstAudioFilter *filter,
    GstBuffer *buffer);

typedef void (*GstAudioFilterSetupFunc) (GstAudioFilter *filter);


#define GST_TYPE_AUDIO_FILTER \
  (gst_audio_filter_get_type())
#define GST_AUDIO_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_FILTER,GstAudioFilter))
#define GST_AUDIO_FILTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_FILTER,GstAudioFilterClass))
#define GST_IS_AUDIO_FILTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_FILTER))
#define GST_IS_AUDIO_FILTER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_FILTER))

struct _GstAudioFilter {
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

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstAudioFilterClass {
  GstElementClass parent_class;

  GstCaps *caps;
  GstAudioFilterSetupFunc setup;
  GstAudioFilterInplaceFilterFunc filter_inplace;
  GstAudioFilterFilterFunc filter;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_audio_filter_get_type(void);

void gst_audio_filter_class_add_pad_templates (GstAudioFilterClass *audiofilterclass, const GstCaps *caps);

G_END_DECLS

#endif /* __GST_AUDIO_FILTER_H__ */

