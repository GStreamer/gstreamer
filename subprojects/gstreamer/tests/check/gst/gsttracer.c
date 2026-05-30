/* GStreamer
 *
 * Unit tests for GstTracer
 *
 * Copyright (C) 2026 Thibault Saunier <tsaunier@igalia.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/gsttracer.h>

typedef struct _GstTestTracer
{
  GstTracer parent;
} GstTestTracer;

typedef struct _GstTestTracerClass
{
  GstTracerClass parent_class;
} GstTestTracerClass;

GType gst_test_tracer_get_type (void);

G_DEFINE_TYPE (GstTestTracer, gst_test_tracer, GST_TYPE_TRACER);

static GstTraceFormat *expected_format;
static GstTraceSpanId last_span_id;
static gint begin_count;
static gint end_count;

static gint event_count;
static GstTraceFormat *last_event_format;
static GstTraceValue last_event_values[8];

/* Captures the raw (format, values) delivered by the "event" hook, expanding
 * the optional have-<field> booleans the same way callers pass them. */
static void
event_cb (GObject * tracer, GstClockTime ts, GstTraceFormat * format,
    const GstTraceValue * values)
{
  guint n_fields = gst_trace_format_get_n_fields (format), i, vi = 0;

  fail_unless (GST_IS_TRACER (tracer));
  fail_unless (values != NULL);

  last_event_format = format;
  for (i = 0; i < n_fields; i++) {
    const GstStructure *meta = gst_trace_format_get_field_structure (format, i);
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    gst_structure_get (meta, "flags", GST_TYPE_TRACER_VALUE_FLAGS, &flags,
        NULL);
    if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL) {
      last_event_values[vi] = values[vi];
      vi++;
    }
    last_event_values[vi] = values[vi];
    vi++;
  }
  event_count++;
}

static void
gst_test_tracer_class_init (GstTestTracerClass * klass)
{
}

static void
gst_test_tracer_init (GstTestTracer * tracer)
{
}

static void
span_begin_cb (GObject * tracer, GstClockTime ts,
    GstTraceSpanId span_id, GstTraceFormat * format,
    const GstTraceValue * values)
{
  fail_unless (GST_IS_TRACER (tracer));
  fail_unless (span_id != GST_TRACE_SPAN_ID_NONE);
  fail_unless_equals_pointer (format, expected_format);
  fail_unless (values != NULL);
  fail_unless_equals_string (values[0].v_string, "value");
  fail_unless_equals_uint64 (values[1].v_uint64, 42);

  fail_unless_equals_string (gst_trace_format_get_name (format), "test_span");
  fail_unless_equals_int (gst_trace_format_get_n_fields (format), 2);
  fail_unless_equals_string (gst_trace_format_get_field_name (format, 0),
      "name");
  fail_unless_equals_int (gst_trace_format_get_field_type (format, 0),
      GST_TRACER_FIELD_TYPE_STRING);
  fail_unless_equals_string (gst_trace_format_get_field_name (format, 1),
      "count");
  fail_unless_equals_int (gst_trace_format_get_field_type (format, 1),
      GST_TRACER_FIELD_TYPE_UINT64);

  last_span_id = span_id;
  begin_count++;
}

static void
span_end_cb (GObject * tracer, GstClockTime ts, GstTraceSpanId span_id)
{
  fail_unless (GST_IS_TRACER (tracer));
  fail_unless_equals_uint64 (span_id, last_span_id);

  end_count++;
}

GST_DEFINE_TRACE_FORMAT (test_span, "name", STRING, "count", UINT64)
    GST_START_TEST (span_hooks)
{
  GstTraceSpanId span_id;
  GstTracer *tracer;

  begin_count = 0;
  end_count = 0;
  last_span_id = GST_TRACE_SPAN_ID_NONE;

  expected_format = test_span ();
  fail_unless (expected_format != NULL);
  fail_unless_equals_string (gst_trace_format_get_name (expected_format),
      "test_span");
  fail_unless_equals_int (gst_trace_format_get_n_fields (expected_format), 2);
  fail_if (gst_trace_format_is_enabled (expected_format));

  /* Second call returns the cached format. */
  fail_unless_equals_pointer (test_span (), expected_format);

  span_id =
      gst_trace_span_begin (expected_format,
      GST_TRACE_VALUES (STRING ("value"), UINT64 (42)));
  fail_unless_equals_uint64 (span_id, GST_TRACE_SPAN_ID_NONE);
  gst_trace_span_end (span_id);

  tracer = g_object_new (gst_test_tracer_get_type (), NULL);
  gst_object_ref_sink (tracer);
  gst_tracing_register_hook (tracer, "span-begin", G_CALLBACK (span_begin_cb));
  gst_tracing_register_hook (tracer, "span-end", G_CALLBACK (span_end_cb));
  gst_object_unref (tracer);

  fail_unless (gst_trace_format_is_enabled (expected_format));

  span_id =
      gst_trace_span_begin (expected_format,
      GST_TRACE_VALUES (STRING ("value"), UINT64 (42)));
  fail_unless (span_id != GST_TRACE_SPAN_ID_NONE);
  fail_unless_equals_int (begin_count, 1);
  fail_unless_equals_uint64 (last_span_id, span_id);

  gst_trace_span_end_and_clear (&span_id);
  fail_unless_equals_int (end_count, 1);
  fail_unless_equals_uint64 (span_id, GST_TRACE_SPAN_ID_NONE);

  /* end_and_clear on an already-cleared slot is a no-op. */
  gst_trace_span_end_and_clear (&span_id);
  fail_unless_equals_int (end_count, 1);

  /* GST_TRACE_SCOPE_BEGIN/END manage a format-named variable (portable). */
  {
    GST_TRACE_SCOPE_BEGIN (test_span, STRING ("value"), UINT64 (42));
    fail_unless_equals_int (begin_count, 2);
    GST_TRACE_SCOPE_END (test_span);
    fail_unless_equals_int (end_count, 2);
  }

#ifdef _GST_TRACE_FUNC_HAS_CLEANUP
  /* GST_TRACE_FUNC opens a span that auto-closes on scope exit. */
  {
    GST_TRACE_FUNC (test_span, STRING ("value"), UINT64 (42));
    fail_unless_equals_int (begin_count, 3);
  }
  fail_unless_equals_int (end_count, 3);
#endif
}

GST_END_TEST;

GST_START_TEST (span_format_builder)
{
  GstTraceFormatBuilder *builder;
  GstTraceFormat *format;
  const GstStructure *field;

  builder = gst_trace_format_builder_new ("builder-span");
  fail_unless (builder != NULL);

  gst_trace_format_builder_set_description (builder, "Builder test");
  gst_trace_format_builder_add_field (builder, "label",
      GST_TRACER_FIELD_TYPE_STRING);
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_description (gst_trace_field_new ("count",
              GST_TRACER_FIELD_TYPE_INT64), "How many items"));
  format = gst_trace_format_builder_register (builder);

  fail_unless (format != NULL);
  fail_unless_equals_string (gst_trace_format_get_name (format),
      "builder-span");
  fail_unless_equals_string (gst_trace_format_get_description (format),
      "Builder test");
  fail_unless_equals_int (gst_trace_format_get_n_fields (format), 2);

  /* label: no description */
  fail_unless_equals_string (gst_trace_format_get_field_name (format, 0),
      "label");
  fail_unless_equals_int (gst_trace_format_get_field_type (format, 0),
      GST_TRACER_FIELD_TYPE_STRING);
  fail_unless (gst_trace_format_get_field_description (format, 0) == NULL);

  /* count: type + description */
  fail_unless_equals_string (gst_trace_format_get_field_name (format, 1),
      "count");
  fail_unless_equals_int (gst_trace_format_get_field_type (format, 1),
      GST_TRACER_FIELD_TYPE_INT64);
  fail_unless_equals_string (gst_trace_format_get_field_description
      (format, 1), "How many items");

  /* Raw field structure accessor exposes the same data. */
  field = gst_trace_format_get_field_structure (format, 1);
  fail_unless (field != NULL);
  fail_unless_equals_string (gst_structure_get_string (field, "description"),
      "How many items");
}

GST_END_TEST;

GST_DEFINE_TRACE_FORMAT (test_event,
    "name", STRING, "count", UINT64, "flag", BOOLEAN)
    GST_START_TEST (custom_event)
{
  GstTracer *tracer;
  GstTraceFormat *format;

  format = test_event ();
  fail_unless (format != NULL);

  event_count = 0;
  last_event_format = NULL;

  /* The event is delivered through the "event" hook. */
  tracer = g_object_new (gst_test_tracer_get_type (), NULL);
  gst_object_ref_sink (tracer);
  gst_tracing_register_hook (tracer, "event", G_CALLBACK (event_cb));
  gst_object_unref (tracer);
  fail_unless (gst_trace_format_is_enabled (format));

  gst_trace_event (format, GST_TRACE_VALUES (STRING ("value"),
          UINT64 (42), BOOLEAN (TRUE)));

  fail_unless_equals_int (event_count, 1);
  fail_unless_equals_pointer (last_event_format, format);
  fail_unless_equals_string (gst_trace_format_get_name (last_event_format),
      "test_event");
  fail_unless_equals_string (last_event_values[0].v_string, "value");
  fail_unless_equals_uint64 (last_event_values[1].v_uint64, 42);
  fail_unless (last_event_values[2].v_boolean == TRUE);
}

GST_END_TEST;

GST_START_TEST (custom_event_optional)
{
  GstTracer *tracer;
  GstTraceFormatBuilder *builder;
  GstTraceFormat *format;
  const GstStructure *field;
  GstTracerValueScope scope;

  builder = gst_trace_format_builder_new ("opt_test");
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_scope (gst_trace_field_new ("id",
              GST_TRACER_FIELD_TYPE_UINT), GST_TRACER_VALUE_SCOPE_PROCESS));
  gst_trace_format_builder_add_field_full (builder,
      gst_trace_field_set_flags (gst_trace_field_new ("extra",
              GST_TRACER_FIELD_TYPE_INT64), GST_TRACER_VALUE_FLAGS_OPTIONAL));
  format = gst_trace_format_builder_register (builder);

  /* scope metadata round-trips */
  field = gst_trace_format_get_field_structure (format, 0);
  fail_unless (gst_structure_get_enum (field, "scope",
          GST_TYPE_TRACER_VALUE_SCOPE, (gint *) & scope));
  fail_unless_equals_int (scope, GST_TRACER_VALUE_SCOPE_PROCESS);

  event_count = 0;
  last_event_format = NULL;

  tracer = g_object_new (gst_test_tracer_get_type (), NULL);
  gst_object_ref_sink (tracer);
  gst_tracing_register_hook (tracer, "event", G_CALLBACK (event_cb));
  gst_object_unref (tracer);

  /* id, then have-extra (TRUE) preceding the optional extra value */
  gst_trace_event (format, GST_TRACE_VALUES (UINT (7),
          BOOLEAN (TRUE), INT64 (123)));

  fail_unless_equals_int (event_count, 1);
  fail_unless_equals_pointer (last_event_format, format);
  fail_unless_equals_string (gst_trace_format_get_name (last_event_format),
      "opt_test");
  /* id, then the have-extra boolean preceding the optional extra value */
  fail_unless_equals_int (last_event_values[0].v_uint, 7);
  fail_unless (last_event_values[1].v_boolean == TRUE);
  fail_unless_equals_int (last_event_values[2].v_int64, 123);
}

GST_END_TEST;

static Suite *
gst_tracer_suite (void)
{
  Suite *s = suite_create ("GstTracer");
  TCase *tc_chain = tcase_create ("tracer");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, span_hooks);
  tcase_add_test (tc_chain, span_format_builder);
  tcase_add_test (tc_chain, custom_event);
  tcase_add_test (tc_chain, custom_event_optional);

  return s;
}

GST_CHECK_MAIN (gst_tracer);
