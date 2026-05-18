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

static Suite *
gst_tracer_suite (void)
{
  Suite *s = suite_create ("GstTracer");
  TCase *tc_chain = tcase_create ("tracer");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, span_hooks);
  tcase_add_test (tc_chain, span_format_builder);

  return s;
}

GST_CHECK_MAIN (gst_tracer);
