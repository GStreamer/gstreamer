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

#ifndef GST_USE_UNSTABLE_API
#warning "The tracer subsystem is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gstobject.h>

G_BEGIN_DECLS

typedef struct _GstTracerRecord GstTracerRecord;
typedef struct _GstTracerRecordPrivate GstTracerRecordPrivate;
typedef struct _GstTracerRecordClass GstTracerRecordClass;

#define GST_TYPE_TRACER_RECORD            (gst_tracer_record_get_type())
#define GST_TRACER_RECORD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRACER_RECORD,GstTracerRecord))
#define GST_TRACER_RECORD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRACER_RECORD,GstTracerRecordClass))
#define GST_IS_TRACER_RECORD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRACER_RECORD))
#define GST_IS_TRACER_RECORD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRACER_RECORD))
#define GST_TRACER_RECORD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_TRACER_RECORD,GstTracerRecordClass))
#define GST_TRACER_RECORD_CAST(obj)       ((GstTracerRecord *)(obj))

GType gst_tracer_record_get_type          (void);

GstTracerRecord * gst_tracer_record_new (GstStructure *spec);
void gst_tracer_record_log (GstTracerRecord *self, ...);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstTracerRecord, gst_object_unref)
#endif

/**
 * GstTracerValueScope:
 * @GST_TRACER_VALUE_SCOPE_PROCESS: the value is related to the process
 * @GST_TRACER_VALUE_SCOPE_THREAD: the value is related to a thread
 * @GST_TRACER_VALUE_SCOPE_ELEMENT: the value is related to an #GstElement
 * @GST_TRACER_VALUE_SCOPE_PAD: the value is related to a #GstPad
 *
 * Tracing record will contain fields that contain a meassured value or extra
 * meta-data. One such meta data are values that tell where a measurement was
 * taken. This enumerating declares to which scope such a meta data field
 * relates to. If it is e.g. %GST_TRACER_VALUE_SCOPE_PAD, then each of the log
 * events may contain values for different #GstPads.
 */
typedef enum
{
  GST_TRACER_VALUE_SCOPE_PROCESS,
  GST_TRACER_VALUE_SCOPE_THREAD,
  GST_TRACER_VALUE_SCOPE_ELEMENT,
  GST_TRACER_VALUE_SCOPE_PAD
} GstTracerValueScope;

G_END_DECLS

#endif /* __GST_TRACER_RECORD_H__ */
