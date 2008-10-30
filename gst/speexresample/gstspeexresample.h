/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2007> Sebastian Dröge <slomo@circular-chaos.org>
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


#ifndef __SPEEX_RESAMPLE_H__
#define __SPEEX_RESAMPLE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>

#include "speex_resampler_wrapper.h"

G_BEGIN_DECLS

#define GST_TYPE_SPEEX_RESAMPLE \
  (gst_speex_resample_get_type())
#define GST_SPEEX_RESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SPEEX_RESAMPLE,GstSpeexResample))
#define GST_SPEEX_RESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SPEEX_RESAMPLE,GstSpeexResampleClass))
#define GST_IS_SPEEX_RESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SPEEX_RESAMPLE))
#define GST_IS_SPEEX_RESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SPEEX_RESAMPLE))

typedef struct _GstSpeexResample GstSpeexResample;
typedef struct _GstSpeexResampleClass GstSpeexResampleClass;

/**
 * GstSpeexResample:
 *
 * Opaque data structure.
 */
struct _GstSpeexResample {
  GstBaseTransform element;

  /* <private> */

  GstCaps *srccaps, *sinkcaps;

  gboolean need_discont;

  guint64 next_offset;
  GstClockTime next_ts;
  GstClockTime next_upstream_ts;
  
  gint channels;
  gint inrate;
  gint outrate;
  gint quality;
  gint width;
  gboolean fp;

  SpeexResamplerState *state;
  const SpeexResampleFuncs *funcs;
};

struct _GstSpeexResampleClass {
  GstBaseTransformClass parent_class;
};

GType gst_speex_resample_get_type(void);

G_END_DECLS

#endif /* __SPEEX_RESAMPLE_H__ */
