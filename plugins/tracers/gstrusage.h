/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstrusage.h: tracing module that logs resource usage stats
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_RUSAGE_TRACER_H__
#define __GST_RUSAGE_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

#define GST_TYPE_RUSAGE_TRACER \
  (gst_rusage_tracer_get_type())
#define GST_RUSAGE_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RUSAGE_TRACER,GstRUsageTracer))
#define GST_RUSAGE_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RUSAGE_TRACER,GstRUsageTracerClass))
#define GST_IS_RUSAGE_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RUSAGE_TRACER))
#define GST_IS_RUSAGE_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RUSAGE_TRACER))
#define GST_RUSAGE_TRACER_CAST(obj) ((GstRUsageTracer *)(obj))

typedef struct _GstRUsageTracer GstRUsageTracer;
typedef struct _GstRUsageTracerClass GstRUsageTracerClass;

typedef struct
{
  GstClockTime ts;
  GstClockTime val;
} GstTraceValue;

typedef struct
{
  GstClockTime window;
  GQueue values;                /* GstTraceValue* */
} GstTraceValues;

/**
 * GstRUsageTracer:
 *
 * Opaque #GstRUsageTracer data structure
 */
struct _GstRUsageTracer {
  GstTracer 	 parent;

  /*< private >*/        
  GHashTable *threads;
  GstTraceValues *tvs_proc;

  /* for ts calibration */
  gpointer main_thread_id;
  guint64 tproc_base;
};

struct _GstRUsageTracerClass {
  GstTracerClass parent_class;

  /* signals */
};

G_GNUC_INTERNAL GType gst_rusage_tracer_get_type (void);

G_END_DECLS

#endif /* __GST_RUSAGE_TRACER_H__ */
