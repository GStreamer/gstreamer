/* GStreamer
 *
 * Unit tests for GstInfo
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#include <string.h>

#ifndef GST_DISABLE_GST_DEBUG

static GList *messages;         /* NULL */
static gboolean save_messages;  /* FALSE */

static void
printf_extension_log_func (GstDebugCategory * category,
    GstDebugLevel level, const gchar * file, const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer unused)
{
  const gchar *dbg_msg;

  dbg_msg = gst_debug_message_get (message);
  fail_unless (dbg_msg != NULL);

  if (save_messages && g_str_equal (category->name, "check"))
    messages = g_list_append (messages, g_strdup (dbg_msg));

  /* g_print ("%s\n", dbg_msg); */

  /* quick hack to still get stuff to show if GST_DEBUG is set */
  if (g_getenv ("GST_DEBUG")) {
    gst_debug_log_default (category, level, file, function, line, object,
        message, unused);
  }
}

/* check our GST_PTR_FORMAT printf extension stuff */
GST_START_TEST (info_ptr_format_printf_extension)
{
  /* set up our own log function to make sure the code in gstinfo is actually
   * executed without GST_DEBUG being set or it being output to stdout */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (printf_extension_log_func, NULL, NULL);

  gst_debug_set_default_threshold (GST_LEVEL_LOG);

  /* NULL object */
  GST_LOG ("NULL: %" GST_PTR_FORMAT, (gpointer) NULL);

  /* structure */
  {
    GstStructure *s;

    s = gst_structure_new ("foo/bar", "number", G_TYPE_INT, 1,
        "string", G_TYPE_STRING, "s", "float-number", G_TYPE_DOUBLE,
        (gdouble) 424242.42, NULL);

    GST_LOG ("STRUCTURE: %" GST_PTR_FORMAT, s);
    gst_structure_free (s);
  }

  /* message */
  {
    GstMessage *msg;

    msg = gst_message_new_element (NULL,
        gst_structure_new ("redirect", "new-location", G_TYPE_STRING,
            "http://foobar.com/r0x0r.ogg", "minimum-bitrate", G_TYPE_INT,
            56000, NULL));

    GST_LOG ("MESSAGE: %" GST_PTR_FORMAT, msg);
    gst_message_unref (msg);
  }

  /* buffer and buffer list */
  {
    GstBufferList *list;
    GstBuffer *buf;

    buf = gst_buffer_new_allocate (NULL, 42, NULL);
    GST_BUFFER_PTS (buf) = 5 * GST_SECOND;
    GST_BUFFER_DURATION (buf) = GST_SECOND;
    GST_LOG ("BUFFER: %" GST_PTR_FORMAT, buf);

    list = gst_buffer_list_new ();
    gst_buffer_list_add (list, buf);
    buf = gst_buffer_new_allocate (NULL, 58, NULL);
    gst_buffer_list_add (list, buf);
    GST_LOG ("BUFFERLIST: %" GST_PTR_FORMAT, list);
    gst_buffer_list_unref (list);
  }

#if 0
  /* TODO: GObject */
  {
    GST_LOG ("GOBJECT: %" GST_PTR_FORMAT, obj);
  }

  /* TODO: GstObject */
  {
    GST_LOG ("GSTOBJECT: %" GST_PTR_FORMAT, obj);
  }

  /* TODO: GstPad */
  {
    GST_LOG ("PAD: %" GST_PTR_FORMAT, pad);
  }

  /* TODO: GstCaps */
  {
    GST_LOG ("PAD: %" GST_PTR_FORMAT, pad);
  }
#endif

  /* clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (printf_extension_log_func);
}

GST_END_TEST;

/* check our GST_SEGMENT_FORMAT printf extension stuff */
GST_START_TEST (info_segment_format_printf_extension)
{
  /* set up our own log function to make sure the code in gstinfo is actually
   * executed without GST_DEBUG being set or it being output to stdout */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (printf_extension_log_func, NULL, NULL);

  gst_debug_set_default_threshold (GST_LEVEL_LOG);

  /* TIME segment */
  {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_TIME);

    segment.rate = 1.0;
    segment.applied_rate = 2.0;
    segment.start = 0;
    segment.stop = 5 * 60 * GST_SECOND;
    segment.time = 0;

    segment.position = 2 * GST_SECOND;
    segment.duration = 90 * 60 * GST_SECOND;

    GST_LOG ("TIME: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* BYTE segment */
  {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);

    segment.rate = 1.0;
    segment.applied_rate = 1.0;
    segment.start = 0;
    segment.stop = 9999999;
    segment.time = 0;

    GST_LOG ("BYTE: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* UNKNOWN format segment (format numbers are consecutive from 0) */
  {
    GstSegment segment;

    gst_segment_init (&segment, 98765432);

    segment.rate = 1.0;
    segment.applied_rate = 1.0;
    segment.start = 0;
    segment.stop = 987654321;
    segment.time = 0;

    GST_LOG ("UNKNOWN: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* UNDEFINED format segment */
  {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_UNDEFINED);

    GST_LOG ("UNDEFINED: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* NULL segment */
  GST_LOG ("NULL: %" GST_SEGMENT_FORMAT, (GstSegment *) NULL);

  /* clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (printf_extension_log_func);
}

GST_END_TEST;

GST_START_TEST (info_log_handler)
{
  guint removed;

  removed = gst_debug_remove_log_function (gst_debug_log_default);
  fail_unless (removed == 1);
}

GST_END_TEST;

GST_START_TEST (info_dump_mem)
{
  GstDebugCategory *cat = NULL;
  GstElement *e;

  const guint8 data[] = { 0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70,
    0x71, 0x74, 0x20, 0x20, 0x20, 0x05, 0x03, 0x00, 0x71, 0x74, 0x20, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xef, 0xe1, 0x6d, 0x6f, 0x6f, 0x76, 0x00, 0x00, 0x00, 0x6c,
    0x6d, 0x76, 0x68, 0x64, 0x00, 0x00, 0x00, 0x00, 0xbf, 0xd1, 0x00, 0x1d,
    0xbf, 0xd1, 0x00, 0x1e, 0x00, 0x00, 0x0b, 0xb5, 0x00, 0x04, 0x59, 0xc5,
    0x00, 0x01, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, '%', 's', '%', 's'
  };

  e = gst_pipeline_new ("pipeline");
  GST_DEBUG_CATEGORY_INIT (cat, "dumpcat", 0, "data dump debug category");
  GST_MEMDUMP ("quicktime header", data, sizeof (data));
  GST_MEMDUMP (NULL, data, sizeof (data));
  GST_CAT_MEMDUMP (cat, "quicktime header", data, sizeof (data));
  GST_MEMDUMP_OBJECT (e, "object stuff", data, sizeof (data));
  GST_CAT_MEMDUMP_OBJECT (cat, e, "object/cat stuff", data, sizeof (data));
  gst_object_unref (e);
}

GST_END_TEST;

GST_START_TEST (info_fixme)
{
  GstDebugCategory *cat = NULL;
  GstElement *e;

  e = gst_pipeline_new ("pipeline");
  GST_DEBUG_CATEGORY_INIT (cat, "fixcat", 0, "FIXME debug category");
  GST_FIXME ("fix %s thing", "this");
  GST_FIXME_OBJECT (e, "fix %s object", "this");
  GST_CAT_FIXME (cat, "fix some%s in this category", "thing");
  GST_CAT_FIXME_OBJECT (cat, e, "fix some%s in this cat and object", "thing");
  gst_object_unref (e);
}

GST_END_TEST;

/* need this indirection so the compiler doesn't check the printf format
 * like it would if we used GST_INFO directly (it would complain) */
static void
call_GST_INFO (const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_debug_log_valist (GST_CAT_DEFAULT, GST_LEVEL_INFO, __FILE__, GST_FUNCTION,
      __LINE__, NULL, format, var_args);
  va_end (var_args);
}

GST_START_TEST (info_old_printf_extensions)
{
  GstSegment segment;
  GstCaps *caps;
  gchar *str;

  /* set up our own log function to make sure the code in gstinfo is actually
   * executed without GST_DEBUG being set or it being output to stdout */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (printf_extension_log_func, NULL, NULL);

  gst_debug_set_default_threshold (GST_LEVEL_LOG);

  save_messages = TRUE;

  fail_unless (messages == NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  caps = gst_caps_new_simple ("foo/bar", "width", G_TYPE_INT, 4096,
      "framerate", GST_TYPE_FRACTION, 50, 1, "format", G_TYPE_STRING, "ARGB",
      NULL);
  call_GST_INFO ("Segment %Q, caps are %P", &segment, caps);
  gst_caps_unref (caps);

  fail_unless_equals_int (g_list_length (messages), 1);
  str = (gchar *) messages->data;
  fail_unless (str != NULL);

  GST_INFO ("str = '%s'", str);

  fail_unless (strstr (str, "time") != NULL);
  fail_unless (strstr (str, "start=0:00:00.000000000") != NULL);
  fail_unless (strstr (str, "stop=99:99:99.999999999") != NULL);
  fail_unless (strstr (str, "applied_rate=1.000000") != NULL);

  fail_unless (strstr (str, " caps are ") != NULL);
  fail_unless (strstr (str, "foo/bar") != NULL);
  fail_unless (strstr (str, "width=(int)4096") != NULL);
  fail_unless (strstr (str, "framerate=(fraction)50/1") != NULL);
  fail_unless (strstr (str, "ARGB") != NULL);

  /* clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (printf_extension_log_func);
  save_messages = FALSE;
  g_list_free_full (messages, (GDestroyNotify) g_free);
  messages = NULL;
}

GST_END_TEST;

GST_START_TEST (info_register_same_debug_category_twice)
{
  GstDebugCategory *cat1 = NULL, *cat2 = NULL;

  GST_DEBUG_CATEGORY_INIT (cat1, "dupli-cat", 0, "Going once");
  GST_DEBUG_CATEGORY_INIT (cat2, "dupli-cat", 0, "Going twice");

  fail_unless_equals_pointer (cat1, cat2);

  fail_unless_equals_string (gst_debug_category_get_name (cat1), "dupli-cat");
  fail_unless_equals_string (gst_debug_category_get_description (cat1),
      "Going once");
}

GST_END_TEST;

GST_START_TEST (info_set_and_unset_single)
{
  GstDebugLevel orig = gst_debug_get_default_threshold ();
  GstDebugLevel cat1, cat2;
  GstDebugCategory *states;

  GST_DEBUG_CATEGORY_GET (states, "GST_STATES");
  fail_unless (states != NULL);

  gst_debug_set_default_threshold (GST_LEVEL_WARNING);

  gst_debug_set_threshold_for_name ("GST_STATES", GST_LEVEL_DEBUG);
  cat1 = gst_debug_category_get_threshold (states);
  gst_debug_unset_threshold_for_name ("GST_STATES");
  cat2 = gst_debug_category_get_threshold (states);

  gst_debug_set_default_threshold (orig);
  fail_unless (cat1 = GST_LEVEL_DEBUG);
  fail_unless (cat2 = GST_LEVEL_WARNING);
}

GST_END_TEST;

GST_START_TEST (info_set_and_unset_multiple)
{
  GstDebugLevel orig = gst_debug_get_default_threshold ();
  GstDebugLevel cat1, cat2, cat3;
  GstDebugCategory *states;
  GstDebugCategory *caps;

  GST_DEBUG_CATEGORY_GET (states, "GST_STATES");
  GST_DEBUG_CATEGORY_GET (caps, "GST_CAPS");
  fail_unless (states != NULL);
  fail_unless (caps != NULL);

  gst_debug_set_default_threshold (GST_LEVEL_WARNING);

  gst_debug_set_threshold_for_name ("GST_STATES", GST_LEVEL_DEBUG);
  gst_debug_set_threshold_for_name ("GST_CAPS", GST_LEVEL_DEBUG);
  cat1 = gst_debug_category_get_threshold (states);
  gst_debug_unset_threshold_for_name ("GST_STATES");
  gst_debug_unset_threshold_for_name ("GST_CAPS");
  cat2 = gst_debug_category_get_threshold (states);
  cat3 = gst_debug_category_get_threshold (caps);

  gst_debug_set_default_threshold (orig);

  fail_unless (cat1 = GST_LEVEL_DEBUG);
  fail_unless (cat2 = GST_LEVEL_WARNING);
  fail_unless (cat3 = GST_LEVEL_WARNING);
}

GST_END_TEST;
#endif

GST_START_TEST (info_fourcc)
{
  gchar *res;
  const gchar *cmp;

  cmp = "abcd";
  res = g_strdup_printf ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (0x64636261));
  fail_unless_equals_string (res, cmp);
  g_free (res);

  cmp = ".bcd";
  res = g_strdup_printf ("%" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (0x646362a9));
  fail_unless_equals_string (res, cmp);
  g_free (res);
}

GST_END_TEST;

static Suite *
gst_info_suite (void)
{
  Suite *s = suite_create ("GstInfo");
  TCase *tc_chain = tcase_create ("info");

  tcase_set_timeout (tc_chain, 30);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, info_fourcc);
#ifndef GST_DISABLE_GST_DEBUG
  tcase_add_test (tc_chain, info_segment_format_printf_extension);
  tcase_add_test (tc_chain, info_ptr_format_printf_extension);
  tcase_add_test (tc_chain, info_log_handler);
  tcase_add_test (tc_chain, info_dump_mem);
  tcase_add_test (tc_chain, info_fixme);
  tcase_add_test (tc_chain, info_old_printf_extensions);
  tcase_add_test (tc_chain, info_register_same_debug_category_twice);
  tcase_add_test (tc_chain, info_set_and_unset_single);
  tcase_add_test (tc_chain, info_set_and_unset_multiple);
#endif

  return s;
}

GST_CHECK_MAIN (gst_info);
