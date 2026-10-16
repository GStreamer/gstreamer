/* GStreamer
 *
 * unit test for videorate
 *
 * Copyright (C) 2006 Thomas Vander Stichele <thomas at apestaart dot org>
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

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;


#define VIDEO_CAPS_TEMPLATE_STRING     \
    "video/x-raw"

#define VIDEO_CAPS_STRING               \
    "video/x-raw, "                 \
    "width = (int) 320, "               \
    "height = (int) 240, "              \
    "framerate = (fraction) 25/1 , "    \
    "format = (string) I420"

#define VIDEO_CAPS_FORCE_VARIABLE_FRAMERATE_STRING \
    "video/x-raw, "                 \
    "framerate = (fraction) 0/1"

#define VIDEO_CAPS_VARIABLE_FRAMERATE_STRING \
    "video/x-raw, "                 \
    "width = (int) 320, "               \
    "height = (int) 240, "              \
    "framerate = (fraction) 0/1 , "     \
    "format = (string) I420"

#define VIDEO_CAPS_NO_FRAMERATE_STRING  \
    "video/x-raw, "                 \
    "width = (int) 320, "               \
    "height = (int) 240, "              \
    "format = (string) I420"

#define VIDEO_CAPS_NEWSIZE_STRING       \
    "video/x-raw, "                 \
    "width = (int) 240, "               \
    "height = (int) 120, "              \
    "framerate = (fraction) 25/1 , "	\
    "format = (string) I420"

#define VIDEO_CAPS_UNUSUAL_FRAMERATE    \
    "video/x-raw, "                 \
    "width = (int) 240, "               \
    "height = (int) 120, "              \
    "framerate = (fraction) 999/7 , "	\
    "format = (string) I420"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate downstreamsinktemplate =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );

static GstStaticPadTemplate force_variable_rate_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_FORCE_VARIABLE_FRAMERATE_STRING)
    );

static void
assert_videorate_stats (GstElement * videorate, const gchar * reason,
    guint64 xin, guint64 xout, guint64 xdropped, guint64 xduplicated)
{
  guint64 in, out, dropped, duplicated;

  g_object_get (videorate, "in", &in, "out", &out, "drop", &dropped,
      "duplicate", &duplicated, NULL);
#define _assert_equals_uint64(a, b)                                     \
G_STMT_START {                                                          \
  guint64 first = a;                                                    \
  guint64 second = b;                                                   \
  fail_unless(first == second,                                          \
    "%s: '" #a "' (%" G_GUINT64_FORMAT ") is not equal to "		\
    "expected '" #a"' (%" G_GUINT64_FORMAT ")", reason, first, second); \
} G_STMT_END;


  _assert_equals_uint64 (in, xin);
  _assert_equals_uint64 (out, xout);
  _assert_equals_uint64 (dropped, xdropped);
  _assert_equals_uint64 (duplicated, xduplicated);
}

static GstElement *
setup_videorate_full (GstStaticPadTemplate * srctemplate,
    GstStaticPadTemplate * sinktemplate)
{
  GstElement *videorate;

  GST_DEBUG ("setup_videorate");
  videorate = gst_check_setup_element ("videorate");
  mysrcpad = gst_check_setup_src_pad (videorate, srctemplate);
  mysinkpad = gst_check_setup_sink_pad (videorate, sinktemplate);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return videorate;
}

static GstElement *
setup_videorate (void)
{
  return setup_videorate_full (&srctemplate, &sinktemplate);
}

static void
cleanup_videorate (GstElement * videorate)
{
  GST_DEBUG ("cleanup_videorate");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_element_set_state (videorate, GST_STATE_NULL);
  gst_element_get_state (videorate, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (videorate);
  gst_check_teardown_sink_pad (videorate);
  gst_check_teardown_element (videorate);
}

static guint8
buffer_get_byte (GstBuffer * buffer, gint offset)
{
  guint8 res;

  gst_buffer_extract (buffer, offset, &res, 1);

  return res;
}

GST_START_TEST (test_one)
{
  GstElement *videorate;
  GstBuffer *inbuffer;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (inbuffer, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

GST_START_TEST (test_more)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *outbuffer;
  GList *l;
  GstCaps *caps;
  GRand *rand;
  guint i;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "creation", 0, 0, 0, 0);

  rand = g_rand_new ();

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = 0;
  /* it shouldn't matter what the offsets are, videorate produces perfect
     streams */
  GST_BUFFER_OFFSET (first) = g_rand_int (rand);
  GST_BUFFER_OFFSET_END (first) = g_rand_int (rand);
  gst_buffer_memset (first, 0, 1, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first buffer", 1, 0, 0, 0);

  /* second buffer; in between second and third output frame's timestamp */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND * 3 / 50;
  GST_BUFFER_OFFSET (second) = g_rand_int (rand);
  GST_BUFFER_OFFSET_END (second) = g_rand_int (rand);
  gst_buffer_memset (second, 0, 2, 4);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away one of my references ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);

  /* ... and the first two are pushed out, with timestamp 0 and 1/25 */
  fail_unless_equals_int (g_list_length (buffers), 2);
  assert_videorate_stats (videorate, "second buffer", 2, 2, 0, 0);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);

  outbuffer = buffers->data;
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer), 0);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = GST_SECOND * 12 / 50;
  GST_BUFFER_OFFSET (third) = g_rand_int (rand);
  GST_BUFFER_OFFSET_END (third) = g_rand_int (rand);
  gst_buffer_memset (third, 0, 3, 4);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);

  /* submitting the third buffer has triggered flushing of five more frames,
     everything until 1/25 -> 6/25 (12/50) */
  assert_videorate_stats (videorate, "third buffer", 3, 7, 0, 4);

  /* check timestamp and source correctness */
  l = buffers;
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data), 0);
  fail_unless_equals_int (buffer_get_byte (l->data, 0), 1);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET (l->data), 0);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (l->data), 1);

  l = g_list_next (l);
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data), GST_SECOND / 25);
  fail_unless_equals_int (buffer_get_byte (l->data, 0), 2);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET (l->data), 1);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (l->data), 2);

  l = g_list_next (l);
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data),
      GST_SECOND * 2 / 25);
  fail_unless_equals_int (buffer_get_byte (l->data, 0), 2);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET (l->data), 2);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (l->data), 3);

  l = g_list_next (l);
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data),
      GST_SECOND * 3 / 25);
  fail_unless_equals_int (buffer_get_byte (l->data, 0), 2);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET (l->data), 3);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (l->data), 4);

  /* third frame from 4/25 to 6/25 */
  for (i = 4; i <= 6; i++) {
    l = g_list_next (l);
    fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data),
        GST_SECOND * i / 25);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), 3);
    fail_unless_equals_uint64 (GST_BUFFER_OFFSET (l->data), i);
    fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (l->data), i + 1);
  }

  fail_unless_equals_int (g_list_length (buffers), 7);
  /* one held by us, three held by each output frame taken from the second */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);

  /* now send EOS */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* 6/25 was already output, nothing left to push */
  assert_videorate_stats (videorate, "eos", 3, 7, 0, 4);
  fail_unless_equals_int (g_list_length (buffers), 7);

  /* cleanup */
  g_rand_free (rand);
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* EOS without segment stop fills up to the end of the last buffer */
GST_START_TEST (test_eos_fill_last_buffer_duration)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GList *l;
  guint i;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* frames at 0, 40 and 80ms, the last one lasting 120ms */
  for (i = 0; i < 3; i++) {
    buf = gst_buffer_new_and_alloc (4);
    gst_buffer_memset (buf, 0, i, 4);
    GST_BUFFER_PTS (buf) = i * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = i < 2 ? 40 * GST_MSECOND : 120 * GST_MSECOND;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* the last frame fills 80, 120 and 160ms */
  fail_unless_equals_int (g_list_length (buffers), 5);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), MIN (i, 2));
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 2);
  }
  assert_videorate_stats (videorate, "eos", 3, 5, 0, 2);

  cleanup_videorate (videorate);
}

GST_END_TEST;

/* single frame that could fill 1s with a 120ms duplication limit */
GST_START_TEST (test_max_closing_segment_duplication_duration)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  guint i;

  /* EOS fills 0, 40 and 80ms */
  videorate = setup_videorate ();
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      120 * GST_MSECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 0;
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * 40 * GST_MSECOND);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 0);
  }
  assert_videorate_stats (videorate, "eos", 1, 3, 0, 2);

  cleanup_videorate (videorate);

  /* new segment fills 0, 40 and 80ms */
  videorate = setup_videorate ();
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      120 * GST_MSECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 0;
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);

  segment.start = 2 * GST_SECOND;
  segment.stop = 3 * GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * 40 * GST_MSECOND);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 0);
  }
  assert_videorate_stats (videorate, "segment", 1, 3, 0, 2);

  cleanup_videorate (videorate);

  /* reverse EOS fills 960, 920 and 880ms */
  videorate = setup_videorate ();
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      120 * GST_MSECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 960 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        (24 - i) * 40 * GST_MSECOND);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 0);
  }
  assert_videorate_stats (videorate, "reverse eos", 1, 3, 0, 2);

  cleanup_videorate (videorate);

  /* reverse EOS with 10fps input and 25fps output fills 960, 920 and 880ms */
  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      120 * GST_MSECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 10, 1, NULL);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 900 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 100 * GST_MSECOND;
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        (24 - i) * 40 * GST_MSECOND);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 0);
  }
  assert_videorate_stats (videorate, "reverse long eos", 1, 3, 0, 2);

  cleanup_videorate (videorate);

  /* EOS without segment stop fills 0, 40 and 80ms of a 1s frame */
  videorate = setup_videorate ();
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      120 * GST_MSECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = GST_SECOND;
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * 40 * GST_MSECOND);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i > 0);
  }
  assert_videorate_stats (videorate, "open eos", 1, 3, 0, 2);

  cleanup_videorate (videorate);
}

GST_END_TEST;

/* frames at 1, 0, 2 -> second one should be ignored */
GST_START_TEST (test_wrong_order_from_zero)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *outbuffer;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "start", 0, 0, 0, 0);

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = GST_SECOND;
  gst_buffer_memset (first, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = 0;
  gst_buffer_memset (second, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and it is now dropped because it is too old */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* ... and the first one is still there */
  assert_videorate_stats (videorate, "second", 2, 0, 1, 0);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND;
  gst_buffer_memset (third, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);

  /* 26 buffer for 0s -> 1s and another 25 from 26/25s -> 2s */
  fail_unless_equals_int (g_list_length (buffers), 51);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  assert_videorate_stats (videorate, "third", 3, 51, 1, 49);

  /* verify last buffer */
  outbuffer = g_list_last (buffers)->data;
  fail_unless (GST_IS_BUFFER (outbuffer));
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer),
      GST_SECOND * 50 / 25);

  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* send frames with 0, 1, 2, 5, 6 seconds, max-duplication-time=2sec */
GST_START_TEST (test_max_duplication_time)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *fourth, *fifth, *outbuffer;
  GstCaps *caps;
  GList *l;
  guint i;

  videorate = setup_videorate ();
  g_object_set (videorate, "max-duplication-time", 2 * GST_SECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "start", 0, 0, 0, 0);

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = 0;
  gst_buffer_memset (first, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND;
  gst_buffer_memset (second, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  /* and it created 26 output buffers as copies of the
     first and second frame */
  fail_unless_equals_int (g_list_length (buffers), 26);
  assert_videorate_stats (videorate, "second", 2, 26, 0, 24);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND;
  gst_buffer_memset (third, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);

  /* submitting a frame with 2 seconds triggers output of 25 more frames */
  fail_unless_equals_int (g_list_length (buffers), 51);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  /* three frames submitted; two of them output as is,
     and 36 + 12 duplicated */
  assert_videorate_stats (videorate, "third", 3, 51, 0, 48);

  /* fourth buffer, this buffer exceeds max-duplication-time */
  fourth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fourth) = 5 * GST_SECOND;
  gst_buffer_memset (fourth, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  gst_buffer_ref (fourth);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, fourth) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);

  /* should now have drained everything up to the 5s buffer above */
  fail_unless_equals_int (g_list_length (buffers), 52);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  assert_videorate_stats (videorate, "fourth", 4, 52, 0, 48);

  /* verify last buffer */
  outbuffer = g_list_last (buffers)->data;
  fail_unless (GST_IS_BUFFER (outbuffer));
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer), 5 * GST_SECOND);

  /* fifth buffer */
  fifth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fifth) = 6 * GST_SECOND;
  gst_buffer_memset (fifth, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (fifth, "fifth", 1);
  gst_buffer_ref (fifth);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, fifth) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (fifth, "fifth", 1);

  /* submitting a frame with 6 seconds triggers output of 25 more frames
   * between 5s and 6s */
  l = g_list_nth (buffers, 52);
  for (i = 1; i <= 25; i++) {
    fail_unless (l != NULL);
    fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (l->data),
        5 * GST_SECOND + i * GST_SECOND / 25);
    l = g_list_next (l);
  }
  fail_unless (l == NULL);
  fail_unless_equals_int (g_list_length (buffers), 77);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  ASSERT_BUFFER_REFCOUNT (fifth, "fifth", 1);
  assert_videorate_stats (videorate, "fifth", 5, 77, 0, 72);

  /* push EOS */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* 6s was already output, nothing left to push */
  fail_unless_equals_int (g_list_length (buffers), 77);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  ASSERT_BUFFER_REFCOUNT (fifth, "fifth", 1);
  assert_videorate_stats (videorate, "fifth", 5, 77, 0, 72);

  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  gst_buffer_unref (fourth);
  gst_buffer_unref (fifth);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* reverse frames at 10.2s, 10s, 200ms and 0, max-duplication-time=2sec */
GST_START_TEST (test_max_duplication_time_reverse)
{
  const GstClockTime frame_duration = 40 * GST_MSECOND;
  const GstClockTime timestamps[] = {
    10 * GST_SECOND + 5 * frame_duration, 10 * GST_SECOND,
    5 * frame_duration, 0,
  };
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  guint i;

  videorate = setup_videorate ();
  g_object_set (videorate, "max-duplication-time", 2 * GST_SECOND, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.start = 0;
  segment.stop = 10 * GST_SECOND + 6 * frame_duration;
  segment.position = segment.stop;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  for (i = 0; i < G_N_ELEMENTS (timestamps); i++) {
    buf = gst_buffer_new_and_alloc (4);
    gst_buffer_memset (buf, 0, i, 4);
    GST_BUFFER_PTS (buf) = timestamps[i];
    GST_BUFFER_DURATION (buf) = frame_duration;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }

  fail_unless_equals_int (g_list_length (buffers), 12);

  /* 10.2s -> 10s is filled with duplicates */
  for (l = buffers, i = 0; i < 6; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        10 * GST_SECOND + (5 - i) * frame_duration);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), i / 3);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i % 3 != 0);
  }

  /* 10s -> 200ms is not filled, 200ms -> 0 is filled with duplicates */
  fail_unless (GST_BUFFER_FLAG_IS_SET (l->data, GST_BUFFER_FLAG_DISCONT));
  for (i = 0; i < 6; l = l->next, i++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        (5 - i) * frame_duration);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), 2 + i / 3);
    fail_unless_equals_int (GST_BUFFER_FLAG_IS_SET (l->data,
            GST_BUFFER_FLAG_GAP), i % 3 != 0);
  }

  cleanup_videorate (videorate);
}

GST_END_TEST;

/* send frames with 0, 1, 2, 0 seconds */
GST_START_TEST (test_wrong_order)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *fourth, *outbuffer;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "start", 0, 0, 0, 0);

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = 0;
  gst_buffer_memset (first, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND;
  gst_buffer_memset (second, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  /* and it created 26 output buffers as copies of the first frame */
  fail_unless_equals_int (g_list_length (buffers), 26);
  assert_videorate_stats (videorate, "second", 2, 26, 0, 24);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND;
  gst_buffer_memset (third, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);

  /* submitting a frame at 2s triggers output of 25 more frames */
  fail_unless_equals_int (g_list_length (buffers), 51);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  /* three frames submitted; two of them output as is, and 36 + 11 duplicated */
  assert_videorate_stats (videorate, "third", 3, 51, 0, 48);

  /* fourth buffer */
  fourth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fourth) = 0;
  gst_buffer_memset (fourth, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  gst_buffer_ref (fourth);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, fourth) == GST_FLOW_OK);
  /* ... and it is dropped */
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);

  fail_unless_equals_int (g_list_length (buffers), 51);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  assert_videorate_stats (videorate, "fourth", 4, 51, 1, 48);

  /* verify last buffer */
  outbuffer = g_list_last (buffers)->data;
  fail_unless (GST_IS_BUFFER (outbuffer));
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer),
      GST_SECOND * 50 / 25);


  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  gst_buffer_unref (fourth);
  cleanup_videorate (videorate);
}

GST_END_TEST;


/* if no framerate is negotiated, we should not be able to push a buffer */
GST_START_TEST (test_no_framerate)
{
  GstElement *videorate;
  GstBuffer *inbuffer;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (inbuffer, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_NO_FRAMERATE_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* take a ref so we can later check refcount */
  gst_buffer_ref (inbuffer);

  /* no framerate is negotiated so pushing should fail */
  fail_if (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* This test outputs 2 buffers of same dimensions (320x240), then 1 buffer of
 * differing dimensions (240x120), and then another buffer of previous
 * dimensions (320x240) and checks that the 3 buffers output as a result have
 * correct caps (first 2 with 320x240 and 3rd with 240x120).
 */
GST_START_TEST (test_changing_size)
{
  GstElement *videorate;
  GstBuffer *first;
  GstBuffer *second;
  GstBuffer *third;
  GstBuffer *fourth;
  GstBuffer *fifth;
  GstBuffer *outbuf;
  GstCaps *caps, *caps_newsize;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  first = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (first, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  GST_BUFFER_TIMESTAMP (first) = 0;
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);

  GST_DEBUG ("pushing first buffer");
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND / 25;
  gst_buffer_memset (second, 0, 0, 4);

  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 2);
  outbuf = buffers->data;
  /* first two buffers should be output here */
  fail_unless (GST_BUFFER_TIMESTAMP (outbuf) == 0);

  /* third buffer with new size */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND / 25;
  gst_buffer_memset (third, 0, 0, 4);
  caps_newsize = gst_caps_from_string (VIDEO_CAPS_NEWSIZE_STRING);
  gst_pad_set_caps (mysrcpad, caps_newsize);

  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* output next buffer after caps change */
  fail_unless_equals_int (g_list_length (buffers), 3);
  outbuf = g_list_last (buffers)->data;
  /* first 3 buffers should be output here */
  //fail_unless (gst_caps_is_equal (GST_BUFFER_CAPS (outbuf), caps));
  fail_unless (GST_BUFFER_TIMESTAMP (outbuf) == GST_SECOND * 2 / 25);

  /* fourth buffer with original size */
  fourth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fourth) = 3 * GST_SECOND / 25;
  gst_buffer_memset (fourth, 0, 0, 4);
  gst_pad_set_caps (mysrcpad, caps);

  fail_unless (gst_pad_push (mysrcpad, fourth) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 4);

  /* fifth buffer with original size */
  fifth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fifth) = 4 * GST_SECOND / 25;
  gst_buffer_memset (fifth, 0, 0, 4);

  fail_unless (gst_pad_push (mysrcpad, fifth) == GST_FLOW_OK);
  /* all five buffers here */
  fail_unless_equals_int (g_list_length (buffers), 5);
  outbuf = g_list_last (buffers)->data;
  /* third buffer should be output here */
  fail_unless (GST_BUFFER_TIMESTAMP (outbuf) == 4 * GST_SECOND / 25);

  gst_caps_unref (caps);
  gst_caps_unref (caps_newsize);
  cleanup_videorate (videorate);
}

GST_END_TEST;

GST_START_TEST (test_non_ok_flow)
{
  GstElement *videorate;
  GstClockTime ts;
  GstBuffer *buf;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buf = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buf, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (buf, "inbuffer", 1);

  /* push a few 'normal' buffers */
  for (ts = 0; ts < 100 * GST_SECOND; ts += GST_SECOND / 33) {
    GstBuffer *inbuf;

    inbuf = gst_buffer_copy (buf);
    GST_BUFFER_TIMESTAMP (inbuf) = ts;

    fail_unless_equals_int (gst_pad_push (mysrcpad, inbuf), GST_FLOW_OK);
  }

  /* we should have buffers according to the output framerate of 25/1 */
  fail_unless_equals_int (g_list_length (buffers), 100 * 25);

  /* now deactivate pad so we get a WRONG_STATE flow return */
  gst_pad_set_active (mysinkpad, FALSE);

  /* push buffer on deactivated pad */
  fail_unless (gst_buffer_is_writable (buf));
  GST_BUFFER_TIMESTAMP (buf) = ts;

  /* pushing gives away our reference */
  fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_FLUSHING);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

GST_START_TEST (test_upstream_caps_nego)
{
  GstElement *videorate;
  GstPad *videorate_pad;
  GstCaps *expected_caps;
  GstCaps *caps;
  GstStructure *structure;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  videorate_pad = gst_element_get_static_pad (videorate, "sink");
  caps = gst_pad_query_caps (videorate_pad, NULL);

  /* assemble the expected caps */
  structure = gst_structure_from_string (VIDEO_CAPS_STRING, NULL);
  expected_caps = gst_caps_new_empty ();
  gst_caps_append_structure (expected_caps, structure);
  structure = gst_structure_copy (structure);
  gst_structure_set (structure, "framerate", GST_TYPE_FRACTION_RANGE,
      0, 1, G_MAXINT, 1, NULL);
  gst_caps_append_structure (expected_caps, structure);

  fail_unless (gst_caps_is_equal (expected_caps, caps));
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);
  gst_object_unref (videorate_pad);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;


GST_START_TEST (test_selected_caps)
{
  GstElement *videorate;
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;

  pipeline = gst_parse_launch ("videotestsrc num-buffers=1"
      " ! identity ! videorate name=videorate0 ! " VIDEO_CAPS_UNUSUAL_FRAMERATE
      " ! fakesink", NULL);
  fail_if (pipeline == NULL);
  videorate = gst_bin_get_by_name (GST_BIN (pipeline), "videorate0");
  fail_if (videorate == NULL);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_if (msg == NULL || GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);

  /* make sure upstream nego works right and videotestsrc has selected the
   * caps we want downstream of videorate */
  {
    GstStructure *s;
    const GValue *val;
    GstCaps *caps = NULL;
    GstPad *videorate_pad;

    videorate_pad = gst_element_get_static_pad (videorate, "sink");
    g_object_get (videorate_pad, "caps", &caps, NULL);
    fail_unless (caps != NULL);

    GST_DEBUG ("negotiated caps: %" GST_PTR_FORMAT, caps);

    s = gst_caps_get_structure (caps, 0);
    val = gst_structure_get_value (s, "framerate");
    fail_unless (val != NULL, "no framerate field in negotiated caps");
    fail_unless (GST_VALUE_HOLDS_FRACTION (val));
    fail_unless_equals_int (gst_value_get_fraction_numerator (val), 999);
    fail_unless_equals_int (gst_value_get_fraction_denominator (val), 7);

    gst_caps_unref (caps);
    gst_object_unref (videorate_pad);
  }

  /* cleanup */
  gst_object_unref (bus);
  gst_message_unref (msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_object_unref (videorate);
  gst_object_unref (pipeline);
}

GST_END_TEST;


/* Caps negotiation tests */
typedef struct
{
  const gchar *caps;
  gboolean drop_only;
  int max_rate;
  /* Result of the videomaxrate caps after transforming */
  const gchar *expected_sink_caps;
  const gchar *expected_src_caps;
} TestInfo;

static TestInfo caps_negotiation_tests[] = {
  {
        .caps = "video/x-raw",
        .drop_only = FALSE,
        .expected_sink_caps = "video/x-raw",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw",
        .drop_only = FALSE,
        .max_rate = 15,
        .expected_sink_caps = "video/x-raw",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, 15]"},
  {
        .caps = "video/x-raw",
        .drop_only = TRUE,
        .expected_sink_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw",
        .drop_only = TRUE,
        .max_rate = 15,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[0/1, 15];"
        "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, 15]"},


  {
        .caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
        .drop_only = FALSE,
        .expected_sink_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
        .drop_only = FALSE,
        .max_rate = 15,
        .expected_sink_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, 15]"},
  {
        .caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
        .drop_only = TRUE,
        .expected_sink_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw, framerate=(fraction)[0/1, MAX]",
        .drop_only = TRUE,
        .max_rate = 15,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[0/1, 15];"
        "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps = "video/x-raw, framerate=(fraction)[0/1, 15]"},
  {
        .caps = "video/x-raw, framerate=15/1",
        .drop_only = FALSE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw, framerate=15/1",
        .drop_only = FALSE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX]",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, 20/1]"},
  {
        .caps = "video/x-raw, framerate=15/1",
        .drop_only = TRUE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, 15/1]"},
  {
        .caps = "video/x-raw, framerate=15/1",
        .drop_only = TRUE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, 15/1];"},
  {
        .caps = "video/x-raw, framerate=[15/1, 30/1]",
        .drop_only = FALSE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[0/1, MAX];"},
  {
        .caps = "video/x-raw, framerate=[15/1, 30/1]",
        .drop_only = FALSE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)[15/1, 20/1];"
        "video/x-raw, framerate=(fraction)[0/1, 20/1];"},
  {
        .caps = "video/x-raw, framerate=[15/1, 30/1]",
        .drop_only = TRUE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[0/1, 30/1]"},
  {
        .caps = "video/x-raw, framerate=[15/1, 30/1]",
        .drop_only = TRUE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)[15/1, 20/1];"
        "video/x-raw, framerate=(fraction)[15/1, 30/1];"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)[15/1, 20/1];"
        "video/x-raw, framerate=(fraction)[0/1, 20/1]"},
  {
        .caps = "video/x-raw, framerate={15/1, 30/1}",
        .drop_only = FALSE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw, framerate={15/1, 30/1}",
        .drop_only = FALSE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, 20/1];"},
  {
        .caps = "video/x-raw, framerate={15/1, 30/1}",
        .drop_only = TRUE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[0/1, 30/1];"},
  {
        .caps = "video/x-raw, framerate={15/1, 30/1}",
        .drop_only = TRUE,
        .max_rate = 20,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction){15/1, 30/1};"
        "video/x-raw, framerate=(fraction)[15/1, MAX];"
        "video/x-raw, framerate=(fraction)0/1",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)15/1;"
        "video/x-raw, framerate=(fraction)[0/1, 20/1]"},
  {
        .caps = "video/x-raw, framerate=0/1",
        .drop_only = TRUE,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)0/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)0/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX]"},
  {
        .caps = "video/x-raw, framerate=0/1",
        .drop_only = TRUE,
        .max_rate = 15,
        .expected_sink_caps =
        "video/x-raw, framerate=(fraction)0/1;"
        "video/x-raw, framerate=(fraction)[0/1, MAX];",
      .expected_src_caps =
        "video/x-raw, framerate=(fraction)0/1;"
        "video/x-raw, framerate=(fraction)[0/1, 15/1]"},
};

static gboolean
_query_function (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *caps = g_object_get_data (G_OBJECT (pad), "caps");

      fail_unless (caps != NULL);

      gst_query_set_caps_result (query, caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static void
check_caps_identical (GstCaps * a, GstCaps * b, const char *name)
{
  int i;
  gchar *caps_str_a;
  gchar *caps_str_b;

  if (gst_caps_get_size (a) != gst_caps_get_size (b))
    goto fail;

  for (i = 0; i < gst_caps_get_size (a); i++) {
    GstStructure *sa, *sb;

    sa = gst_caps_get_structure (a, i);
    sb = gst_caps_get_structure (b, i);

    if (!gst_structure_is_equal (sa, sb))
      goto fail;
  }

  return;

fail:
  caps_str_a = gst_caps_to_string (a);
  caps_str_b = gst_caps_to_string (b);
  fail ("%s caps (%s) is not equal to caps (%s)", name, caps_str_a, caps_str_b);
  g_free (caps_str_a);
  g_free (caps_str_b);
}

static void
check_peer_caps (GstPad * pad, const char *expected, const char *name)
{
  GstCaps *caps;
  GstCaps *expected_caps;

  caps = gst_pad_peer_query_caps (pad, NULL);
  fail_unless (caps != NULL);

  expected_caps = gst_caps_from_string (expected);
  fail_unless (expected_caps != NULL);

  check_caps_identical (caps, expected_caps, name);

  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);
}

GST_START_TEST (test_caps_negotiation)
{
  GstElement *videorate;
  GstCaps *caps;
  TestInfo *test = &caps_negotiation_tests[__i__];

  videorate = setup_videorate_full (&srctemplate, &sinktemplate);

  caps = gst_caps_from_string (test->caps);

  g_object_set_data_full (G_OBJECT (mysrcpad), "caps",
      gst_caps_ref (caps), (GDestroyNotify) gst_caps_unref);

  g_object_set_data_full (G_OBJECT (mysinkpad), "caps",
      gst_caps_ref (caps), (GDestroyNotify) gst_caps_unref);
  gst_caps_unref (caps);

  g_object_set (videorate, "drop-only", test->drop_only, NULL);
  if (test->max_rate != 0)
    g_object_set (videorate, "max-rate", test->max_rate, NULL);

  gst_pad_set_query_function (mysrcpad, _query_function);
  gst_pad_set_query_function (mysinkpad, _query_function);

  check_peer_caps (mysrcpad, test->expected_sink_caps, "sink");
  check_peer_caps (mysinkpad, test->expected_src_caps, "src");

  cleanup_videorate (videorate);
}

GST_END_TEST;

static void
videorate_send_buffers (GstElement * videorate,
    const gchar * pre_push_caps, const gchar * post_push_caps)
{
  GstCaps *caps, *expected_caps;
  GstBuffer *first;
  GstBuffer *second;
  GstBuffer *third;

  caps = gst_pad_get_current_caps (mysinkpad);
  expected_caps = gst_caps_from_string (pre_push_caps);
  gst_check_caps_equal (caps, expected_caps);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

  GST_DEBUG ("pushing first buffer");
  first = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (first, 0, 0, 4);
  GST_BUFFER_TIMESTAMP (first) = 0;
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND / 25;
  gst_buffer_memset (second, 0, 0, 4);

  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);

  /* third buffer with new size */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND / 25;
  gst_buffer_memset (third, 0, 0, 4);

  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);

  caps = gst_pad_get_current_caps (mysinkpad);
  expected_caps = gst_caps_from_string (post_push_caps);
  gst_check_caps_equal (caps, expected_caps);
  gst_caps_unref (caps);
  gst_caps_unref (expected_caps);

}

GST_START_TEST (test_fixed_framerate)
{
  GstElement *videorate;
  GstCaps *caps;

  /* 1) if upstream caps contain a non-0/1 framerate, we should use that and pass
   *    it on downstream (if possible; otherwise fixate_to_nearest)
   */
  videorate = setup_videorate_full (&srctemplate, &sinktemplate);

  caps = gst_caps_from_string ("video/x-raw,framerate=25/1");
  ASSERT_SET_STATE (videorate, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  videorate_send_buffers (videorate, "video/x-raw,framerate=25/1",
      "video/x-raw,framerate=25/1");
  cleanup_videorate (videorate);

  /* 2) if upstream framerate is 0/1 and downstream doesn't force a particular
   *    framerate, we try to guess based on buffer intervals and use that as output
   *    framerate */
  videorate = setup_videorate_full (&srctemplate, &sinktemplate);
  ASSERT_SET_STATE (videorate, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  caps = gst_caps_from_string ("video/x-raw,framerate=0/1");
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  videorate_send_buffers (videorate, "video/x-raw,framerate=0/1",
      "video/x-raw,framerate=25/1");
  cleanup_videorate (videorate);

  /* 3) if downstream force variable framerate, do that */
  videorate =
      setup_videorate_full (&srctemplate, &force_variable_rate_template);
  ASSERT_SET_STATE (videorate, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  caps = gst_caps_from_string ("video/x-raw,framerate=0/1");
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  videorate_send_buffers (videorate, "video/x-raw,framerate=0/1",
      "video/x-raw,framerate=0/1");
  cleanup_videorate (videorate);

  /* 4) if downstream force variable framerate and drop-only is TRUE */
  videorate =
      setup_videorate_full (&srctemplate, &force_variable_rate_template);
  g_object_set (videorate, "drop-only", TRUE, NULL);
  ASSERT_SET_STATE (videorate, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  caps = gst_caps_from_string ("video/x-raw,framerate=0/1");
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  videorate_send_buffers (videorate, "video/x-raw,framerate=0/1",
      "video/x-raw,framerate=0/1");
  cleanup_videorate (videorate);
}

GST_END_TEST;

GST_START_TEST (test_variable_framerate_renegotiation)
{
  GstElement *videorate;
  GstCaps *caps;
  GstCaps *allowed;

  videorate = setup_videorate_full (&srctemplate, &sinktemplate);
  ASSERT_SET_STATE (videorate, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS);
  caps = gst_caps_from_string ("video/x-raw,framerate=0/1");
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  videorate_send_buffers (videorate, "video/x-raw,framerate=0/1",
      "video/x-raw,framerate=25/1");

  /* framerate=0/1 must still be allowed to be configured on
   * the upstream side of videorate */
  allowed = gst_pad_get_allowed_caps (mysrcpad);
  fail_unless (gst_caps_is_subset (caps, allowed) == TRUE);

  gst_caps_unref (allowed);
  gst_caps_unref (caps);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* Rate tests info */
typedef struct
{
  gdouble rate;
  guint64 expected_in, expected_out, expected_drop, expected_dup;
  gint current_buf;
  GstClockTime expected_ts;
} RateInfo;

static RateInfo rate_tests[] = {
  {
        .rate = 1.0,
        .expected_in = 34,
        .expected_out = 25,
        .expected_drop = 8,
        .expected_dup = 0,
        .current_buf = 0,
      .expected_ts = 0},
  {
        .rate = 0.5,
        .expected_in = 34,
        .expected_out = 50,
        .expected_drop = 0,
        .expected_dup = 17,
        .current_buf = 0,
      .expected_ts = 0},
  {
        .rate = 2.0,
        .expected_in = 34,
        .expected_out = 13,
        .expected_drop = 20,
        .expected_dup = 0,
        .current_buf = 0,
      .expected_ts = 0},
};

static GstPadProbeReturn
listen_outbuffer_ts (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstBuffer *buffer;
  guint64 buf_ts;
  RateInfo *test = (RateInfo *) user_data;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  buf_ts = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG ("Probed %d outbuf. ts : %" GST_TIME_FORMAT
      ", expected : %" GST_TIME_FORMAT, test->current_buf,
      GST_TIME_ARGS (buf_ts), GST_TIME_ARGS (test->expected_ts));
  fail_unless_equals_uint64 (buf_ts, test->expected_ts);

  /* Next expected timestamp with fps 25/1 */
  test->expected_ts += 40000000;
  test->current_buf += 1;

  fail_if (test->current_buf > test->expected_out);

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_rate)
{
  GstElement *videorate;
  RateInfo *test = &rate_tests[__i__];
  GstClockTime ts;
  GstBuffer *buf;
  GstCaps *caps;
  gulong probe;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  probe = gst_pad_add_probe (mysinkpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) listen_outbuffer_ts, test, NULL);

  buf = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buf, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (buf, "inbuffer", 1);

  /* Setting rate */
  g_object_set (videorate, "rate", test->rate, NULL);

  /* Push 1 second of buffers */
  for (ts = 0; ts < 1 * GST_SECOND; ts += GST_SECOND / 33) {
    GstBuffer *inbuf;

    inbuf = gst_buffer_copy (buf);
    GST_BUFFER_TIMESTAMP (inbuf) = ts;

    fail_unless_equals_int (gst_pad_push (mysrcpad, inbuf), GST_FLOW_OK);
  }

  fail_unless_equals_int (g_list_length (buffers), test->expected_out);
  assert_videorate_stats (videorate, "last buffer", test->expected_in,
      test->expected_out, test->expected_drop, test->expected_dup);

  /* cleanup */
  gst_pad_remove_probe (mysinkpad, probe);
  cleanup_videorate (videorate);
  gst_buffer_unref (buf);
}

GST_END_TEST;

/* Probing the pad to force a fake upstream duration */
static GstPadProbeReturn
listen_sink_query_duration (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstQuery *query;
  gint64 *duration = (gint64 *) user_data;

  query = gst_pad_probe_info_get_query (info);

  if (GST_QUERY_TYPE (query) == GST_QUERY_DURATION) {
    gst_query_set_duration (query, GST_FORMAT_TIME, *duration);
    return GST_PAD_PROBE_HANDLED;
  }
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_query_duration)
{
  GstElement *videorate;
  gulong probe_sink;
  gint64 duration;
  GstQuery *query;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  probe_sink =
      gst_pad_add_probe (mysrcpad,
      GST_PAD_PROBE_TYPE_QUERY_UPSTREAM,
      (GstPadProbeCallback) listen_sink_query_duration, &duration, NULL);

  query = gst_query_new_duration (GST_FORMAT_TIME);
  duration = GST_CLOCK_TIME_NONE;
  gst_pad_peer_query (mysinkpad, query);
  gst_query_parse_duration (query, NULL, &duration);
  fail_unless_equals_uint64 (duration, GST_CLOCK_TIME_NONE);

  /* Setting fake upstream duration to 1 second */
  duration = GST_SECOND;

  /* Setting rate to 2.0 */
  g_object_set (videorate, "rate", 2.0, NULL);

  gst_pad_peer_query (mysinkpad, query);
  gst_query_parse_duration (query, NULL, &duration);
  fail_unless_equals_uint64 (duration, 0.5 * GST_SECOND);

  /* cleanup */
  gst_query_unref (query);
  gst_pad_remove_probe (mysrcpad, probe_sink);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* Position tests info */
typedef struct
{
  gdouble rate;
} PositionInfo;

static PositionInfo position_tests[] = {
  {
      .rate = 1.0},
  {
      .rate = 0.5},
  {
      .rate = 2.0},
  {
      .rate = 1.7},
};

GST_START_TEST (test_query_position)
{
  GstElement *videorate;
  PositionInfo *test = &position_tests[__i__];
  GstClockTime ts;
  GstBuffer *buf;
  GstCaps *caps;
  gint64 position, expected_position = 0;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buf = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buf, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (buf, "inbuffer", 1);

  /* Push a few buffers */
  g_object_set (videorate, "rate", test->rate, NULL);
  for (ts = 0; ts < GST_SECOND; ts += GST_SECOND / 20) {
    GstBuffer *inbuf;

    inbuf = gst_buffer_copy (buf);
    GST_BUFFER_TIMESTAMP (inbuf) = ts;

    fail_unless_equals_int (gst_pad_push (mysrcpad, inbuf), GST_FLOW_OK);

    expected_position = ts / test->rate;
    gst_element_query_position (videorate, GST_FORMAT_TIME, &position);
    GST_DEBUG_OBJECT (NULL,
        "pushed buffer %" GST_TIME_FORMAT ", queried pos: %" GST_TIME_FORMAT
        ", expected pos: %" GST_TIME_FORMAT, GST_TIME_ARGS (ts),
        GST_TIME_ARGS (position), GST_TIME_ARGS (expected_position));
    fail_unless_equals_uint64 (position, expected_position);
  }

  /* cleanup */
  cleanup_videorate (videorate);
  gst_buffer_unref (buf);
}

GST_END_TEST;

/* frames at 1, 0, 2 -> second one should be ignored */
GST_START_TEST (test_nopts_in_middle)
{
  GstElement *videorate;
  GstBuffer *first, *second;
  GstCaps *caps;

  videorate =
      setup_videorate_full (&srctemplate, &force_variable_rate_template);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = GST_SECOND;

  gst_buffer_memset (first, 0, 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 0, 1, NULL);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "second", 1, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (second, 0, 0, 4);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_ERROR);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);


  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* test segment update with start/time update */
GST_START_TEST (test_segment_base_nonzero)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  /*
     GList *l;
     GstClockTime next_ts = 0;
     guint n = 1;
   */

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  g_object_set (videorate, "skip-to-first", TRUE, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string ("video/x-raw, "
      "width = (int) 320, "
      "height = (int) 240, "
      "framerate = (fraction) 30000/1001 , " "format = (string) I420");
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Send first closed segment [0, -1] */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = ((1000 * 60 + 2) * 60 + 20) * GST_SECOND + 560636518;
  segment.time = (2 * 60 + 20) * GST_SECOND + 560636518;
  segment.position = segment.start;
  segment.base = segment.time;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* first buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) =
      ((1000 * 60 + 2) * 60 + 20) * GST_SECOND + 543953185;
  gst_buffer_memset (buf, 0, 1, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_ref (buf);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) =
      ((1000 * 60 + 2) * 60 + 20) * GST_SECOND + 577319851;
  gst_buffer_memset (buf, 0, 2, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 1);
  assert_videorate_stats (videorate, "second", 2, 1, 0, 0);

  /* push EOS to drain out all buffers */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  fail_unless_equals_int (g_list_length (buffers), 2);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* test segment update with start/time update */
GST_START_TEST (test_segment_update_start_advance)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  GstClockTime next_ts = 0;
  guint n = 1;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Send first segment [0, -1] */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0 * GST_SECOND;
  segment.time = 0 * GST_SECOND;
  segment.position = 0 * GST_SECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* first buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 0;
  gst_buffer_memset (buf, 0, 1, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_ref (buf);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 2, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 2);
  assert_videorate_stats (videorate, "second", 2, 2, 0, 0);

  /* third buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 2 * 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 3, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "third", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "third", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 3);
  assert_videorate_stats (videorate, "third", 3, 3, 0, 0);

  /* should have the first 3 buffers here */
  for (l = buffers; l; l = l->next) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (buf, 0), n);

    next_ts += GST_SECOND / 25;
    n += 1;
  }
  gst_check_drop_buffers ();
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* Send a new segment [30, -1] with base 0, running time restarts */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 30 * GST_SECOND;
  segment.time = 30 * GST_SECOND;
  segment.position = 30 * GST_SECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  /* third buffer was already output, nothing left to push */
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* fourth buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 30 * GST_SECOND + 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 4, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "fourth", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "fourth", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "fourth", 4, 3, 0, 0);

  /* fifth buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 30 * GST_SECOND + 2 * 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 5, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "fifth", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "fifth", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 3);
  assert_videorate_stats (videorate, "fifth", 5, 6, 0, 1);

  /* Output restarts at segment start and buffer 4 fills gap before its PTS */
  next_ts = 30 * GST_SECOND;
  for (l = buffers; l; l = l->next) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);

    if (n < 6) {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 4);
    } else {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 5);
    }

    next_ts += GST_SECOND / 25;
    n += 1;
  }
  fail_unless_equals_int (n, 7);
  gst_check_drop_buffers ();
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* Send a new segment [60, -1] with base 80ms, running time continues */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 60 * GST_SECOND;
  segment.time = 60 * GST_SECOND;
  segment.position = 60 * GST_SECOND;
  segment.base = 80 * GST_MSECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 6 and 7 */
  for (n = 6; n <= 7; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_TIMESTAMP (buf) = 60 * GST_SECOND + (n - 5) * 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  fail_unless_equals_int (g_list_length (buffers), 2);
  assert_videorate_stats (videorate, "seventh", 7, 8, 0, 1);

  next_ts = 60 * GST_SECOND + 40 * GST_MSECOND;
  for (l = buffers, n = 6; l; l = l->next, n++) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_int (buffer_get_byte (buf, 0), n);

    next_ts += GST_SECOND / 25;
  }
  gst_check_drop_buffers ();

  /* Send a new segment [10, -1] with base 160ms, running time continues */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 10 * GST_SECOND;
  segment.time = 10 * GST_SECOND;
  segment.position = 10 * GST_SECOND;
  segment.base = 160 * GST_MSECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 8 and 9 */
  for (n = 8; n <= 9; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_TIMESTAMP (buf) = 10 * GST_SECOND + (n - 7) * 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  fail_unless_equals_int (g_list_length (buffers), 2);
  assert_videorate_stats (videorate, "ninth", 9, 10, 0, 1);

  next_ts = 10 * GST_SECOND + 40 * GST_MSECOND;
  for (l = buffers, n = 8; l; l = l->next, n++) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_int (buffer_get_byte (buf, 0), n);

    next_ts += GST_SECOND / 25;
  }
  gst_check_drop_buffers ();

  /* Send a new segment [5, -1] with base 0, running time goes back */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 5 * GST_SECOND;
  segment.time = 5 * GST_SECOND;
  segment.position = 5 * GST_SECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 10 and 11 */
  for (n = 10; n <= 11; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_TIMESTAMP (buf) = 5 * GST_SECOND + (n - 9) * 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  fail_unless_equals_int (g_list_length (buffers), 3);
  assert_videorate_stats (videorate, "eleventh", 11, 13, 0, 2);

  /* buffer 11 was already output, nothing left to push */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  fail_unless_equals_int (g_list_length (buffers), 3);

  /* Output restarts at segment start and buffer 10 fills gap before its PTS */
  next_ts = 5 * GST_SECOND;
  for (l = buffers, n = 0; l; l = l->next, n++) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_int (buffer_get_byte (buf, 0), n < 2 ? 10 : 11);

    next_ts += GST_SECOND / 25;
  }
  gst_check_drop_buffers ();

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* test segment update with same segment */
GST_START_TEST (test_segment_update_same)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  GstClockTime next_ts = 0;
  guint n = 1;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Send first closed segment [30, -1] */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 30 * GST_SECOND;
  segment.time = 30 * GST_SECOND;
  segment.position = 30 * GST_SECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* first buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 32 * GST_SECOND;
  gst_buffer_memset (buf, 0, 1, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_ref (buf);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  /* ... and a copy is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (buf, "first", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 0);
  assert_videorate_stats (videorate, "first", 1, 0, 0, 0);

  /* second buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 32 * GST_SECOND + 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 2, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "second", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 52);
  assert_videorate_stats (videorate, "second", 2, 52, 0, 50);

  /* third buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 32 * GST_SECOND + 2 * 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 3, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "third", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "third", 1);
  gst_buffer_unref (buf);
  /* first buffer has been dupped to fill the gap to the 2nd buffer */
  fail_unless_equals_int (g_list_length (buffers), 53);
  assert_videorate_stats (videorate, "second", 3, 53, 0, 50);

  /* have the first 3 buffers here */
  next_ts = 30 * GST_SECOND;
  for (l = buffers; l; l = l->next) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);

    if (n < 52) {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 1);
    } else if (n < 53) {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 2);
    } else {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 3);
    }

    next_ts += GST_SECOND / 25;
    n += 1;
  }
  gst_check_drop_buffers ();
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* Send a new segment [30, -1] that is exactly the same as before */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 30 * GST_SECOND;
  segment.time = 30 * GST_SECOND;
  segment.position = 30 * GST_SECOND;
  segment.stop = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* fourth buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 35 * GST_SECOND + 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 4, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "fourth", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "fourth", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 74);
  assert_videorate_stats (videorate, "fourth", 4, 127, 0, 123);

  /* lot's of new buffers upto 35s 40ms */
  for (l = buffers; l; l = l->next) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);
    if (n < 91) {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 3);
    } else {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 4);
    }

    next_ts += GST_SECOND / 25;
    n += 1;
  }
  gst_check_drop_buffers ();
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* fifth buffer */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 35 * GST_SECOND + 2 * 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 5, 4);
  ASSERT_BUFFER_REFCOUNT (buf, "fifth", 1);
  gst_buffer_ref (buf);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  ASSERT_BUFFER_REFCOUNT (buf, "fifth", 1);
  gst_buffer_unref (buf);
  fail_unless_equals_int (g_list_length (buffers), 1);

  /* fifth buffer was already output, nothing left to push */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  fail_unless_equals_int (g_list_length (buffers), 1);

  assert_videorate_stats (videorate, "fourth", 5, 128, 0, 123);

  for (l = buffers; l; l = l->next) {
    buf = l->data;
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), next_ts);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);

    if (n < 128) {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 4);
    } else {
      fail_unless_equals_int (buffer_get_byte (buf, 0), 5);
    }

    next_ts += GST_SECOND / 25;
    n += 1;
  }
  gst_check_drop_buffers ();
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* Closing a segment while the output framerate is variable and the held
 * buffer has no duration (PTS-only input, e.g. FLV) must not hit the
 * assertion in gst_video_rate_push_buffer(); the held buffer is pushed
 * exactly once instead.
 * https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/5229 */
GST_START_TEST (test_segment_update_vfr_no_duration)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;

  videorate =
      setup_videorate_full (&srctemplate, &force_variable_rate_template);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_VARIABLE_FRAMERATE_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* first buffer with PTS but no duration, held as prevbuf */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 0;
  gst_buffer_memset (buf, 0, 1, 4);
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* second buffer with PTS but no duration; the first buffer goes out with
   * its duration backfilled, this one is now held without a duration */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 40 * GST_MSECOND;
  gst_buffer_memset (buf, 0, 2, 4);
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  buf = buffers->data;
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 0);
  fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);

  /* new segment with continuous running time closes the current one and
   * pushes the held buffer exactly once with an invalid duration */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 10 * GST_SECOND;
  segment.time = 10 * GST_SECOND;
  segment.position = 10 * GST_SECOND;
  segment.base = 80 * GST_MSECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 2);
  buf = g_list_nth_data (buffers, 1);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 40 * GST_MSECOND);
  fail_unless (!GST_BUFFER_DURATION_IS_VALID (buf));
  assert_videorate_stats (videorate, "close segment", 2, 2, 0, 0);

  /* the stream keeps working in the new segment and drains on EOS */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buf) = 10 * GST_SECOND;
  gst_buffer_memset (buf, 0, 3, 4);
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 2);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  fail_unless_equals_int (g_list_length (buffers), 3);
  buf = g_list_nth_data (buffers, 2);
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), 10 * GST_SECOND);
  assert_videorate_stats (videorate, "eos", 3, 3, 0, 0);

  cleanup_videorate (videorate);
}

GST_END_TEST;

GST_START_TEST (test_segment_update_average_period)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;

  /* Create a videorate that outputs at most 1 frame per second. */
  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  g_object_set (videorate, "drop-only", TRUE, "average-period", GST_SECOND,
      NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Segment starts at 0  */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* First buffer at t=30s (pts=30s + segment.base=0s). */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 30 * GST_SECOND;
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);
  assert_videorate_stats (videorate, "first", 1, 1, 0, 0);

  /* Simulate a pad offset of 30s */
  segment.base = 30 * GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* Second buffer at t=32s (pts=2s + segment.base=30s). Videorate used to do
   * current PTS (2s) - previous PTS (30s) = -28s, negative value being always
   * smaller than the target average, the buffer was droped. This verify that it
   * now correctly reset average computation when receiving a new segment. */
  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 2 * GST_SECOND;
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 2);
  assert_videorate_stats (videorate, "second", 2, 2, 0, 0);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

static GstPadProbeReturn
segment_update_probe_cb (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GList **events = user_data;

  *events = g_list_append (*events, gst_event_ref (event));
  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_segment_update)
{
  GstElement *videorate;
  GstCaps *caps;
  GstBuffer *buf;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Note: There is 1 buffer latency, we receive 1st buffer after pushing the
   * 2nd, or after pushing a new segment.
   */

  /* Initial segment is [0, -1], first buffer has PTS=0 */
  gsize frame_size = sizeof (gfloat) * 1;
  buf = gst_buffer_new_and_alloc (frame_size);
  GST_BUFFER_TIMESTAMP (buf) = 0;
  gst_pad_push (mysrcpad, buf);
  fail_unless_equals_int (g_list_length (buffers), 0);

  GList *events = NULL;
  gst_pad_add_probe (mysrcpad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) segment_update_probe_cb, &events, NULL);

  /* Set segment base time to 2nd frame's PTS */
  GstSegment seg;
  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.base = 40 * GST_MSECOND;
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&seg));
  fail_unless_equals_int (g_list_length (events), 1);
  g_clear_list (&events, (GDestroyNotify) gst_event_unref);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_unless_equals_int64 (GST_BUFFER_PTS (buffers->data), 0);
  gst_check_drop_buffers ();

  /* PTS=0 is correct because of the segment base time */
  buf = gst_buffer_new_and_alloc (frame_size);
  GST_BUFFER_TIMESTAMP (buf) = 0;
  gst_pad_push (mysrcpad, buf);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* Push [0, -1] segment again with base time back to 0 */
  gst_segment_init (&seg, GST_FORMAT_TIME);
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&seg));
  fail_unless_equals_int (g_list_length (events), 1);
  g_clear_list (&events, (GDestroyNotify) gst_event_unref);
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_unless_equals_int64 (GST_BUFFER_PTS (buffers->data), 0);
  gst_check_drop_buffers ();

  /* PTS of 3rd frame because base time is back to 0.
   * videorate used to output a buffer with PTS back to segment.start instead of
   * continuing from its current position. */
  buf = gst_buffer_new_and_alloc (frame_size);
  GST_BUFFER_TIMESTAMP (buf) = 2 * 40 * GST_MSECOND;
  gst_pad_push (mysrcpad, buf);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* One last buffer just to verify the one we pushed previously */
  buf = gst_buffer_new_and_alloc (frame_size);
  GST_BUFFER_TIMESTAMP (buf) = 3 * 40 * GST_MSECOND;
  gst_pad_push (mysrcpad, buf);
  fail_unless_equals_int (g_list_length (buffers), 2);
  fail_unless_equals_int64 (GST_BUFFER_PTS (buffers->data),
      2 * 40 * GST_MSECOND);
  gst_check_drop_buffers ();

  gst_element_set_state (videorate, GST_STATE_NULL);

  /* cleanup */
  g_clear_list (&events, (GDestroyNotify) gst_event_unref);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* running time goes back while the end of the last buffer maps past the new
 * segment stop */
GST_START_TEST (test_segment_update_backwards_rt_past_stop)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  guint n;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 10, 1, NULL);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* buffers 1 and 2 */
  for (n = 1; n <= 2; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = (n - 1) * 100 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 100 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  gst_check_drop_buffers ();

  /* Send a new segment [0, 160ms] with base 0, running time goes back */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.stop = 160 * GST_MSECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 3 and 4 */
  for (n = 3; n <= 4; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = (n - 3) * 100 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 100 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* Output restarts at segment start */
  fail_unless_equals_int (g_list_length (buffers), 4);
  for (l = buffers, n = 0; l; l = l->next, n++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), n * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), n < 2 ? 3 : 4);
  }

  cleanup_videorate (videorate);
}

GST_END_TEST;

/* segment update in reverse playback with running time going back */
GST_START_TEST (test_segment_update_reverse_backwards_rt)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  guint n;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  /* closing the old segment doesn't fill it down to its start */
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      (guint64) 0, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* buffers 1 and 2 */
  for (n = 1; n <= 2; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = GST_SECOND - n * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  gst_check_drop_buffers ();

  /* Send a new segment [0, 2] with base 0, running time goes back */
  segment.stop = 2 * GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 3 to 5 */
  for (n = 3; n <= 5; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = 2 * GST_SECOND - (n - 2) * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }

  /* Output restarts at segment stop */
  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, n = 3; l; l = l->next, n++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        2 * GST_SECOND - (n - 2) * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), n);
  }

  cleanup_videorate (videorate);
}

GST_END_TEST;

/* segment updates switching playback direction with continuous running time */
GST_START_TEST (test_segment_update_direction_continuous_rt)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GList *l;
  guint n;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  /* closing the old segment doesn't fill it down to its start */
  g_object_set (videorate, "max-closing-segment-duplication-duration",
      (guint64) 0, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* buffers 1 and 2 */
  for (n = 1; n <= 2; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = GST_SECOND - n * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }
  gst_check_drop_buffers ();

  /* Send a forward segment [0, -1] with base 80ms, running time continues */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.base = 80 * GST_MSECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 3 to 5 */
  for (n = 3; n <= 5; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = (n - 3) * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }

  /* Output continues at segment start with one frame per buffer */
  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, n = 3; l; l = l->next, n++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        (n - 3) * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), n);
  }
  gst_check_drop_buffers ();

  /* Send a reverse segment [0, 2] with base 200ms, running time continues */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = 2 * GST_SECOND;
  segment.base = 200 * GST_MSECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 6 to 8 */
  for (n = 6; n <= 8; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = 2 * GST_SECOND - (n - 5) * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }

  /* Output continues at segment stop with one frame per buffer */
  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, n = 6; l; l = l->next, n++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        2 * GST_SECOND - (n - 5) * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), n);
  }
  gst_check_drop_buffers ();

  /* Send a reverse segment [0, 3] with base 320ms, running time continues */
  segment.stop = 3 * GST_SECOND;
  segment.base = 320 * GST_MSECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* buffers 9 to 11 */
  for (n = 9; n <= 11; n++) {
    buf = gst_buffer_new_and_alloc (4);
    GST_BUFFER_PTS (buf) = 3 * GST_SECOND - (n - 8) * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    gst_buffer_memset (buf, 0, n, 4);
    fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  }

  /* Output continues at segment stop with one frame per buffer */
  fail_unless_equals_int (g_list_length (buffers), 3);
  for (l = buffers, n = 9; l; l = l->next, n++) {
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data),
        3 * GST_SECOND - (n - 8) * 40 * GST_MSECOND);
    fail_unless_equals_int (buffer_get_byte (l->data, 0), n);
  }

  cleanup_videorate (videorate);
}

GST_END_TEST;

static GstFlowReturn
gst_check_drop_only_ref_chain_func (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (pad, "chain_func: received buffer %p", buffer);
  ASSERT_BUFFER_REFCOUNT (buffer, "buf", 1);
  buffers = g_list_append (buffers, buffer);

  g_mutex_lock (&check_mutex);
  g_cond_signal (&check_cond);
  g_mutex_unlock (&check_mutex);

  return GST_FLOW_OK;
}


GST_START_TEST (test_drop_only_ref_count)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;

  /* Create a videorate that outputs at most 1 frame per second. */
  videorate = gst_check_setup_element ("videorate");
  mysrcpad = gst_check_setup_src_pad (videorate, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (videorate, &sinktemplate);
  gst_pad_set_chain_function (mysinkpad, gst_check_drop_only_ref_chain_func);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  g_object_set (videorate, "drop-only", TRUE, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Segment starts at 0  */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  buf = gst_buffer_new_and_alloc (4);
  GST_BUFFER_PTS (buf) = 0;
  fail_unless (gst_pad_push (mysrcpad, buf) == GST_FLOW_OK);
  fail_unless_equals_int (g_list_length (buffers), 1);

  /* cleanup */
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* checks drop-only output of 50fps input with the given new-pref */
static void
check_drop_only (gdouble new_pref)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GList *l;
  gint i;

  videorate = setup_videorate_full (&srctemplate, &downstreamsinktemplate);
  g_object_set (videorate, "drop-only", TRUE, "new-pref", new_pref, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 50, 1, NULL);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  for (i = 0; i < 8; i++) {
    buf = gst_buffer_new_and_alloc (4);
    gst_buffer_memset (buf, 0, i, 4);
    GST_BUFFER_PTS (buf) = i * GST_SECOND / 50;
    GST_BUFFER_DURATION (buf) = GST_SECOND / 50;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }

  /* every second frame of the 50fps input goes out at 25fps */
  fail_unless_equals_int (g_list_length (buffers), 4);
  for (l = buffers, i = 0; l; l = l->next, i += 2) {
    fail_unless_equals_int (buffer_get_byte (l->data, 0), i);
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * GST_SECOND / 50);
  }

  cleanup_videorate (videorate);
}

/* drop-only gives the same output with any new-pref */
GST_START_TEST (test_drop_only_vs_new_pref)
{
  check_drop_only (0.0);
  check_drop_only (0.5);
  check_drop_only (1.0);
}

GST_END_TEST;

static GstPadProbeReturn
check_buffer_size_probe (GstPad * pad, GstPadProbeInfo * info,
    gint * expected_buffer_size)
{
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    if (GST_EVENT_TYPE (ev) == GST_EVENT_CAPS) {
      GstCaps *caps;
      GstStructure *s;
      gint size;

      gst_event_parse_caps (ev, &caps);
      s = gst_caps_get_structure (caps, 0);
      fail_unless (gst_structure_get_int (s, "expected-size", &size));
      *expected_buffer_size = size;
    }
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER (info);
    gint size = (gint) gst_buffer_get_size (buf);

    fail_unless (size == *expected_buffer_size);
  }

  return GST_PAD_PROBE_OK;
}

/* Tests correct caps and buffer sequence when the first buffer after caps change
 * has backward PTS thus the buffer should be dropped */
GST_START_TEST (test_backward_pts_with_caps_change)
{
  GstElement *videorate;
  GstElement *capsfilter;
  GstElement *fakesink;
  GstElement *bin;
  GstBuffer *buf;
  GstCaps *caps;
  GstSegment segment;
  GstPad *sinkpad;
  GstPad *srcpad;
  gint expected_buffer_size = 4;
  GstStateChangeReturn state_ret;
  gboolean ret;
  GstFlowReturn flow_ret;

  /* Setup simple videorate ! capsfilter ! fakesink pipeline */
  videorate = gst_element_factory_make ("videorate", NULL);
  capsfilter = gst_element_factory_make ("capsfilter", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  bin = gst_bin_new (NULL);
  gst_bin_add_many (GST_BIN (bin), videorate, capsfilter, fakesink, NULL);
  gst_element_link_many (videorate, capsfilter, fakesink, NULL);

  caps = gst_caps_from_string ("image/jpeg,framerate=2/1");
  g_object_set (capsfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  sinkpad = gst_element_get_static_pad (videorate, "sink");
  srcpad = gst_element_get_static_pad (capsfilter, "src");

  /* Add probe to validate caps and buffer sequence */
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) check_buffer_size_probe,
      &expected_buffer_size, NULL);

  state_ret = gst_element_set_state (bin, GST_STATE_PLAYING);
  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  /* Push initial mandatory events */
  ret = gst_pad_send_event (sinkpad,
      gst_event_new_stream_start ("test-stream-start"));
  fail_unless (ret);

  caps = gst_caps_from_string ("image/jpeg,framerate=1/1,expected-size=4");
  ret = gst_pad_send_event (sinkpad, gst_event_new_caps (caps));
  fail_unless (ret);
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  ret = gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));
  fail_unless (ret);

  /* Push two buffers for expected-size=4 */
  buf = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buf, 0, 0, 4);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = GST_SECOND;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  buf = gst_buffer_new_and_alloc (4);
  gst_buffer_memset (buf, 0, 0, 4);
  GST_BUFFER_PTS (buf) = GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  /* Change framerate and expected-size */
  caps = gst_caps_from_string ("image/jpeg,framerate=1/2,expected-size=8");
  ret = gst_pad_send_event (sinkpad, gst_event_new_caps (caps));
  fail_unless (ret);
  gst_caps_unref (caps);

  /* Push buffer with backward PTS. videorate should drop this buffer */
  buf = gst_buffer_new_and_alloc (8);
  gst_buffer_memset (buf, 0, 0, 8);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = GST_MSECOND * 500;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  /* Then push buffer with valid PTS */
  buf = gst_buffer_new_and_alloc (8);
  gst_buffer_memset (buf, 0, 0, 8);
  GST_BUFFER_PTS (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_MSECOND * 500;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  /* drain and cleanup */
  gst_pad_send_event (sinkpad, gst_event_new_eos ());

  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (bin);
}

GST_END_TEST;

/* All framerates apart from 25fps can expose rounding errors, e.g. causing a
 * frame to be pushed in the wrong slot */
static const struct
{
  guint numerator, denominator, frames;
} reverse_framerates[] = {
  {25, 1, 25},
  {30, 1, 30},
  {60, 1, 60},
  {30000, 1001, 30},
};

/* With equal input and output rates, preserve each inside picture once in
 * reverse order, including the picture at segment.start. Discard inputs at or
 * beyond segment.stop, and check output content, timing, flags and counters.
 *
 * Run twice per reverse_framerates entry: __i__ / 2 selects the framerate and
 * __i__ % 2 selects drop-out-of-segment (false, then true). Each run covers all
 * duration modes and outside-input prefixes below. */
GST_START_TEST (test_reverse_frames)
{
  enum
  {
    /* All input durations are unknown; infer them for accepted pictures. */
    DURATION_INFERRED,
    /* All inputs carry their known, positive frame duration. */
    DURATION_KNOWN,
    /* Outside inputs have zero duration; inside durations are known. */
    DURATION_ZERO_OUTSIDE,
  };
  const guint fps_index = __i__ / 2;
  const gboolean drop_out_of_segment = __i__ % 2;
  const guint fps_n = reverse_framerates[fps_index].numerator;
  const guint fps_d = reverse_framerates[fps_index].denominator;
  const guint frames = reverse_framerates[fps_index].frames;
  const guint64 time_scale = GST_SECOND * fps_d;
  const GstClockTime stop = gst_util_uint64_scale (frames, time_scale, fps_n);

  /* 0: start inside; 1: start at stop; 2: start beyond stop, then reach stop.
   * Two outside inputs also check that consecutive drops leave the next inside
   * input's duration inference intact. More outside inputs would repeat these
   * boundary cases. */
  for (guint outside_frames = 0; outside_frames < 3; outside_frames++) {
    for (guint duration_mode = DURATION_INFERRED;
        duration_mode <= DURATION_ZERO_OUTSIDE; duration_mode++) {
      GstElement *videorate = setup_videorate ();
      GstCaps *caps = gst_caps_from_string (VIDEO_CAPS_STRING);
      GstSegment segment;
      guint n = 0;
      guint64 in_count, out_count, drop_count, dup_count;

      g_object_set (videorate, "drop-out-of-segment", drop_out_of_segment,
          "silent", FALSE, NULL);
      fail_unless (gst_element_set_state (videorate,
              GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
          "could not set to playing");
      gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
          NULL);
      gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
      gst_caps_unref (caps);

      gst_segment_init (&segment, GST_FORMAT_TIME);
      segment.rate = -1.0;
      segment.stop = stop;
      segment.position = stop;
      fail_unless (gst_pad_push_event (mysrcpad,
              gst_event_new_segment (&segment)));

      for (gint i = frames - 1 + outside_frames; i >= 0; i--) {
        GstBuffer *buf = gst_buffer_new_and_alloc (4);
        GstClockTime pts = gst_util_uint64_scale (i, time_scale, fps_n);
        GstClockTime end = gst_util_uint64_scale (i + 1, time_scale, fps_n);

        gst_buffer_memset (buf, 0, i, 4);
        GST_BUFFER_PTS (buf) = pts;
        GST_BUFFER_DURATION (buf) = duration_mode != DURATION_INFERRED ?
            end - pts : GST_CLOCK_TIME_NONE;
        if (duration_mode == DURATION_ZERO_OUTSIDE && (guint) i >= frames)
          GST_BUFFER_DURATION (buf) = 0;
        fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);

        /* Inputs at or beyond stop are discarded. Hold the first
         * usable input until another one arrives. */
        guint expected = (guint) i >= frames ? 0 : frames - i;

        if (i == frames - 1)
          expected = 0;
        fail_unless_equals_int (g_list_length (buffers), expected);
      }
      fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
      fail_unless_equals_int (g_list_length (buffers), frames);

      /* output slots count back from the segment stop */
      for (GList * l = buffers; l; l = l->next, n++) {
        GstBuffer *buf = l->data;
        GstClockTime offset = gst_util_uint64_scale (n + 1, time_scale, fps_n);
        GstClockTime previous = gst_util_uint64_scale (n, time_scale, fps_n);

        fail_unless_equals_int (buffer_get_byte (buf, 0), frames - 1 - n);
        fail_unless_equals_uint64 (GST_BUFFER_PTS (buf), stop - offset);
        fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf),
            offset - previous);
        fail_if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP));
      }
      g_object_get (videorate, "in", &in_count, "out", &out_count,
          "drop", &drop_count, "duplicate", &dup_count, NULL);
      fail_unless_equals_uint64 (in_count, frames + outside_frames);
      fail_unless_equals_uint64 (out_count, frames);
      fail_unless_equals_uint64 (drop_count, outside_frames);
      fail_unless_equals_uint64 (dup_count, 0);
      cleanup_videorate (videorate);
    }
  }
}

GST_END_TEST;

/* An input at or beyond the reverse segment stop must remain unused when
 * EOS follows. With no inside input, EOS must produce nothing. If an inside
 * input follows, output only that picture and infer its 40 ms duration without
 * using the discarded input's timestamp.
 *
 * Run at stop, stop + 1 ns and stop + 40 ms. For each offset, four independent
 * boolean flags give 16 subcases. Each subcase sends one outside input, then
 * either EOS immediately or one inside input followed by EOS. */
GST_START_TEST (test_reverse_empty_boundary_eos)
{
  const GstClockTime offsets[] = { 0, 1, 40 * GST_MSECOND };

  /* Bits 0 through 3 enumerate all combinations of the flags below. */
  for (guint mode = 0; mode < 16; mode++) {
    GstElement *videorate = setup_videorate ();
    GstCaps *caps = gst_caps_from_string (VIDEO_CAPS_STRING);
    GstSegment segment;
    GstBuffer *buffer;
    guint64 in_count, out_count, drop_count, dup_count;
    /* Bit 0: supply zero duration if true, unknown duration otherwise. */
    gboolean explicit_zero = (mode & 1) != 0;
    /* Bit 1: enable drop-only, which forbids picture duplication. */
    gboolean drop_only = (mode & 2) != 0;
    /* Bit 2: enable filtering of output buffers outside the segment. */
    gboolean drop_out_of_segment = (mode & 4) != 0;
    /* Bit 3: send an inside input before EOS instead of EOS immediately. */
    gboolean following = (mode & 8) != 0;

    g_object_set (videorate, "drop-only", drop_only,
        "drop-out-of-segment", drop_out_of_segment, "silent", FALSE,
        /* In normal mode, limit EOS tail filling to one 40 ms frame. */
        "max-closing-segment-duplication-duration",
        (guint64) (40 * GST_MSECOND), NULL);
    fail_unless (gst_element_set_state (videorate,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
    gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION, 25, 1, NULL);
    gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
    gst_caps_unref (caps);
    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.rate = -1.0;
    segment.stop = GST_SECOND;
    segment.position = segment.stop;
    fail_unless (gst_pad_push_event (mysrcpad,
            gst_event_new_segment (&segment)));
    buffer = gst_buffer_new_and_alloc (4);
    /* Distinct byte markers let us verify that any output uses the inside
     * picture (24), never this outside picture (25). */
    gst_buffer_memset (buffer, 0, 25, 4);
    GST_BUFFER_PTS (buffer) = GST_SECOND + offsets[__i__];
    /* Neither zero nor unknown duration may make this outside input usable. */
    GST_BUFFER_DURATION (buffer) = explicit_zero ? 0 : GST_CLOCK_TIME_NONE;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buffer), GST_FLOW_OK);
    fail_unless_equals_int (g_list_length (buffers), 0);
    if (following) {
      /* This is the only usable input: PTS 960 ms, inferred duration 40 ms. */
      buffer = gst_buffer_new_and_alloc (4);
      gst_buffer_memset (buffer, 0, 24, 4);
      GST_BUFFER_PTS (buffer) = GST_SECOND - 40 * GST_MSECOND;
      GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
      fail_unless_equals_int (gst_pad_push (mysrcpad, buffer), GST_FLOW_OK);
    }
    /* EOS flushes the held inside input in normal mode. In drop-only mode
     * it was already output; EOS must not add anything. */
    fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
    fail_unless_equals_int (g_list_length (buffers), following ? 1 : 0);
    if (following) {
      buffer = buffers->data;
      fail_unless_equals_int (buffer_get_byte (buffer, 0), 24);
      fail_unless_equals_uint64 (GST_BUFFER_PTS (buffer),
          GST_SECOND - 40 * GST_MSECOND);
      fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
          40 * GST_MSECOND);
    }
    g_object_get (videorate, "in", &in_count, "out", &out_count,
        "drop", &drop_count, "duplicate", &dup_count, NULL);
    fail_unless_equals_uint64 (in_count, following ? 2 : 1);
    fail_unless_equals_uint64 (out_count, following ? 1 : 0);
    fail_unless_equals_uint64 (drop_count, 1);
    fail_unless_equals_uint64 (dup_count, 0);
    cleanup_videorate (videorate);
  }
}

GST_END_TEST;

/* Missing input pictures are filled from the first inside picture,
 * independently of the outside picture's known positive duration. */
GST_START_TEST (test_reverse_boundary_gap)
{
  GstElement *videorate = setup_videorate ();
  GstCaps *caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  GstSegment segment;
  /* At 25 fps, each picture ID also determines its PTS in 40 ms steps. */
  const guint input_ids[] = {
    25,                         /* PTS 1000 ms, at segment.stop: discard. */
    22,                         /* PTS 880 ms: first inside picture; inputs at
                                 * 960 and 920 ms are missing. */
    21,                         /* PTS 840 ms: next inside picture. */
  };
  /* Use picture 22 at output PTS 960, 920 and 880 ms, then picture 21 at
   * 840 ms. The outside picture must not fill the missing input pictures. */
  const guint output_ids[] = { 22, 22, 22, 21 };
  guint n = 0;
  guint64 in_count, out_count, drop_count, dup_count;

  g_object_set (videorate, "drop-out-of-segment", (gboolean) __i__, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  segment.position = segment.stop;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  for (guint i = 0; i < G_N_ELEMENTS (input_ids); i++) {
    GstBuffer *buf = gst_buffer_new_and_alloc (4);

    /* Tag each picture so reading one output byte checks that its content
     * remains paired with the expected timestamp. */
    gst_buffer_memset (buf, 0, input_ids[i], 4);
    GST_BUFFER_PTS (buf) = input_ids[i] * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }

  fail_unless_equals_int (g_list_length (buffers), G_N_ELEMENTS (output_ids));
  for (GList * l = buffers; l; l = l->next, n++) {
    GstBuffer *buf = l->data;

    fail_unless_equals_int (buffer_get_byte (buf, 0), output_ids[n]);
    fail_unless_equals_uint64 (GST_BUFFER_PTS (buf),
        GST_SECOND - (n + 1) * 40 * GST_MSECOND);
    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buf), 40 * GST_MSECOND);
  }
  /* Three inputs: discard picture 25, output picture 22 three times and
   * picture 21 once. The two extra uses of picture 22 are duplicates. */
  g_object_get (videorate, "in", &in_count, "out", &out_count,
      "drop", &drop_count, "duplicate", &dup_count, NULL);
  fail_unless_equals_uint64 (in_count, 3);
  fail_unless_equals_uint64 (out_count, 4);
  fail_unless_equals_uint64 (drop_count, 1);
  fail_unless_equals_uint64 (dup_count, 2);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* Drop-only also infers durations from the last accepted reverse timestamp. */
GST_START_TEST (test_reverse_drop_only_timestamp_order)
{
  GstElement *videorate = setup_videorate ();
  GstCaps *caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  GstSegment segment;
  const guint input_ms[] = {
    0,                          /* Known duration: output this picture. */
    0,                          /* Duplicate PTS with unknown duration: drop. */
    40,                         /* PTS increases during reverse playback: drop. */
    0,                          /* Still a duplicate: the rejected 40 ms input
                                 * must not change the duration-inference anchor. */
  };
  const guint input_ids[] = { 0, 99, 98, 97 };
  guint64 in_count, out_count, drop_count, dup_count;

  g_object_set (videorate, "drop-only", TRUE, "silent", FALSE, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = GST_SECOND;
  segment.position = segment.stop;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* After the picture at segment.start, duplicate and increasing reverse
   * timestamps must not cause more output or underflow duration inference. */
  for (guint i = 0; i < G_N_ELEMENTS (input_ms); i++) {
    GstBuffer *buf = gst_buffer_new_and_alloc (4);

    /* Tag each picture so reading one output byte checks that its content
     * remains paired with the expected timestamp. */
    gst_buffer_memset (buf, 0, input_ids[i], 4);
    GST_BUFFER_PTS (buf) = input_ms[i] * GST_MSECOND;
    GST_BUFFER_DURATION (buf) = i == 0 ? 40 * GST_MSECOND : GST_CLOCK_TIME_NONE;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));
  fail_unless_equals_int (g_list_length (buffers), 1);
  fail_unless_equals_int (buffer_get_byte (buffers->data, 0), 0);
  fail_unless_equals_uint64 (GST_BUFFER_PTS ((GstBuffer *) buffers->data), 0);
  fail_unless_equals_uint64 (GST_BUFFER_DURATION ((GstBuffer *) buffers->data),
      40 * GST_MSECOND);
  g_object_get (videorate, "in", &in_count, "out", &out_count,
      "drop", &drop_count, "duplicate", &dup_count, NULL);
  fail_unless_equals_uint64 (in_count, 4);
  fail_unless_equals_uint64 (out_count, 1);
  fail_unless_equals_uint64 (drop_count, 3);
  fail_unless_equals_uint64 (dup_count, 0);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* with new-pref=1.0 a buffer held for variable output keeps its own caps */
GST_START_TEST (test_new_pref_1_caps_rollback_variable_framerate)
{
  GstElement *videorate;
  GstBuffer *buf;
  GstCaps *caps;
  GList *l;
  gint expected_buffer_size = 0;
  gint i;

  videorate =
      setup_videorate_full (&srctemplate, &force_variable_rate_template);
  g_object_set (videorate, "new-pref", 1.0, NULL);
  /* fails if a buffer goes out with caps that don't match its size */
  gst_pad_add_probe (mysinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) check_buffer_size_probe,
      &expected_buffer_size, NULL);
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_caps_set_simple (caps, "expected-size", G_TYPE_INT, 4, NULL);
  gst_check_setup_events (mysrcpad, videorate, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  for (i = 0; i < 2; i++) {
    buf = gst_buffer_new_and_alloc (4);
    gst_buffer_memset (buf, 0, i, 4);
    GST_BUFFER_PTS (buf) = i * GST_SECOND / 25;
    GST_BUFFER_DURATION (buf) = GST_SECOND / 25;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }

  caps = gst_caps_from_string (VIDEO_CAPS_NEWSIZE_STRING);
  gst_caps_set_simple (caps, "expected-size", G_TYPE_INT, 8, NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);

  for (i = 2; i < 4; i++) {
    buf = gst_buffer_new_and_alloc (8);
    gst_buffer_memset (buf, 0, i, 8);
    GST_BUFFER_PTS (buf) = i * GST_SECOND / 25;
    GST_BUFFER_DURATION (buf) = GST_SECOND / 25;
    fail_unless_equals_int (gst_pad_push (mysrcpad, buf), GST_FLOW_OK);
  }

  /* EOS drains the held last buffer */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 4);
  for (l = buffers, i = 0; l; l = l->next, i++) {
    fail_unless_equals_int (buffer_get_byte (l->data, 0), i);
    fail_unless_equals_uint64 (GST_BUFFER_PTS (l->data), i * GST_SECOND / 25);
  }

  cleanup_videorate (videorate);
}

GST_END_TEST;

static Suite *
videorate_suite (void)
{
  Suite *s = suite_create ("videorate");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_one);
  tcase_add_test (tc_chain, test_more);
  tcase_add_test (tc_chain, test_eos_fill_last_buffer_duration);
  tcase_add_test (tc_chain, test_max_closing_segment_duplication_duration);
  tcase_add_test (tc_chain, test_wrong_order_from_zero);
  tcase_add_test (tc_chain, test_wrong_order);
  tcase_add_test (tc_chain, test_no_framerate);
  tcase_add_test (tc_chain, test_changing_size);
  tcase_add_test (tc_chain, test_non_ok_flow);
  tcase_add_test (tc_chain, test_upstream_caps_nego);
  tcase_add_test (tc_chain, test_selected_caps);
  tcase_add_loop_test (tc_chain, test_caps_negotiation,
      0, G_N_ELEMENTS (caps_negotiation_tests));
  tcase_add_test (tc_chain, test_fixed_framerate);
  tcase_add_test (tc_chain, test_variable_framerate_renegotiation);
  tcase_add_loop_test (tc_chain, test_rate, 0, G_N_ELEMENTS (rate_tests));
  tcase_add_test (tc_chain, test_query_duration);
  tcase_add_test (tc_chain, test_max_duplication_time);
  tcase_add_test (tc_chain, test_max_duplication_time_reverse);
  tcase_add_loop_test (tc_chain, test_query_position, 0,
      G_N_ELEMENTS (position_tests));
  tcase_add_test (tc_chain, test_nopts_in_middle);
  tcase_add_test (tc_chain, test_segment_base_nonzero);
  tcase_add_test (tc_chain, test_segment_update_start_advance);
  tcase_add_test (tc_chain, test_segment_update_same);
  tcase_add_test (tc_chain, test_segment_update_vfr_no_duration);
  tcase_add_test (tc_chain, test_segment_update_average_period);
  tcase_add_test (tc_chain, test_segment_update);
  tcase_add_test (tc_chain, test_segment_update_backwards_rt_past_stop);
  tcase_add_test (tc_chain, test_segment_update_reverse_backwards_rt);
  tcase_add_test (tc_chain, test_segment_update_direction_continuous_rt);
  tcase_add_test (tc_chain, test_drop_only_ref_count);
  tcase_add_test (tc_chain, test_drop_only_vs_new_pref);
  tcase_add_test (tc_chain, test_backward_pts_with_caps_change);
  tcase_add_loop_test (tc_chain, test_reverse_frames, 0,
      2 * G_N_ELEMENTS (reverse_framerates));

  tcase_add_loop_test (tc_chain, test_reverse_empty_boundary_eos, 0, 3);
  tcase_add_loop_test (tc_chain, test_reverse_boundary_gap, 0, 2);
  tcase_add_test (tc_chain, test_reverse_drop_only_timestamp_order);
  tcase_add_test (tc_chain, test_new_pref_1_caps_rollback_variable_framerate);

  return s;
}

GST_CHECK_MAIN (videorate)
