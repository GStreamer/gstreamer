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

#include <gst/check/gstcheck.h>
#include <gst/gsttracerrecord.h>

static GList *messages;         /* NULL */
static gboolean save_messages;  /* FALSE */

static void
tracer_log_func (GstDebugCategory * category,
    GstDebugLevel level, const gchar * file, const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer unused)
{
  const gchar *dbg_msg;

  if (!save_messages || level != GST_LEVEL_TRACE ||
      !g_str_equal (category->name, "GST_TRACER")) {
    return;
  }

  dbg_msg = gst_debug_message_get (message);
  fail_unless (dbg_msg != NULL);

  messages = g_list_append (messages, g_strdup (dbg_msg));
}

static void
setup (void)
{
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (tracer_log_func, NULL, NULL);
  gst_debug_set_threshold_for_name ("GST_TRACER", GST_LEVEL_TRACE);
  messages = NULL;
  save_messages = FALSE;
}

static void
cleanup (void)
{
  save_messages = FALSE;
  gst_debug_set_threshold_for_name ("GST_TRACER", GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (tracer_log_func);
  g_list_free_full (messages, (GDestroyNotify) g_free);
  messages = NULL;
}


GST_START_TEST (serialize_message_logging)
{
  GstTracerRecord *tr;
  gchar *str;

  /* *INDENT-OFF* */
  tr = gst_tracer_record_new ("test.class",
      "string", GST_TYPE_STRUCTURE, gst_structure_new ("value",
          "type", G_TYPE_GTYPE, G_TYPE_STRING,
          NULL),
      NULL);
  /* *INDENT-ON* */

  save_messages = TRUE;
  gst_tracer_record_log (tr, "test");
  save_messages = FALSE;

  fail_unless_equals_int (g_list_length (messages), 1);
  str = (gchar *) messages->data;
  fail_unless (str != NULL);

  g_object_unref (tr);
}

GST_END_TEST;


GST_START_TEST (serialize_static_record)
{
  GstTracerRecord *tr;
  GstStructure *s;
  gchar *str;
  gchar *str_val;
  gint int_val;
  gboolean bool_val;
  GstPadDirection enum_val;

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

  save_messages = TRUE;
  gst_tracer_record_log (tr, "test", 1, TRUE, GST_PAD_SRC);
  save_messages = FALSE;

  str = (gchar *) messages->data;
  GST_INFO ("serialized to '%s'", str);

  s = gst_structure_from_string (str, NULL);
  fail_unless (s != NULL);

  fail_unless_equals_string (gst_structure_get_name (s), "test");

  fail_unless (gst_structure_get (s,
          "string", G_TYPE_STRING, &str_val,
          "int", G_TYPE_INT, &int_val,
          "bool", G_TYPE_BOOLEAN, &bool_val,
          "enum", GST_TYPE_PAD_DIRECTION, &enum_val, NULL));
  fail_unless_equals_int (int_val, 1);
  fail_unless_equals_string (str_val, "test");
  fail_unless_equals_int (bool_val, TRUE);
  fail_unless_equals_int (enum_val, GST_PAD_SRC);
  g_free (str_val);

  gst_structure_free (s);
  g_object_unref (tr);
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
