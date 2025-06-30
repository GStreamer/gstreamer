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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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

  gst_debug_set_threshold_from_string ("LOG", TRUE);

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

  gst_debug_set_threshold_from_string ("LOG", TRUE);

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
  fail_unless_equals_int (removed, 1);
}

GST_END_TEST;

static gboolean log_found_match = FALSE;

static void
compare_gst_log_func (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line, GObject * object,
    GstDebugMessage * message, gpointer user_data)
{
  gboolean match;
  gchar *log_line;

  fail_unless_equals_pointer (user_data, NULL);

  log_line = gst_debug_log_get_line (category, level, file, function, line,
      object, message);

  match = g_pattern_match_simple ("*:*:*.*0*DEBUG*check*gstinfo.c:*"
      ":info_log_handler_get_line: test message\n", log_line);

  if (match)
    log_found_match = TRUE;

  g_free (log_line);
}

GST_START_TEST (info_log_handler_get_line)
{
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (compare_gst_log_func, NULL, NULL);

  gst_debug_set_threshold_from_string ("LOG", TRUE);
  GST_DEBUG ("test message");

  fail_unless (log_found_match == TRUE);

  /* clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (compare_gst_log_func);
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

  gst_debug_set_threshold_from_string ("LOG", TRUE);

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
  GstDebugLevel cat1, cat2;
  GstDebugCategory *states;

  GST_DEBUG_CATEGORY_GET (states, "GST_STATES");
  fail_unless (states != NULL);

  gst_debug_set_threshold_from_string ("WARNING", TRUE);

  gst_debug_set_threshold_for_name ("GST_STATES", GST_LEVEL_DEBUG);
  cat1 = gst_debug_category_get_threshold (states);
  gst_debug_unset_threshold_for_name ("GST_STATES");
  cat2 = gst_debug_category_get_threshold (states);

  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  fail_unless_equals_int (cat1, GST_LEVEL_DEBUG);
  fail_unless_equals_int (cat2, GST_LEVEL_WARNING);
}

GST_END_TEST;

GST_START_TEST (info_set_and_unset_multiple)
{
  GstDebugLevel cat1, cat2, cat3;
  GstDebugCategory *states;
  GstDebugCategory *caps;

  GST_DEBUG_CATEGORY_GET (states, "GST_STATES");
  GST_DEBUG_CATEGORY_GET (caps, "GST_CAPS");
  fail_unless (states != NULL);
  fail_unless (caps != NULL);

  gst_debug_set_threshold_from_string ("WARNING", TRUE);

  gst_debug_set_threshold_for_name ("GST_STATES", GST_LEVEL_DEBUG);
  gst_debug_set_threshold_for_name ("GST_CAPS", GST_LEVEL_DEBUG);
  cat1 = gst_debug_category_get_threshold (states);
  gst_debug_unset_threshold_for_name ("GST_STATES");
  gst_debug_unset_threshold_for_name ("GST_CAPS");
  cat2 = gst_debug_category_get_threshold (states);
  cat3 = gst_debug_category_get_threshold (caps);

  gst_debug_set_default_threshold (GST_LEVEL_NONE);

  fail_unless_equals_int (cat1, GST_LEVEL_DEBUG);
  fail_unless_equals_int (cat2, GST_LEVEL_WARNING);
  fail_unless_equals_int (cat3, GST_LEVEL_WARNING);
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

/* Here we're testing adding debug categories after gst_init() and making
 * sure that this doesn't incur exponential costs. Previously this would
 * reparse the debug string and re-add the parsed category/levels to the
 * list, thus doubling the list to pattern match a category against for
 * every category added. And we would also re-evaluate all existing categories
 * against that list. This test makes sure the overhead of registering debug
 * categories late is very small. This test would time out before the fix. */
GST_START_TEST (info_post_gst_init_category_registration)
{
  GstDebugCategory *cats[10000] = { NULL, };
  guint i;

  /* Note: before the fixes this wouldn't work to trigger the problem because
   * only a pattern set via GST_DEBUG before gst_init would be picked up
   * (another bug) */
  gst_debug_set_threshold_from_string ("*a*b:6,*b*0:6,*c:3,d*:2,xyz*:9,ax:1",
      TRUE);

  fail_unless_equals_int (GST_LEVEL_DEFAULT,
      gst_debug_get_default_threshold ());

  for (i = 0; i < G_N_ELEMENTS (cats); ++i) {
    gchar *name = g_strdup_printf ("%s-%x", (i % 2 == 0) ? "cat" : "dog", i);
    GST_DEBUG_CATEGORY_INIT (cats[i], name, 0, "none");
    g_free (name);
  }

  /* none */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[0]),
      GST_LEVEL_DEFAULT);
  /* d*:2 */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[1]),
      GST_LEVEL_WARNING);
  /* none */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[2]),
      GST_LEVEL_DEFAULT);
  /* d*:2 */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[3]),
      GST_LEVEL_WARNING);
  /* *c:3 */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[0xc]),
      GST_LEVEL_FIXME);
  /* *c:3 */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[0x4c]),
      GST_LEVEL_FIXME);
  /* *a*b:6 and d*:2, but d*:2 takes priority here as cat name is "dog-a1b"
   * and order matters: items listed later override earlier ones. */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[0xa1b]),
      GST_LEVEL_WARNING);
  /* *a*0:6 */
  fail_unless_equals_int (gst_debug_category_get_threshold (cats[0xb10]),
      GST_LEVEL_LOG);
}

GST_END_TEST;

GST_START_TEST (info_set_and_reset_string)
{
  GstDebugCategory *states = NULL;
  GstDebugCategory *caps = NULL;
  GstDebugLevel cat;

  GST_DEBUG_CATEGORY_GET (states, "GST_STATES");
  GST_DEBUG_CATEGORY_GET (caps, "GST_CAPS");
  fail_unless (states != NULL);
  fail_unless (caps != NULL);

  gst_debug_set_threshold_from_string ("WARNING,GST_CAPS:DEBUG", TRUE);
  cat = gst_debug_category_get_threshold (states);
  fail_unless_equals_int (cat, GST_LEVEL_WARNING);
  cat = gst_debug_category_get_threshold (caps);
  fail_unless_equals_int (cat, GST_LEVEL_DEBUG);

  gst_debug_set_threshold_from_string ("GST_STATES:TRACE", FALSE);
  cat = gst_debug_category_get_threshold (states);
  fail_unless_equals_int (cat, GST_LEVEL_TRACE);
  cat = gst_debug_category_get_threshold (caps);
  fail_unless_equals_int (cat, GST_LEVEL_DEBUG);

  gst_debug_set_threshold_from_string ("INFO,GST_CAPS:FIXME", FALSE);
  cat = gst_debug_category_get_threshold (states);
  fail_unless_equals_int (cat, GST_LEVEL_TRACE);
  cat = gst_debug_category_get_threshold (caps);
  fail_unless_equals_int (cat, GST_LEVEL_FIXME);

  gst_debug_set_threshold_from_string ("INFO,GST_CAPS:FIXME", TRUE);
  cat = gst_debug_category_get_threshold (states);
  fail_unless_equals_int (cat, GST_LEVEL_INFO);
  cat = gst_debug_category_get_threshold (caps);
  fail_unless_equals_int (cat, GST_LEVEL_FIXME);

  gst_debug_set_threshold_from_string ("", TRUE);
  cat = gst_debug_category_get_threshold (states);
  fail_unless_equals_int (cat, GST_LEVEL_DEFAULT);
  cat = gst_debug_category_get_threshold (caps);
  fail_unless_equals_int (cat, GST_LEVEL_DEFAULT);
}

GST_END_TEST;

static gint context_log_count = 0;

static void
context_log_counter_func (GstDebugCategory * category,
    GstDebugLevel level, const gchar * file, const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer user_data)
{
  /* Track the number of messages received */
  context_log_count++;

  /* Let the default log function handle it for output if needed */
  if (g_getenv ("GST_DEBUG")) {
    gst_debug_log_default (category, level, file, function, line, object,
        message, NULL);
  }
}

GST_START_TEST (info_context_log)
{
  GstDebugCategory *cat = NULL;
  GstLogContext *ctx = NULL;

  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (context_log_counter_func, NULL, NULL);
  gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
  GST_DEBUG_CATEGORY_INIT (cat, "contextcat", 0, "Log context test category");

  GST_LOG_CONTEXT_INIT (ctx, GST_LOG_CONTEXT_FLAG_THROTTLE);
  context_log_count = 0;
  /* Test all the different logging macros with context and verify the log level is respected */
  GST_CTX_ERROR (ctx, "Error message with context");
  GST_CTX_WARNING (ctx, "Warning message with context");
  GST_CTX_FIXME (ctx, "Fixme message with context");
  GST_CTX_INFO (ctx, "Info message with context");
  GST_CTX_DEBUG (ctx, "Debug message with context");
  GST_CTX_LOG (ctx, "Log message with context");
  GST_CTX_TRACE (ctx, "Trace message with context");
  /* Since trace and log are above our threshold, it won't be counted */
  fail_unless_equals_int (context_log_count, 5);

  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (context_log_counter_func);
  gst_log_context_free (ctx);
}

GST_END_TEST;

GST_START_TEST (info_context_log_once)
{
  GstDebugCategory *cat = NULL;
  GstLogContext *ctx = NULL;

  /* Set up our counting log function */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (context_log_counter_func, NULL, NULL);

  /* Enable debug logging to ensure our logs get processed */
  gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
  GST_DEBUG_CATEGORY_INIT (cat, "contextcat", 0, "Log context test category");
  GST_LOG_CONTEXT_INIT (ctx, GST_LOG_CONTEXT_FLAG_THROTTLE);

  context_log_count = 0;

  /* Log the same message multiple times */
  GST_CTX_DEBUG (ctx, "This message should only appear once");
  GST_CTX_DEBUG (ctx, "This message should only appear once");
  GST_CTX_DEBUG (ctx, "This message should only appear once");

  /* Different messages should appear */
  GST_CTX_DEBUG (ctx, "A different message");
  GST_CTX_DEBUG (ctx, "Another different message");

  /* Should see 3 messages total */
  fail_unless_equals_int (context_log_count, 3);

  /* Clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (context_log_counter_func);
  gst_log_context_free (ctx);
}

GST_END_TEST;

GST_START_TEST (info_context_log_periodic)
{
  GstDebugCategory *cat = NULL;
  GstLogContext *ctx = NULL;

  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (context_log_counter_func, NULL, NULL);
  gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
  GST_DEBUG_CATEGORY_INIT (cat, "contextcat", 0, "Log context test category");

  GST_LOG_CONTEXT_INIT (ctx, GST_LOG_CONTEXT_FLAG_THROTTLE, {
        GST_LOG_CONTEXT_BUILDER_SET_INTERVAL (10 * GST_MSECOND);
      }
  );

  /* Reset the counter */
  context_log_count = 0;
  GST_CTX_DEBUG (ctx, "This message should appear the first time");
  GST_CTX_DEBUG (ctx, "This message should appear the first time");
  GST_CTX_DEBUG (ctx, "This message should appear the first time");

  /* Should see the message only once, unless it took more than 10ms to print 3
   * debug message ... */
  fail_unless_equals_int (context_log_count, 1);

  /* Sleep to ensure the reset interval passes */
  g_usleep (20000);             /* 20ms */

  /* Log the same message again - it should appear after the interval */
  GST_CTX_DEBUG (ctx, "This message should appear the first time");

  /* Should see both messages now */
  fail_unless_equals_int (context_log_count, 2);

  /* Clean up */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (context_log_counter_func);
  gst_log_context_free (ctx);
}

GST_END_TEST;

/* Test the static context macros */
GST_LOG_CONTEXT_STATIC_DEFINE (static_ctx, GST_LOG_CONTEXT_FLAG_THROTTLE);
#define STATIC_CTX GST_LOG_CONTEXT_LAZY_INIT(static_ctx)
GST_LOG_CONTEXT_STATIC_DEFINE (static_periodic_ctx,
    GST_LOG_CONTEXT_FLAG_THROTTLE, GST_LOG_CONTEXT_BUILDER_SET_INTERVAL (1);
    );
#define STATIC_PERIODIC_CTX GST_LOG_CONTEXT_LAZY_INIT(static_periodic_ctx)

GST_START_TEST (info_context_log_static)
{
  GstDebugCategory *cat = NULL;

  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (context_log_counter_func, NULL, NULL);
  gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
  GST_DEBUG_CATEGORY_INIT (cat, "contextcat", 0, "Log context test category");

  context_log_count = 0;

  GST_CTX_DEBUG (STATIC_CTX, "Static context message");
  GST_CTX_DEBUG (STATIC_CTX, "Static context default category message");
  fail_unless_equals_int (context_log_count, 2);

  context_log_count = 0;
  GST_CTX_DEBUG (STATIC_PERIODIC_CTX, "Static periodic context message");
  fail_unless_equals_int (context_log_count, 1);

  /* Sleep to ensure the reset interval passes */
  g_usleep (2000);              /* 2ms */
  GST_CTX_DEBUG (STATIC_PERIODIC_CTX, "Static periodic context message");
  fail_unless_equals_int (context_log_count, 2);

  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (context_log_counter_func);
}

GST_END_TEST;

GST_START_TEST (info_context_log_flags)
{
  GstDebugCategory *cat = NULL;
  GstElement *element;
  GstLogContext *ctx1 = NULL, *ctx2 = NULL, *ctx3 = NULL;

  /* Set up our counting log function */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (context_log_counter_func, NULL, NULL);

  /* Enable debug logging to ensure our logs get processed */
  gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
  GST_DEBUG_CATEGORY_INIT (cat, "contextcat", 0, "Log context test category");

  /* Create an element for object-based logging */
  element = gst_element_factory_make ("identity", NULL);
  fail_unless (element != NULL);

  /* Test DEFAULT context */
  GST_LOG_CONTEXT_INIT (ctx1, GST_LOG_CONTEXT_FLAG_THROTTLE);
  context_log_count = 0;
  GST_CTX_DEBUG_OBJECT (ctx1, element, "Test message with default context");
  GST_CTX_DEBUG_OBJECT (ctx1, NULL, "Test message with default context");
  /* Should see both messages since objects are different */
  fail_unless_equals_int (context_log_count, 2);

  /* Test IGNORE_OBJECT context */
  GST_LOG_CONTEXT_INIT (ctx2, GST_LOG_CONTEXT_FLAG_THROTTLE, {
        GST_LOG_CONTEXT_BUILDER_SET_HASH_FLAGS (GST_LOG_CONTEXT_IGNORE_OBJECT);
      }
  );
  context_log_count = 0;
  GST_CTX_DEBUG_OBJECT (ctx2, element,
      "Test message with ignore object context");
  GST_CTX_DEBUG_OBJECT (ctx2, NULL, "Test message with ignore object context");
  /* Should see only one message since objects are ignored in hash calculation */
  fail_unless_equals_int (context_log_count, 1);

  /* Test USE_LINE_NUMBER context */
  GST_LOG_CONTEXT_INIT (ctx3, GST_LOG_CONTEXT_FLAG_THROTTLE, {
        GST_LOG_CONTEXT_BUILDER_SET_HASH_FLAGS
        (GST_LOG_CONTEXT_USE_LINE_NUMBER);
      }
  );
  context_log_count = 0;
  GST_CTX_DEBUG (ctx3, "Test message with line context");
  GST_CTX_DEBUG (ctx3, "Test message with line context");
  /* Should see the 2 messages since line numbers are taken into account */
  fail_unless_equals_int (context_log_count, 2);

  gst_object_unref (element);
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
  gst_debug_add_log_function (gst_debug_log_default, NULL, NULL);
  gst_debug_remove_log_function (context_log_counter_func);
  gst_log_context_free (ctx1);
  gst_log_context_free (ctx2);
  gst_log_context_free (ctx3);
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
  tcase_add_test (tc_chain, info_log_handler_get_line);
  tcase_add_test (tc_chain, info_dump_mem);
  tcase_add_test (tc_chain, info_fixme);
  tcase_add_test (tc_chain, info_old_printf_extensions);
  tcase_add_test (tc_chain, info_register_same_debug_category_twice);
  tcase_add_test (tc_chain, info_set_and_unset_single);
  tcase_add_test (tc_chain, info_set_and_unset_multiple);
  tcase_add_test (tc_chain, info_post_gst_init_category_registration);
  tcase_add_test (tc_chain, info_set_and_reset_string);

  tcase_add_test (tc_chain, info_context_log);
  tcase_add_test (tc_chain, info_context_log_once);
  tcase_add_test (tc_chain, info_context_log_periodic);
  tcase_add_test (tc_chain, info_context_log_static);
  tcase_add_test (tc_chain, info_context_log_flags);
#endif

  return s;
}

GST_CHECK_MAIN (gst_info);
