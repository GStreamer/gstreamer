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


#ifndef __LEGACYRESAMPLE_H__
#define __LEGACYRESAMPLE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "resample.h"

G_BEGIN_DECLS

#define GST_TYPE_LEGACYRESAMPLE \
  (gst_legacyresample_get_type())
#define GST_LEGACYRESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LEGACYRESAMPLE,GstLegacyresample))
#define GST_LEGACYRESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LEGACYRESAMPLE,GstLegacyresampleClass))
#define GST_IS_LEGACYRESAMPLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LEGACYRESAMPLE))
#define GST_IS_LEGACYRESAMPLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LEGACYRESAMPLE))

typedef struct _GstLegacyresample GstLegacyresample;
typedef struct _GstLegacyresampleClass GstLegacyresampleClass;

/**
 * GstLegacyresample:
 *
 * Opaque data structure.
 */
struct _GstLegacyresample {
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

struct _GstLegacyresampleClass {
  GstBaseTransformClass parent_class;
};

GType gst_legacyresample_get_type(void);

G_END_DECLS

#endif /* __LEGACYRESAMPLE_H__ */
