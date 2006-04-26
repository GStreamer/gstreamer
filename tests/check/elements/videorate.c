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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>

GList *buffers = NULL;
gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;


#define VIDEO_CAPS_TEMPLATE_STRING     \
    "video/x-raw-yuv"

#define VIDEO_CAPS_STRING               \
    "video/x-raw-yuv, "                 \
    "width = (int) 320, "               \
    "height = (int) 240, "              \
    "framerate = (fraction) 25/1 , "    \
    "format = (fourcc) I420"

#define VIDEO_CAPS_NO_FRAMERATE_STRING  \
    "video/x-raw-yuv, "                 \
    "width = (int) 320, "               \
    "height = (int) 240, "              \
    "format = (fourcc) I420"


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_TEMPLATE_STRING)
    );

void
assert_videorate_stats (GstElement * videorate, gchar * reason,
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

GstElement *
setup_videorate ()
{
  GstElement *videorate;

  GST_DEBUG ("setup_videorate");
  videorate = gst_check_setup_element ("videorate");
  mysrcpad = gst_check_setup_src_pad (videorate, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (videorate, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return videorate;
}

void
cleanup_videorate (GstElement * videorate)
{
  GST_DEBUG ("cleanup_videorate");

  g_list_foreach (buffers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (buffers);
  buffers = NULL;

  gst_element_set_state (videorate, GST_STATE_NULL);
  gst_element_get_state (videorate, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_check_teardown_src_pad (videorate);
  gst_check_teardown_sink_pad (videorate);
  gst_check_teardown_element (videorate);
}

GST_START_TEST (test_one)
{
  GstElement *videorate;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_and_alloc (4);
  memset (GST_BUFFER_DATA (inbuffer), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (inbuffer, caps);
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

/* frames at 1, 0, 2 -> second one should be ignored */
GST_START_TEST (test_wrong_order_from_zero)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *outbuffer;
  GstCaps *caps;
  guint64 in, out, dropped, duplicated;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "start", 0, 0, 0, 0);

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = GST_SECOND;
  memset (GST_BUFFER_DATA (first), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (first, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 2);
  fail_unless_equals_int (g_list_length (buffers), 0);
  /* FIXME: in is not counted properly, should be 1 */
  assert_videorate_stats (videorate, "first", 0, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = 0;
  memset (GST_BUFFER_DATA (second), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (second, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and it got dropped because it was before the first */
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  fail_unless_equals_int (g_list_length (buffers), 0);

  /* ... and the first one is now dropped */
  /* FIXME: still a bug with in, needs to be 2 */
  assert_videorate_stats (videorate, "second", 1, 0, 1, 0);
  ASSERT_BUFFER_REFCOUNT (first, "first", 2);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND;
  memset (GST_BUFFER_DATA (third), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (third, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 2);

  /* and now the first one should be pushed once and dupped 24 + 13 times, to
   * reach the half point between 1 s (first) and 2 s (third) */
  fail_unless_equals_int (g_list_length (buffers), 38);
  ASSERT_BUFFER_REFCOUNT (first, "first", 39);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  ASSERT_BUFFER_REFCOUNT (third, "third", 2);
  /* FIXME: you guessed it ... */
  assert_videorate_stats (videorate, "third", 2, 38, 1, 37);

  /* verify last buffer */
  outbuffer = g_list_last (buffers)->data;
  fail_unless (GST_IS_BUFFER (outbuffer));
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer),
      GST_SECOND * 37 / 25);

  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  cleanup_videorate (videorate);
}

GST_END_TEST;

/* send frames with 0, 1, 2, 0 seconds */
GST_START_TEST (test_wrong_order)
{
  GstElement *videorate;
  GstBuffer *first, *second, *third, *fourth, *outbuffer;
  GstCaps *caps;
  guint64 in, out, dropped, duplicated;

  videorate = setup_videorate ();
  fail_unless (gst_element_set_state (videorate,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  assert_videorate_stats (videorate, "start", 0, 0, 0, 0);

  /* first buffer */
  first = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (first) = 0;
  memset (GST_BUFFER_DATA (first), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (first, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (first, "first", 1);
  gst_buffer_ref (first);

  GST_DEBUG ("pushing first buffer");
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, first) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (first, "first", 2);
  fail_unless_equals_int (g_list_length (buffers), 0);
  /* FIXME: in is not counted properly, should be 1 */
  assert_videorate_stats (videorate, "first", 0, 0, 0, 0);

  /* second buffer */
  second = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (second) = GST_SECOND;
  memset (GST_BUFFER_DATA (second), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (second, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (second, "second", 1);
  gst_buffer_ref (second);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, second) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (second, "second", 2);
  /* and it created 13 output buffers as copies of the first frame */
  fail_unless_equals_int (g_list_length (buffers), 13);
  /* FIXME: guess */
  assert_videorate_stats (videorate, "second", 1, 13, 0, 12);
  ASSERT_BUFFER_REFCOUNT (first, "first", 14);

  /* third buffer */
  third = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (third) = 2 * GST_SECOND;
  memset (GST_BUFFER_DATA (third), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (third, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (third, "third", 1);
  gst_buffer_ref (third);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, third) == GST_FLOW_OK);
  /* ... and it is now stuck inside videorate */
  ASSERT_BUFFER_REFCOUNT (third, "third", 2);

  /* submitting a frame with 2 seconds triggers output of 25 more frames */
  fail_unless_equals_int (g_list_length (buffers), 38);
  ASSERT_BUFFER_REFCOUNT (first, "first", 14);
  ASSERT_BUFFER_REFCOUNT (second, "second", 26);
  /* three frames submitted; two of them output as is, and 36 duplicated */
  /* FIXME: guess */
  assert_videorate_stats (videorate, "third", 2, 38, 0, 36);

  /* fourth buffer */
  fourth = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (fourth) = 0;
  memset (GST_BUFFER_DATA (fourth), 0, 4);
  caps = gst_caps_from_string (VIDEO_CAPS_STRING);
  gst_buffer_set_caps (fourth, caps);
  gst_caps_unref (caps);
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);
  gst_buffer_ref (fourth);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mysrcpad, fourth) == GST_FLOW_OK);
  /* ... and it is dropped from videorate because 0 < 2 */
  ASSERT_BUFFER_REFCOUNT (fourth, "fourth", 1);

  fail_unless_equals_int (g_list_length (buffers), 38);
  ASSERT_BUFFER_REFCOUNT (first, "first", 14);  /* 13 frames pushed out */
  ASSERT_BUFFER_REFCOUNT (second, "second", 26);
  /* FIXME: guess */
  assert_videorate_stats (videorate, "fourth", 3, 38, 1, 36);

  /* verify last buffer */
  outbuffer = g_list_last (buffers)->data;
  fail_unless (GST_IS_BUFFER (outbuffer));
  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (outbuffer),
      GST_SECOND * 37 / 25);

  /* cleanup */
  gst_buffer_unref (first);
  gst_buffer_unref (second);
  gst_buffer_unref (third);
  gst_buffer_unref (fourth);
  cleanup_videorate (videorate);
}

GST_END_TEST;

Suite *
videorate_suite (void)
{
  Suite *s = suite_create ("videorate");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_wrong_order_from_zero);
  tcase_add_test (tc_chain, test_wrong_order);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = videorate_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
