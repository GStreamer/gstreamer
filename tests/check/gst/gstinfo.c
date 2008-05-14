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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/check/gstcheck.h>

#ifndef GST_DISABLE_GST_DEBUG

static void
printf_extension_log_func (GstDebugCategory * category,
    GstDebugLevel level, const gchar * file, const gchar * function,
    gint line, GObject * object, GstDebugMessage * message, gpointer unused)
{
  const gchar *dbg_msg;

  dbg_msg = gst_debug_message_get (message);
  fail_unless (dbg_msg != NULL);

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
  gst_debug_add_log_function (printf_extension_log_func, NULL);

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
  gst_debug_add_log_function (gst_debug_log_default, NULL);
  gst_debug_remove_log_function (printf_extension_log_func);
}

GST_END_TEST;

/* check our GST_SEGMENT_FORMAT printf extension stuff */
GST_START_TEST (info_segment_format_printf_extension)
{
  /* set up our own log function to make sure the code in gstinfo is actually
   * executed without GST_DEBUG being set or it being output to stdout */
  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_add_log_function (printf_extension_log_func, NULL);

  gst_debug_set_default_threshold (GST_LEVEL_LOG);

  /* TIME segment */
  {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_TIME);

    gst_segment_set_newsegment_full (&segment, FALSE, 1.0, 2.0,
        GST_FORMAT_TIME, 0, 5 * 60 * GST_SECOND, 0);

    segment.last_stop = 2 * GST_SECOND;
    segment.duration = 90 * 60 * GST_SECOND;

    GST_LOG ("TIME: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* BYTE segment */
  {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);

    gst_segment_set_newsegment_full (&segment, FALSE, 1.0, 1.0,
        GST_FORMAT_BYTES, 0, 9999999, 0);

    GST_LOG ("BYTE: %" GST_SEGMENT_FORMAT, &segment);
  }

  /* UNKNOWN format segment (format numbers are consecutive from 0) */
  {
    GstSegment segment;

    gst_segment_init (&segment, 98765432);

    gst_segment_set_newsegment_full (&segment, FALSE, 1.0, 1.0,
        GST_FORMAT_BYTES, 0, 987654321, 0);

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
  gst_debug_add_log_function (gst_debug_log_default, NULL);
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

#endif

static Suite *
gst_info_suite (void)
{
  Suite *s = suite_create ("GstInfo");
  TCase *tc_chain = tcase_create ("info");

  tcase_set_timeout (tc_chain, 30);

  suite_add_tcase (s, tc_chain);
#ifndef GST_DISABLE_GST_DEBUG
  tcase_add_test (tc_chain, info_segment_format_printf_extension);
  tcase_add_test (tc_chain, info_ptr_format_printf_extension);
  tcase_add_test (tc_chain, info_log_handler);
#endif

  return s;
}

GST_CHECK_MAIN (gst_info);
