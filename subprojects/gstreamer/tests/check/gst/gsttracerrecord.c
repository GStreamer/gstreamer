/* GStreamer
 *
 * Unit tests for GstTracerRecord
 *
 * Copyright (C) 2016 Stefan Sauer <ensonic@users.sf.net>
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
#include <gst/gsttracerrecord.h>

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
/* The deprecated GstTracerRecord API now feeds gst_trace_event(), which is
 * delivered through the "event" tracer hook. The tests register a tracer on
 * that hook and check the (format, values) the record API forwards. */
    typedef struct
{
  GstTracer parent;
} GstRecordTestTracer;

typedef struct
{
  GstTracerClass parent_class;
} GstRecordTestTracerClass;

static GType gst_record_test_tracer_get_type (void);
G_DEFINE_TYPE (GstRecordTestTracer, gst_record_test_tracer, GST_TYPE_TRACER);
static void
gst_record_test_tracer_class_init (GstRecordTestTracerClass * klass)
{
}

static void
gst_record_test_tracer_init (GstRecordTestTracer * tracer)
{
}

static gint event_count;
static GstTraceFormat *last_format;
static GstTraceValue last_values[8];
static GstTracer *test_tracer;

static void
event_cb (GObject * tracer, GstClockTime ts, GstTraceFormat * format,
    const GstTraceValue * values)
{
  guint n_fields = gst_trace_format_get_n_fields (format), i, vi = 0;

  last_format = format;
  for (i = 0; i < n_fields; i++) {
    const GstStructure *meta = gst_trace_format_get_field_structure (format, i);
    GstTracerValueFlags flags = GST_TRACER_VALUE_FLAGS_NONE;

    gst_structure_get (meta, "flags", GST_TYPE_TRACER_VALUE_FLAGS, &flags,
        NULL);
    if (flags & GST_TRACER_VALUE_FLAGS_OPTIONAL) {
      last_values[vi] = values[vi];
      vi++;
    }
    last_values[vi] = values[vi];
    vi++;
  }
  event_count++;
}

static void
setup (void)
{
  event_count = 0;
  last_format = NULL;
  test_tracer = g_object_new (gst_record_test_tracer_get_type (), NULL);
  gst_object_ref_sink (test_tracer);
  gst_tracing_register_hook (test_tracer, "event", G_CALLBACK (event_cb));
}

static void
cleanup (void)
{
  gst_object_unref (test_tracer);
  test_tracer = NULL;
}


GST_START_TEST (serialize_message_logging)
{
  GstTracerRecord *tr;

  /* *INDENT-OFF* */
  tr = gst_tracer_record_new ("test.class",
      "string", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          NULL),
      NULL);
  /* *INDENT-ON* */

  gst_tracer_record_log (tr, "test");

  fail_unless_equals_int (event_count, 1);
  fail_unless_equals_string (gst_trace_format_get_name (last_format), "test");
  fail_unless_equals_int (gst_trace_format_get_n_fields (last_format), 1);
  fail_unless_equals_string (gst_trace_format_get_field_name (last_format, 0),
      "string");
  fail_unless_equals_int (gst_trace_format_get_field_type (last_format, 0),
      GST_TRACER_FIELD_TYPE_STRING);
  fail_unless_equals_string (last_values[0].v_string, "test");

  gst_object_unref (tr);
}

GST_END_TEST;


GST_START_TEST (serialize_static_record)
{
  GstTracerRecord *tr;

  /* *INDENT-OFF* */
  tr = gst_tracer_record_new ("test.class",
      "string", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          NULL),
      "int", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_INT,
          NULL),
      "bool", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_BOOLEAN,
          NULL),
      "enum", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, GST_TYPE_PAD_DIRECTION,
          NULL),
      NULL);
  /* *INDENT-ON* */

  gst_tracer_record_log (tr, "test", 1, TRUE, GST_PAD_SRC);

  fail_unless_equals_int (event_count, 1);
  fail_unless_equals_string (gst_trace_format_get_name (last_format), "test");
  fail_unless_equals_int (gst_trace_format_get_n_fields (last_format), 4);
  fail_unless_equals_int (gst_trace_format_get_field_type (last_format, 0),
      GST_TRACER_FIELD_TYPE_STRING);
  fail_unless_equals_int (gst_trace_format_get_field_type (last_format, 1),
      GST_TRACER_FIELD_TYPE_INT);
  fail_unless_equals_int (gst_trace_format_get_field_type (last_format, 2),
      GST_TRACER_FIELD_TYPE_BOOLEAN);
  /* enum field types map to GST_TRACER_FIELD_TYPE_INT */
  fail_unless_equals_int (gst_trace_format_get_field_type (last_format, 3),
      GST_TRACER_FIELD_TYPE_INT);
  fail_unless_equals_string (last_values[0].v_string, "test");
  fail_unless_equals_int (last_values[1].v_int, 1);
  fail_unless_equals_int (last_values[2].v_boolean, TRUE);
  fail_unless_equals_int (last_values[3].v_int, GST_PAD_SRC);

  gst_object_unref (tr);
}

GST_END_TEST;


static Suite *
gst_tracer_record_suite (void)
{
  Suite *s = suite_create ("GstTracerRecord");
  TCase *tc_chain = tcase_create ("record");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, cleanup);
  tcase_add_test (tc_chain, serialize_message_logging);
  tcase_add_test (tc_chain, serialize_static_record);

  /* FIXME: add more tests, e.g. enums, pointer types and optional fields */

  return s;
}

GST_CHECK_MAIN (gst_tracer_record);

G_GNUC_END_IGNORE_DEPRECATIONS
