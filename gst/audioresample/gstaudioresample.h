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
#include <gst/base/gstbasetransform.h>

#include "resample.h"

G_BEGIN_DECLS

#define GST_TYPE_AUDIORESAMPLE \
  (gst_audioresample_get_type())
#define GST_AUDIORESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIORESAMPLE,GstAudioresample))
#define GST_AUDIORESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIORESAMPLE,GstAudioresampleClass))
#define GST_IS_AUDIORESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIORESAMPLE))
#define GST_IS_AUDIORESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIORESAMPLE))

typedef struct _GstAudioresample GstAudioresample;
typedef struct _GstAudioresampleClass GstAudioresampleClass;

/**
 * GstAudioresample:
 *
 * Opaque data structure.
 */
struct _GstAudioresample {
  GstBaseTransform element;

  GstCaps *srccaps, *sinkcaps;

  gboolean passthru;
  gboolean need_discont;

  guint64 offset;
  guint64 ts_offset;
  GstClockTime next_ts;
  GstClockTime prev_ts, prev_duration;
  int channels;

  int i_rate;
  int o_rate;
  int filter_length;

  ResampleState * resample;
};

struct _GstAudioresampleClass {
  GstBaseTransformClass parent_class;
};

GType gst_audioresample_get_type(void);

G_END_DECLS

#endif /* __AUDIORESAMPLE_H__ */
