/* GStreamer
 *
 * unit test for queue
 *
 * Copyright (C) <2006> Stefan Kost <ensonic@users.sf.net>
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

static gint overrun_count = 0;
static gint underrun_count = 0;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad, *mysinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void
queue_overrun (GstElement * queue, gpointer user_data)
{
  GST_DEBUG ("queue overrun");
  overrun_count++;
}

static void
queue_underrun (GstElement * queue, gpointer user_data)
{
  GST_DEBUG ("queue underrun");
  g_mutex_lock (check_mutex);
  underrun_count++;
  g_cond_signal (check_cond);
  g_mutex_unlock (check_mutex);
}

static GstElement *
setup_queue (void)
{
  GstElement *queue;

  GST_DEBUG ("setup_queue");

  overrun_count = 0;
  underrun_count = 0;

  queue = gst_check_setup_element ("queue");
  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_signal_connect (queue, "underrun", G_CALLBACK (queue_underrun), NULL);

  return queue;
}

static void
cleanup_queue (GstElement * queue)
{
  GST_DEBUG ("cleanup_queue");

  gst_check_teardown_element (queue);
}

/* set queue size to 2 buffers
 * pull 1 buffer
 * check over/underuns
 */
GST_START_TEST (test_non_leaky_underrun)
{
  GstElement *queue;
  GstBuffer *buffer = NULL;

  queue = setup_queue ();
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);

  GST_DEBUG ("starting");

  g_mutex_lock (check_mutex);
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  g_cond_wait (check_cond, check_mutex);
  g_mutex_unlock (check_mutex);

  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  fail_unless (buffer == NULL);

  GST_DEBUG ("stopping");

  /* cleanup */
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (queue);
  cleanup_queue (queue);
}

GST_END_TEST;

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underuns
 * push 1 more buffer
 * check over/underuns again
 */
GST_START_TEST (test_non_leaky_overrun)
{
  GstElement *queue;
  GstBuffer *buffer1, *buffer2, *buffer3;

  queue = setup_queue ();
  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buffer1 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer2, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer3, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("stopping");

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 0);

  /* cleanup */
  gst_pad_set_active (mysrcpad, FALSE);
  gst_check_teardown_src_pad (queue);
  gst_check_teardown_sink_pad (queue);
  cleanup_queue (queue);
}

GST_END_TEST;

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underuns
 * push 1 more buffer
 * check over/underuns again
 * check which buffer was leaked
 */
GST_START_TEST (test_leaky_upstream)
{
  GstElement *queue;
  GstBuffer *buffer1, *buffer2, *buffer3;
  GstBuffer *buffer;

  queue = setup_queue ();
  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, "leaky", 1, NULL);
  gst_pad_set_active (mysrcpad, TRUE);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buffer1 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer2, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer3, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, gst_buffer_ref (buffer3));

  g_mutex_lock (check_mutex);
  /* start the src-task briefly leak buffer3 */
  gst_pad_set_active (mysinkpad, TRUE);
  g_cond_wait (check_cond, check_mutex);
  g_mutex_unlock (check_mutex);

  gst_pad_set_active (mysinkpad, FALSE);

  GST_DEBUG ("stopping");

  fail_unless (g_list_length (buffers) > 0);
  buffer = g_list_first (buffers)->data;
  fail_unless (buffer == buffer1);
  ASSERT_BUFFER_REFCOUNT (buffer3, "buffer", 1);

  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);

  /* cleanup */
  gst_pad_set_active (mysrcpad, FALSE);
  gst_buffer_unref (buffer3);
  gst_check_teardown_src_pad (queue);
  gst_check_teardown_sink_pad (queue);
  cleanup_queue (queue);
}

GST_END_TEST;

GST_START_TEST (test_leaky_downstream)
{
  GstElement *queue;
  GstBuffer *buffer1, *buffer2, *buffer3;
  GstBuffer *buffer;

  queue = setup_queue ();
  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  g_object_set (G_OBJECT (queue), "leaky", 2, "max-size-buffers", 2, NULL);
  gst_pad_set_active (mysrcpad, TRUE);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  buffer1 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, gst_buffer_ref (buffer1));

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer2, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  ASSERT_BUFFER_REFCOUNT (buffer3, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer3);

  g_mutex_lock (check_mutex);
  /* start the src-task briefly and leak buffer1 */
  gst_pad_set_active (mysinkpad, TRUE);
  g_cond_wait (check_cond, check_mutex);
  g_mutex_unlock (check_mutex);

  gst_pad_set_active (mysinkpad, FALSE);

  GST_DEBUG ("stopping");

  fail_unless (g_list_length (buffers) > 0);
  buffer = g_list_first (buffers)->data;
  fail_unless (buffer == buffer2);
  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);

  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);

  /* cleanup */
  gst_pad_set_active (mysrcpad, FALSE);
  gst_buffer_unref (buffer1);
  gst_check_teardown_src_pad (queue);
  gst_check_teardown_sink_pad (queue);
  cleanup_queue (queue);
}

GST_END_TEST;

/* set queue size to 5 buffers
 * pull 1 buffer
 * check over/underuns
 */
GST_START_TEST (test_time_level)
{
  GstElement *queue;
  GstBuffer *buffer = NULL;
  GstClockTime time;

  queue = setup_queue ();
  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 6, NULL);
  g_object_set (G_OBJECT (queue), "max-size-time", 10 * GST_SECOND, NULL);
  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* push buffer without duration */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  /* level should be 1 seconds because buffer has no duration and starts at 1
   * SECOND (sparse stream). */
  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != GST_SECOND);

  /* second push should set the level to 2 second */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 2 * GST_SECOND);

  /* third push should set the level to 4 seconds, the 1 second diff with the
   * previous buffer (without duration) and the 1 second duration of this
   * buffer. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 4 * GST_SECOND);

  /* fourth push should set the level to 6 seconds, the 2 second diff with the
   * previous buffer, same duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 6 * GST_SECOND);

  /* fifth push should not adjust the level, the timestamp and duration are the
   * same, meaning the previous buffer did not really have a duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 6 * GST_SECOND);

  /* sixth push should adjust the level with 1 second, we now know the
   * previous buffer actually had a duration of 2 SECONDS */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 7 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  /* pushing gives away my reference ... */
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 7 * GST_SECOND);

  GST_DEBUG ("stopping");

  /* cleanup */
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_sink_pad (queue);
  cleanup_queue (queue);
}

GST_END_TEST;

static Suite *
queue_suite (void)
{
  Suite *s = suite_create ("queue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_non_leaky_underrun);
  tcase_add_test (tc_chain, test_non_leaky_overrun);
  tcase_add_test (tc_chain, test_leaky_upstream);
  tcase_add_test (tc_chain, test_leaky_downstream);
  tcase_add_test (tc_chain, test_time_level);

  return s;
}

GST_CHECK_MAIN (queue);
