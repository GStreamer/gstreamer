/*
 * cmmlenc.c - GStreamer CMML decoder test suite
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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

#include <gst/tag/tag.h>

#define SINK_CAPS "text/x-cmml"
#define SRC_CAPS "text/x-cmml,encoded=(boolean)FALSE"

#define IDENT_HEADER \
  "CMML\x00\x00\x00\x00"\
  "\x03\x00\x00\x00"\
  "\xe8\x03\x00\x00\x00\x00\x00\x00"\
  "\x01\x00\x00\x00\x00\x00\x00\x00"\
  "\x20"

#define XML_PREAMBLE \
  "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"\
  "<!DOCTYPE cmml SYSTEM \"cmml.dtd\">\n"

#define START_TAG \
  "<cmml>"

#define PROCESSING_INSTRUCTION \
  "<?cmml ?>"

#define PREAMBLE \
  XML_PREAMBLE START_TAG

#define PREAMBLE_ENCODED \
  XML_PREAMBLE PROCESSING_INSTRUCTION

#define STREAM_TAG \
  "<stream timebase=\"10\">"\
     "<import src=\"test.ogg\"/>"\
     "<import src=\"test1.ogg\"/>"\
   "</stream>"

#define STREAM_TAG_ENCODED STREAM_TAG

#define HEAD_TAG \
  "<head>"\
    "<title>The Research Hunter</title>"\
    "<meta name=\"DC.audience\" content=\"General\"/>"\
    "<meta name=\"DC.author\" content=\"CSIRO Publishing\"/>"\
    "<meta name=\"DC.format\" content=\"video\"/>"\
    "<meta name=\"DC.language\" content=\"English\"/>"\
    "<meta name=\"DC.publisher\" content=\"CSIRO Australia\"/>"\
  "</head>"

#define HEAD_TAG_ENCODED HEAD_TAG

#define CLIP_TEMPLATE \
  "<clip id=\"%s\" track=\"%s\" start=\"%s\">"\
    "<a href=\"http://www.annodex.org/\">http://www.annodex.org</a>"\
    "<img src=\"images/index.jpg\"/>"\
    "<desc>Annodex Foundation</desc>"\
    "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define ENDED_CLIP_TEMPLATE \
  "<clip id=\"%s\" track=\"%s\" start=\"%s\" end=\"%s\">"\
    "<a href=\"http://www.annodex.org/\">http://www.annodex.org</a>"\
    "<img src=\"images/index.jpg\"/>"\
    "<desc>Annodex Foundation</desc>"\
    "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define CLIP_TEMPLATE_ENCODED \
  "<clip id=\"%s\" track=\"%s\">"\
    "<a href=\"http://www.annodex.org/\">http://www.annodex.org</a>"\
    "<img src=\"images/index.jpg\"/>"\
    "<desc>Annodex Foundation</desc>"\
    "<meta name=\"test\" content=\"test content\"/>"\
  "</clip>"

#define EMPTY_CLIP_TEMPLATE_ENCODED \
  "<clip track=\"%s\"/>"

#define fail_unless_equals_flow_return(a, b)                            \
G_STMT_START {                                                          \
  gchar *a_up = g_ascii_strup (gst_flow_get_name (a), -1);              \
  gchar *b_up = g_ascii_strup (gst_flow_get_name (b), -1);              \
  fail_unless (a == b,                                                  \
      "'" #a "' (GST_FLOW_%s) is not equal to '" #b "' (GST_FLOW_%s)",  \
      a_up, b_up);                                                      \
  g_free (a_up);                                                        \
  g_free (b_up);                                                        \
} G_STMT_END;

static GList *current_buf;
static guint64 granulerate;
static guint8 granuleshift;
static GstElement *cmmlenc;
static GstBus *bus;
static GstFlowReturn flow;
static GstPad *srcpad, *sinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS)
    );

static GstBuffer *
buffer_new (const gchar * buffer_data, guint size)
{
  GstBuffer *buffer;
  GstCaps *caps;

  buffer = gst_buffer_new_and_alloc (size);
  memcpy (GST_BUFFER_DATA (buffer), buffer_data, size);
  caps = gst_caps_from_string (SRC_CAPS);
  gst_buffer_set_caps (buffer, caps);
  gst_caps_unref (caps);

  return buffer;
}

static void
buffer_unref (void *buffer, void *user_data)
{
  gst_buffer_unref (GST_BUFFER (buffer));
}

static void
setup_cmmlenc (void)
{
  guint64 granulerate_n, granulerate_d;

  GST_DEBUG ("setup_cmmlenc");

  cmmlenc = gst_check_setup_element ("cmmlenc");
  srcpad = gst_check_setup_src_pad (cmmlenc, &srctemplate, NULL);
  sinkpad = gst_check_setup_sink_pad (cmmlenc, &sinktemplate, NULL);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  bus = gst_bus_new ();
  gst_element_set_bus (cmmlenc, bus);

  fail_unless (gst_element_set_state (cmmlenc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  g_object_get (cmmlenc, "granule-rate-numerator", &granulerate_n,
      "granule-rate-denominator", &granulerate_d,
      "granule-shift", &granuleshift, NULL);

  granulerate = GST_SECOND * granulerate_d / granulerate_n;
}

static void
teardown_cmmlenc (void)
{
  /* free encoded buffers */
  g_list_foreach (buffers, buffer_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;
  current_buf = NULL;

  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  GST_DEBUG ("teardown_cmmlenc");
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (cmmlenc);
  gst_check_teardown_sink_pad (cmmlenc);
  gst_check_teardown_element (cmmlenc);
}

static void
check_output_buffer_is_equal (const gchar * name,
    const gchar * data, gint refcount)
{
  GstBuffer *buffer;

  if (current_buf == NULL)
    current_buf = buffers;
  else
    current_buf = g_list_next (current_buf);

  fail_unless (current_buf != NULL);
  buffer = GST_BUFFER (current_buf->data);

  ASSERT_OBJECT_REFCOUNT (buffer, name, refcount);
  fail_unless (memcmp (GST_BUFFER_DATA (buffer), data,
          GST_BUFFER_SIZE (buffer)) == 0,
      "'%s' (%s) is not equal to (%s)", name, GST_BUFFER_DATA (buffer), data);
}

static GstFlowReturn
push_data (const gchar * name, const gchar * data, gint size)
{
  GstBuffer *buffer;
  GstFlowReturn res;

  buffer = buffer_new (data, size);
  res = gst_pad_push (srcpad, buffer);

  return res;
}

static void
check_headers (void)
{
  /* push the cmml start tag */
  flow = push_data ("preamble", PREAMBLE, strlen (PREAMBLE));
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* push the stream tag */
  flow = push_data ("stream", STREAM_TAG, strlen (STREAM_TAG));
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  /* push the head tag */
  flow = push_data ("head", HEAD_TAG, strlen (HEAD_TAG));
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);

  /* should output 3 buffers: the ident, preamble and head headers */
  fail_unless_equals_int (g_list_length (buffers), 3);

  /* check the ident header */
  check_output_buffer_is_equal ("cmml-ident-buffer", IDENT_HEADER, 1);

  /* check the cmml processing instruction */
  check_output_buffer_is_equal ("cmml-preamble-buffer", PREAMBLE_ENCODED, 1);

  /* check the encoded head tag */
  check_output_buffer_is_equal ("head-tag-buffer", HEAD_TAG_ENCODED, 1);
}

static GstFlowReturn
push_clip (const gchar * name, const gchar * track,
    const gchar * start, const gchar * end)
{
  gchar *clip;
  GstFlowReturn res;

  if (end != NULL)
    clip = g_strdup_printf (ENDED_CLIP_TEMPLATE, name, track, start, end);
  else
    clip = g_strdup_printf (CLIP_TEMPLATE, name, track, start);
  res = push_data (name, clip, strlen (clip));
  g_free (clip);

  return res;
}

static void
check_clip_times (GstBuffer * buffer, GstClockTime start, GstClockTime prev)
{
  guint64 keyindex, keyoffset, granulepos;

  granulepos = GST_BUFFER_OFFSET_END (buffer);
  if (granuleshift == 0 || granuleshift == 64)
    keyindex = 0;
  else
    keyindex = granulepos >> granuleshift;
  keyoffset = granulepos - (keyindex << granuleshift);
  fail_unless_equals_uint64 (keyindex * granulerate, prev);
  fail_unless_equals_uint64 ((keyindex + keyoffset) * granulerate, start);
}

static void
check_clip (const gchar * name, const gchar * track,
    GstClockTime start, GstClockTime prev)
{
  gchar *encoded_clip;
  GstBuffer *buffer;

  encoded_clip = g_strdup_printf (CLIP_TEMPLATE_ENCODED, name, track);
  check_output_buffer_is_equal (name, encoded_clip, 1);
  g_free (encoded_clip);
  buffer = GST_BUFFER (current_buf->data);
  check_clip_times (buffer, start, prev);
}

static void
check_empty_clip (const gchar * name, const gchar * track,
    GstClockTime start, GstClockTime prev)
{
  gchar *encoded_clip;
  GstBuffer *buffer;

  encoded_clip = g_strdup_printf (EMPTY_CLIP_TEMPLATE_ENCODED, track);
  check_output_buffer_is_equal (name, encoded_clip, 1);
  g_free (encoded_clip);
  buffer = GST_BUFFER (current_buf->data);
  check_clip_times (buffer, start, prev);
}

GST_START_TEST (test_enc)
{
  check_headers ();

  flow = push_clip ("clip-1", "default", "1.234", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-1", "default", 1 * GST_SECOND + 234 * GST_MSECOND, 0);

  flow = push_clip ("clip-2", "default", "5.678", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-2", "default",
      5 * GST_SECOND + 678 * GST_MSECOND, 1 * GST_SECOND + 234 * GST_MSECOND);

  flow = push_clip ("clip-3", "othertrack", "9.123", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-3", "othertrack", 9 * GST_SECOND + 123 * GST_MSECOND, 0);

  flow = push_data ("end-tag", "</cmml>", strlen ("</cmml>"));
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_output_buffer_is_equal ("cmml-eos", NULL, 1);
}

GST_END_TEST;

GST_START_TEST (test_clip_end_time)
{
  check_headers ();

  /* push a clip that starts at 1.234 an ends at 2.234 */
  flow = push_clip ("clip-1", "default", "1.234", "2.234");
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-1", "default", 1 * GST_SECOND + 234 * GST_MSECOND, 0);

  /* now check that the encoder created an empty clip starting at 2.234 to mark
   * the end of clip-1 */
  check_empty_clip ("clip-1-end", "default",
      2 * GST_SECOND + 234 * GST_MSECOND, 1 * GST_SECOND + 234 * GST_MSECOND);

  /* now push another clip on the same track and check that the keyindex part of
   * the granulepos points to clip-1 and not to the empty clip */
  flow = push_clip ("clip-2", "default", "5", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-2", "default",
      5 * GST_SECOND, 1 * GST_SECOND + 234 * GST_MSECOND);
}

GST_END_TEST;

GST_START_TEST (test_time_order)
{
  check_headers ();

  /* clips belonging to the same track must have start times in non decreasing
   * order */
  flow = push_clip ("clip-1", "default", "1000:00:00.000", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-1", "default", 3600 * 1000 * GST_SECOND, 0);

  /* this will make the encoder throw an error message */
  flow = push_clip ("clip-2", "default", "5.678", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  flow = push_clip ("clip-3", "default", "1000:00:00.001", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-3", "default",
      3600 * 1000 * GST_SECOND + 1 * GST_MSECOND, 3600 * 1000 * GST_SECOND);

  /* tracks don't interfere with each other */
  flow = push_clip ("clip-4", "othertrack", "9.123", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("clip-4", "othertrack", 9 * GST_SECOND + 123 * GST_MSECOND, 0);
}

GST_END_TEST;

GST_START_TEST (test_time_parsing)
{
  check_headers ();

  flow = push_clip ("bad-msecs", "default", "0.1000", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  flow = push_clip ("bad-secs", "default", "00:00:60.123", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  flow = push_clip ("bad-minutes", "default", "00:60:12.345", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* this fails since we can't store 5124096 * 3600 * GST_SECOND in a
   * GstClockTime */
  flow = push_clip ("bad-hours", "default", "5124096:00:00.000", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);
}

GST_END_TEST;

GST_START_TEST (test_time_limits)
{
  check_headers ();

  /* ugly hack to make sure that the following checks actually overflow parsing
   * the times in gst_cmml_clock_time_from_npt rather than converting them to
   * granulepos in gst_cmml_clock_time_to_granule */
  granuleshift = 64;
  g_object_set (cmmlenc, "granule-shift", granuleshift, NULL);

  /* 5124095:34:33.709 is the max npt-hhmmss time representable with
   * GstClockTime */
  flow = push_clip ("max-npt-hhmmss", "foo", "5124095:34:33.709", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("max-npt-hhmmss", "foo",
      (GstClockTime) 5124095 * 3600 * GST_SECOND + 34 * 60 * GST_SECOND +
      33 * GST_SECOND + 709 * GST_MSECOND, 0);

  flow = push_clip ("overflow-max-npt-hhmmss", "overflows",
      "5124095:34:33.710", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* 18446744073.709 is the max ntp-sec time */
  flow = push_clip ("max-npt-secs", "bar", "18446744073.709", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("max-npt-secs", "bar",
      (GstClockTime) 5124095 * 3600 * GST_SECOND + 34 * 60 * GST_SECOND +
      33 * GST_SECOND + 709 * GST_MSECOND, 0);

  /* overflow doing 18446744074 * GST_SECOND */
  flow = push_clip ("overflow-max-npt-secs", "overflows",
      "18446744074.000", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* overflow doing seconds + milliseconds */
  flow = push_clip ("overflow-max-npt-secs-msecs", "overflows",
      "18446744073.710", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);

  /* reset granuleshift to 32 to check keyoffset overflows in
   * gst_cmml_clock_time_to_granule */
  granuleshift = 32;
  g_object_set (cmmlenc, "granule-shift", granuleshift, NULL);

  /* 1193:02:47.295 is the max time we can encode in the keyoffset part of a
   * granulepos given a granuleshift of 32 */
  flow = push_clip ("max-granule-keyoffset", "baz", "1193:02:47.295", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_OK);
  check_clip ("max-granule-keyoffset", "baz",
      1193 * 3600 * GST_SECOND + 2 * 60 * GST_SECOND +
      47 * GST_SECOND + 295 * GST_MSECOND, 0);

  flow = push_clip ("overflow-max-granule-keyoffset", "overflows",
      "1193:02:47.296", NULL);
  fail_unless_equals_flow_return (flow, GST_FLOW_ERROR);
}

GST_END_TEST;

static Suite *
cmmlenc_suite (void)
{
  Suite *s = suite_create ("cmmlenc");
  TCase *tc_general = tcase_create ("general");

  suite_add_tcase (s, tc_general);
  tcase_add_checked_fixture (tc_general, setup_cmmlenc, teardown_cmmlenc);
  tcase_add_test (tc_general, test_enc);
  tcase_add_test (tc_general, test_clip_end_time);
  tcase_add_test (tc_general, test_time_order);
  tcase_add_test (tc_general, test_time_parsing);
  tcase_add_test (tc_general, test_time_limits);

  return s;
}

GST_CHECK_MAIN (cmmlenc);
