/* GStreamer
 * Copyright (C) 2016 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerrecord.c: tracer log record class
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

/**
 * SECTION:gsttracerrecord
 * @title: GstTracerRecord
 * @short_description: Trace log entry class
 *
 * Tracing modules will create instances of this class to announce the data they
 * will log and create a log formatter.
 *
 * Since: 1.8
 * Deprecated: 1.30: Use #GstTraceFormat and gst_trace_event() instead.
 */

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gstinfo.h"
#include "gststructure.h"
#include "gsttracerrecord.h"
#include "gstvalue.h"
#include <gobject/gvaluecollector.h>

/* The GstTracerRecord API is deprecated and now a thin shim over the
 * GstTraceFormat / gst_trace_event() API; keep using it internally without
 * triggering the deprecation warning. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS struct _GstTracerRecord
{
  GstObject parent;

  GstTraceFormat *format;
};

struct _GstTracerRecordClass
{
  GstObjectClass parent_class;
};

#define gst_tracer_record_parent_class parent_class
G_DEFINE_TYPE (GstTracerRecord, gst_tracer_record, GST_TYPE_OBJECT);

static GstTracerFieldType
gst_tracer_record_map_type (GType type)
{
  if (type == G_TYPE_BOOLEAN)
    return GST_TRACER_FIELD_TYPE_BOOLEAN;
  if (type == G_TYPE_INT)
    return GST_TRACER_FIELD_TYPE_INT;
  if (type == G_TYPE_UINT)
    return GST_TRACER_FIELD_TYPE_UINT;
  if (type == G_TYPE_INT64)
    return GST_TRACER_FIELD_TYPE_INT64;
  if (type == G_TYPE_UINT64)
    return GST_TRACER_FIELD_TYPE_UINT64;
  if (type == GST_TYPE_CLOCK_TIME)
    return GST_TRACER_FIELD_TYPE_CLOCK_TIME;
  if (type == G_TYPE_DOUBLE)
    return GST_TRACER_FIELD_TYPE_DOUBLE;
  if (type == G_TYPE_STRING)
    return GST_TRACER_FIELD_TYPE_STRING;
  if (type == GST_TYPE_STRUCTURE)
    return GST_TRACER_FIELD_TYPE_STRUCTURE;
  if (G_TYPE_IS_ENUM (type))
    return GST_TRACER_FIELD_TYPE_INT;
  if (G_TYPE_IS_FLAGS (type))
    return GST_TRACER_FIELD_TYPE_UINT;
  if (G_TYPE_IS_OBJECT (type))
    return GST_TRACER_FIELD_TYPE_OBJECT;

  g_warning ("Unsupported GstTracerRecord field type %s, logging as uint64",
      g_type_name (type));
  return GST_TRACER_FIELD_TYPE_UINT64;
}

static void
gst_tracer_record_class_init (GstTracerRecordClass * klass)
{
}

static void
gst_tracer_record_init (GstTracerRecord * self)
{
}

/**
 * gst_tracer_record_new:
 * @name: name of new record, must end on ".class".
 * @firstfield: name of first field to set
 * @...: additional arguments
 *
 * Create a new tracer record. The record instance can be used to efficiently
 * log entries using gst_tracer_record_log().
 * %NULL terminator required after the last argument.
 *
 * The @name without the ".class" suffix will be used for the log records.
 * There must be fields for each value that gets logged where the field name is
 * the value name. The field must be a #GstStructure describing the value. The
 * sub structure must contain a field called 'type' of %G_TYPE_GTYPE that
 * contains the GType of the value. The resulting #GstTracerRecord will take
 * ownership of the field structures.
 *
 * The way to deal with optional values is to log an additional boolean before
 * the optional field, that if %TRUE signals that the optional field is valid
 * and %FALSE signals that the optional field should be ignored. One must still
 * log a placeholder value for the optional field though. Please also note, that
 * pointer type values must not be NULL - the underlying serialisation can not
 * handle that right now.
 *
 * Returns: (transfer full): a new #GstTracerRecord
 *
 * Since: 1.8
 * Deprecated: 1.30: Build a #GstTraceFormat with
 *   gst_trace_format_builder_new() / gst_trace_format_register() instead.
 */
GstTracerRecord *
gst_tracer_record_new (const gchar * name, const gchar * firstfield, ...)
{
  GstTracerRecord *self;
  GstTraceFormatBuilder *builder;
  va_list varargs;
  gchar *format_name, *p;
  GType type;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (g_str_has_suffix (name, ".class"), NULL);

  /* the records are logged under @name without the ".class" suffix */
  format_name = g_strdup (name);
  p = strrchr (format_name, '.');
  *p = '\0';
  builder = gst_trace_format_builder_new (format_name);
  g_free (format_name);

  va_start (varargs, firstfield);
  while (firstfield) {
    GValue val = G_VALUE_INIT;
    gchar *err = NULL;
    const GstStructure *sub;
    GstTraceField *field;
    GType field_type = G_TYPE_INVALID;
    GstTracerValueScope scope;
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    type = va_arg (varargs, GType);
    if (type != GST_TYPE_STRUCTURE) {
      GST_ERROR ("expected field of type GstStructure, but %s is %s",
          firstfield, g_type_name (type));
      va_end (varargs);
      gst_trace_format_builder_free (builder);
      return NULL;
    }

    /* borrow the caller's structure, ownership is taken below */
    G_VALUE_COLLECT_INIT (&val, type, varargs, G_VALUE_NOCOPY_CONTENTS, &err);
    if (G_UNLIKELY (err)) {
      g_critical ("%s", err);
      g_free (err);
      break;
    }

    sub = gst_value_get_structure (&val);
    gst_structure_get (sub, "type", G_TYPE_GTYPE, &field_type, "flags",
        GST_TYPE_TRACER_VALUE_FLAGS, &flags, NULL);

    field = gst_trace_field_new (firstfield,
        gst_tracer_record_map_type (field_type));
    if (gst_structure_get_enum (sub, "scope", GST_TYPE_TRACER_VALUE_SCOPE,
            (gint *) & scope))
      gst_trace_field_set_scope (field, scope);
    if (flags != GST_TRACER_VALUE_FLAGS_NONE)
      gst_trace_field_set_flags (field, flags);
    gst_trace_format_builder_add_field_full (builder, field);

    /* take ownership of the borrowed structure so g_value_unset() frees it */
    val.data[1].v_uint &= ~G_VALUE_NOCOPY_CONTENTS;
    g_value_unset (&val);

    firstfield = va_arg (varargs, const gchar *);
  }
  va_end (varargs);

  self = g_object_new (GST_TYPE_TRACER_RECORD, NULL);
  /* Clear floating flag */
  gst_object_ref_sink (self);

  self->format = gst_trace_format_builder_register (builder);

  return self;
}

#ifndef GST_DISABLE_GST_DEBUG
/**
 * gst_tracer_record_log:
 * @self: the tracer-record
 * @...: the args as described in the spec-
 *
 * Serialzes the trace event into the log.
 *
 * Right now this is using the gstreamer debug log with the level TRACE (7) and
 * the category "GST_TRACER".
 *
 * Since: 1.8
 * Deprecated: 1.30: Use gst_trace_event() instead.
 */
void
gst_tracer_record_log (GstTracerRecord * self, ...)
{
  va_list var_args;
  guint n_fields, i, vi, n_vals;
  GstTraceValue *values;

  /* The event is delivered through the "event" tracer hook; skip building it
   * when no tracer is listening. */
  if (G_LIKELY (!gst_trace_format_is_enabled (self->format)))
    return;

  n_fields = gst_trace_format_get_n_fields (self->format);

  /* each optional field is preceded by a "have-<field>" boolean value */
  n_vals = n_fields;
  for (i = 0; i < n_fields; i++) {
    const GstStructure *meta =
        gst_trace_format_get_field_structure (self->format, i);
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    gst_structure_get (meta, "flags", GST_TYPE_TRACER_VALUE_FLAGS, &flags,
        NULL);
    if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL)
      n_vals++;
  }

  values = g_newa (GstTraceValue, n_vals);

  va_start (var_args, self);
  for (i = 0, vi = 0; i < n_fields; i++) {
    const GstStructure *meta =
        gst_trace_format_get_field_structure (self->format, i);
    GstTracerFieldType type = gst_trace_format_get_field_type (self->format, i);
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    gst_structure_get (meta, "flags", GST_TYPE_TRACER_VALUE_FLAGS, &flags,
        NULL);
    if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL)
      values[vi++].v_boolean = va_arg (var_args, gboolean);

    switch (type) {
      case GST_TRACER_FIELD_TYPE_BOOLEAN:
        values[vi++].v_boolean = va_arg (var_args, gboolean);
        break;
      case GST_TRACER_FIELD_TYPE_INT:
        values[vi++].v_int = va_arg (var_args, gint);
        break;
      case GST_TRACER_FIELD_TYPE_UINT:
        values[vi++].v_uint = va_arg (var_args, guint);
        break;
      case GST_TRACER_FIELD_TYPE_INT64:
        values[vi++].v_int64 = va_arg (var_args, gint64);
        break;
      case GST_TRACER_FIELD_TYPE_UINT64:
      case GST_TRACER_FIELD_TYPE_CLOCK_TIME:
        values[vi++].v_uint64 = va_arg (var_args, guint64);
        break;
      case GST_TRACER_FIELD_TYPE_DOUBLE:
        values[vi++].v_double = va_arg (var_args, gdouble);
        break;
      case GST_TRACER_FIELD_TYPE_STRING:
        values[vi++].v_string = va_arg (var_args, const gchar *);
        break;
      case GST_TRACER_FIELD_TYPE_STRUCTURE:
        values[vi++].v_structure = va_arg (var_args, GstStructure *);
        break;
      case GST_TRACER_FIELD_TYPE_OBJECT:
        values[vi++].v_object = va_arg (var_args, GObject *);
        break;
    }
  }
  va_end (var_args);

  gst_trace_event (self->format, values);
}
#endif

G_GNUC_END_IGNORE_DEPRECATIONS
