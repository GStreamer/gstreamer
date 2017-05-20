/* GStreamer
 * Copyright (C) 2016 Collabora Ltd. <guillaume.desmottes@collabora.co.uk>
 *
 * gstleaks.c: tracing module detecting object leaks
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

#ifndef __GST_LEAKS_TRACER_H__
#define __GST_LEAKS_TRACER_H__

#include <gst/gst.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

#define GST_TYPE_LEAKS_TRACER \
  (gst_leaks_tracer_get_type())
#define GST_LEAKS_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_LEAKS_TRACER,GstLeaksTracer))
#define GST_LEAKS_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_LEAKS_TRACER,GstLeaksTracerClass))
#define GST_IS_LEAKS_TRACER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_LEAKS_TRACER))
#define GST_IS_LEAKS_TRACER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_LEAKS_TRACER))
#define GST_LEAKS_TRACER_CAST(obj) ((GstLeaksTracer *)(obj))

typedef struct _GstLeaksTracer GstLeaksTracer;
typedef struct _GstLeaksTracerClass GstLeaksTracerClass;

/**
 * GstLeaksTracer:
 *
 * Opaque #GstLeaksTracer data structure
 */
struct _GstLeaksTracer {
  GstTracer parent;

  /*< private >*/
  /* gpointer (object currently alive) -> gchar * (its creation trace, or
   * NULL). Protected by object lock */
  GHashTable *objects;
  /* array of GType used as filtering */
  GArray *filter;
  /* Set of owned ObjectLog.  Protected by object lock */
  GHashTable *added;
  /* Set of owned ObjectLog.  Protected by object lock */
  GHashTable *removed;
  /* If not NULL, contain a set of string representing type filter not
   * (yet?) known by the type system.
   * Protected by object lock. */
  GHashTable *unhandled_filter;
  /* The number of elements in unhandled_filter */
  gint unhandled_filter_count;
  gboolean done;

  gboolean check_refs;

  GstStackTraceFlags trace_flags;
};

struct _GstLeaksTracerClass {
  GstTracerClass parent_class;
};

G_GNUC_INTERNAL GType gst_leaks_tracer_get_type (void);

G_END_DECLS

#endif /* __GST_LEAKS_TRACER_H__ */
