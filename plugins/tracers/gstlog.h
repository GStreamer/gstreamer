/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gstlog.h: tracing module that logs events
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

#ifndef __GST_LOG_TRACER_H__
#define __GST_LOG_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

#define GST_TYPE_LOG_TRACER \
  (gst_log_tracer_get_type())
#define GST_LOG_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LOG_TRACER,GstLogTracer))
#define GST_LOG_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LOG_TRACER,GstLogTracerClass))
#define GST_IS_LOG_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LOG_TRACER))
#define GST_IS_LOG_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LOG_TRACER))
#define GST_LOG_TRACER_CAST(obj) ((GstLogTracer *)(obj))

typedef struct _GstLogTracer GstLogTracer;
typedef struct _GstLogTracerClass GstLogTracerClass;

/**
 * GstLogTracer:
 *
 * Opaque #GstLogTracer data structure
 */
struct _GstLogTracer {
  GstTracer 	 parent;

  /*< private >*/
};

struct _GstLogTracerClass {
  GstTracerClass parent_class;

  /* signals */
};

G_GNUC_INTERNAL GType gst_log_tracer_get_type (void);

G_END_DECLS

#endif /* __GST_LOG_TRACER_H__ */
