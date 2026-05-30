/* GStreamer
 * Copyright (C) 2016 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerrecord.h: tracer log record class
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

#ifndef __GST_TRACER_RECORD_H__
#define __GST_TRACER_RECORD_H__

#include <gst/gstobject.h>
#include <gst/gsttracer.h>

G_BEGIN_DECLS

/**
 * GstTracerRecord:
 *
 * The opaque GstTracerRecord instance structure
 *
 * Since: 1.8
 */
typedef struct _GstTracerRecord GstTracerRecord;
typedef struct _GstTracerRecordClass GstTracerRecordClass;

#define GST_TYPE_TRACER_RECORD            (gst_tracer_record_get_type())
#define GST_TRACER_RECORD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRACER_RECORD,GstTracerRecord))
#define GST_TRACER_RECORD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRACER_RECORD,GstTracerRecordClass))
#define GST_IS_TRACER_RECORD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRACER_RECORD))
#define GST_IS_TRACER_RECORD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRACER_RECORD))
#define GST_TRACER_RECORD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_TRACER_RECORD,GstTracerRecordClass))
#define GST_TRACER_RECORD_CAST(obj)       ((GstTracerRecord *)(obj))

GST_API
GType gst_tracer_record_get_type          (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTracerRecord, gst_object_unref)

GST_DEPRECATED_FOR(gst_trace_format_register)
GstTracerRecord * gst_tracer_record_new (const gchar * name, const gchar * firstfield, ...) G_GNUC_NULL_TERMINATED G_GNUC_WARN_UNUSED_RESULT;

#ifndef GST_DISABLE_GST_DEBUG
GST_DEPRECATED_FOR(gst_trace_event)
void              gst_tracer_record_log (GstTracerRecord *self, ...);
#else
#define gst_tracer_record_log(...) G_STMT_START {} G_STMT_END
#endif

G_END_DECLS

#endif /* __GST_TRACER_RECORD_H__ */
