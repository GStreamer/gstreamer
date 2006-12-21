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

GList *buffers = NULL;
gint overrun_count = 0;
gint underrun_count = 0;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

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
  underrun_count++;
}

GstElement *
setup_queue ()
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

void
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

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* do we need to wait here a little */
  usleep (100);
   /**/ GST_DEBUG ("running");

  fail_unless (overrun_count == 0);
  fail_unless (underrun_count > 0);

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

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  GST_DEBUG ("running");

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


Suite *
queue_suite (void)
{
  Suite *s = suite_create ("queue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_non_leaky_underrun);
  tcase_add_test (tc_chain, test_non_leaky_overrun);

  return s;
}

GST_CHECK_MAIN (queue);
