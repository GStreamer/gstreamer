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

#define UNDERRUN_LOCK() (g_mutex_lock (underrun_mutex))
#define UNDERRUN_UNLOCK() (g_mutex_unlock (underrun_mutex))
#define UNDERRUN_SIGNAL() (g_cond_signal (underrun_cond))
#define UNDERRUN_WAIT() (g_cond_wait (underrun_cond, underrun_mutex))

static GstElement *queue;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad;
static GstPad *mysinkpad;

static gint overrun_count;

static GMutex *underrun_mutex;
static GCond *underrun_cond;
static gint underrun_count;

static GList *events;

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
  UNDERRUN_LOCK ();
  underrun_count++;
  UNDERRUN_SIGNAL ();
  UNDERRUN_UNLOCK ();
}

static gboolean
event_func (GstPad * pad, GstEvent * event)
{
  GST_DEBUG ("%s event", gst_event_type_get_name (GST_EVENT_TYPE (event)));
  events = g_list_append (events, event);

  return TRUE;
}

static void
drop_events (void)
{
  while (events != NULL) {
    gst_event_unref (GST_EVENT (events->data));
    events = g_list_delete_link (events, events);
  }
}

static void
setup (void)
{
  GST_DEBUG ("setup_queue");

  queue = gst_check_setup_element ("queue");
  g_signal_connect (queue, "underrun", G_CALLBACK (queue_underrun), NULL);

  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate, NULL);
  gst_pad_set_active (mysrcpad, TRUE);

  mysinkpad = NULL;

  overrun_count = 0;

  underrun_mutex = g_mutex_new ();
  underrun_cond = g_cond_new ();
  underrun_count = 0;

  events = NULL;
}

static void
cleanup (void)
{
  GST_DEBUG ("cleanup_queue");

  gst_check_drop_buffers ();

  drop_events ();

  g_cond_free (underrun_cond);
  underrun_cond = NULL;
  g_mutex_free (underrun_mutex);
  underrun_mutex = NULL;

  if (mysinkpad != NULL) {
    gst_pad_set_active (mysinkpad, FALSE);
    gst_check_teardown_sink_pad (queue);
  }

  gst_pad_set_active (mysrcpad, FALSE);
  gst_check_teardown_src_pad (queue);

  gst_check_teardown_element (queue);
  queue = NULL;
}

/* setup the sinkpad on a playing queue element. gst_check_setup_sink_pad()
 * does not work in this case since it does not activate the pad before linking
 * it. */
static GstPad *
setup_sink_pad (GstElement * element, GstStaticPadTemplate * tmpl)
{
  GstPad *srcpad;
  GstPad *sinkpad;

  sinkpad = gst_pad_new_from_static_template (tmpl, "sink");
  fail_if (sinkpad == NULL);
  srcpad = gst_element_get_static_pad (element, "src");
  fail_if (srcpad == NULL);
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);
  gst_pad_set_event_function (sinkpad, event_func);
  gst_pad_set_active (sinkpad, TRUE);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);

  return sinkpad;
}

/* set queue size to 2 buffers
 * pull 1 buffer
 * check over/underuns
 */
GST_START_TEST (test_non_leaky_underrun)
{
  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate, NULL);
  gst_pad_set_active (mysinkpad, TRUE);

  GST_DEBUG ("starting");

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

static void
queue_overrun_link_and_activate (GstElement * queue, gpointer user_data)
{
  GST_DEBUG ("queue overrun");
  overrun_count++;

  /* link the src pad of the queue to make it dequeue buffers */
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
}

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underuns
 * push 1 more buffer
 * check over/underuns again
 */
GST_START_TEST (test_non_leaky_overrun)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;

  g_signal_connect (queue, "overrun",
      G_CALLBACK (queue_overrun_link_and_activate), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  /* lock the check_mutex to block the first buffer pushed to mysinkpad */
  g_mutex_lock (check_mutex);
  /* the next call to gst_pad_push will emit the overrun signal. The signal
   * handler queue_overrun_link_and_activate() (above) increases overrun_count,
   * activates and links mysinkpad. The queue task then dequeues a buffer and
   * gst_pad_push() will return. */
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3rd");
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 0);

  /* now let the queue push all buffers */
  while (g_list_length (buffers) < 3) {
    g_cond_wait (check_cond, check_mutex);
  }
  g_mutex_unlock (check_mutex);

  fail_unless (overrun_count == 1);
  /* make sure we get the underrun signal before we check underrun_count */
  UNDERRUN_LOCK ();
  while (underrun_count < 1) {
    UNDERRUN_WAIT ();
  }
  UNDERRUN_UNLOCK ();
  fail_unless (underrun_count == 1);

  buffer = g_list_nth (buffers, 0)->data;
  fail_unless (buffer == buffer1);

  buffer = g_list_nth (buffers, 1)->data;
  fail_unless (buffer == buffer2);

  buffer = g_list_nth (buffers, 2)->data;
  fail_unless (buffer == buffer3);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
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
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, "leaky", 1, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  /* buffer3 will be leaked, keep a ref so refcount can be checked below */
  gst_buffer_ref (buffer3);
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3rd");
  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 0);

  /* wait for underrun and check that we got buffer1 and buffer2 only */
  UNDERRUN_LOCK ();
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 1);

  fail_unless (g_list_length (buffers) == 2);

  buffer = g_list_nth (buffers, 0)->data;
  fail_unless (buffer == buffer1);

  buffer = g_list_nth (buffers, 1)->data;
  fail_unless (buffer == buffer2);

  ASSERT_BUFFER_REFCOUNT (buffer3, "buffer", 1);
  gst_buffer_unref (buffer3);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underuns
 * push 1 more buffer
 * check over/underuns again
 * check which buffer was leaked
 */
GST_START_TEST (test_leaky_downstream)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, "leaky", 2, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* buffer1 will be leaked, keep a ref so refcount can be checked below */
  gst_buffer_ref (buffer1);
  /* pushing gives away one reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer3 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3rd");
  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 0);

  /* wait for underrun and check that we got buffer1 and buffer2 only */
  UNDERRUN_LOCK ();
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 1);

  fail_unless (g_list_length (buffers) == 2);

  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);
  gst_buffer_unref (buffer1);

  buffer = g_list_nth (buffers, 0)->data;
  fail_unless (buffer == buffer2);

  buffer = g_list_nth (buffers, 1)->data;
  fail_unless (buffer == buffer3);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

/* set queue size to 6 buffers and 7 seconds
 * push 7 buffers with and without duration
 * check current-level-time
 */
GST_START_TEST (test_time_level)
{
  GstBuffer *buffer = NULL;
  GstClockTime time;

  g_signal_connect (queue, "overrun",
      G_CALLBACK (queue_overrun_link_and_activate), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 6, NULL);
  g_object_set (G_OBJECT (queue), "max-size-time", 7 * GST_SECOND, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  /* push buffer without duration */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = GST_SECOND;
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer);

  /* level should be 1 seconds because buffer has no duration and starts at 1
   * SECOND (sparse stream). */
  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != GST_SECOND);

  /* second push should set the level to 2 second */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
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
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 4 * GST_SECOND);

  /* fourth push should set the level to 6 seconds, the 2 second diff with the
   * previous buffer, same duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 6 * GST_SECOND);

  /* fifth push should not adjust the level, the timestamp and duration are the
   * same, meaning the previous buffer did not really have a duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 6 * GST_SECOND);

  /* sixth push should adjust the level with 1 second, we now know the
   * previous buffer actually had a duration of 2 SECONDS */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 7 * GST_SECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 7 * GST_SECOND);

  /* eighth push should cause overrun */
  fail_unless (overrun_count == 0);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 8 * GST_SECOND;
  /* the next call to gst_pad_push will emit the overrun signal. The signal
   * handler queue_overrun_link_and_activate() (above) increases overrun_count,
   * activates and links mysinkpad. The queue task then dequeues a buffer and
   * gst_pad_push() will return. */
  gst_pad_push (mysrcpad, buffer);

  fail_unless (overrun_count == 1);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

GST_START_TEST (test_time_level_task_not_started)
{
  GstEvent *event;
  GstClockTime time;

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_TIME,
      1 * GST_SECOND, 5 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 0 * GST_SECOND);

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
      1 * GST_SECOND, 5 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 4 * GST_SECOND);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

static gboolean
event_equals_newsegment (GstEvent * event, gboolean update, gdouble rate,
    GstFormat format, gint64 start, gint64 stop, gint64 position)
{
  gboolean ns_update;
  gdouble ns_rate;
  GstFormat ns_format;
  gint64 ns_start;
  gint64 ns_stop;
  gint64 ns_position;

  if (GST_EVENT_TYPE (event) != GST_EVENT_NEWSEGMENT) {
    return FALSE;
  }

  gst_event_parse_new_segment (event, &ns_update, &ns_rate, &ns_format,
      &ns_start, &ns_stop, &ns_position);

  GST_DEBUG ("update %d, rate %lf, format %s, start %" GST_TIME_FORMAT
      ", stop %" GST_TIME_FORMAT ", position %" GST_TIME_FORMAT, ns_update,
      ns_rate, gst_format_get_name (ns_format), GST_TIME_ARGS (ns_start),
      GST_TIME_ARGS (ns_stop), GST_TIME_ARGS (ns_position));

  return (ns_update == update && ns_rate == rate && ns_format == format &&
      ns_start == start && ns_stop == stop && ns_position == position);
}

GST_START_TEST (test_newsegment)
{
  GstEvent *event;
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 1, "max-size-time",
      (guint64) 0, "leaky", 2, NULL);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  event = gst_event_new_new_segment (FALSE, 2.0, GST_FORMAT_TIME, 0,
      2 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  GST_DEBUG ("added 1st newsegment");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0,
      3 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  GST_DEBUG ("added 2nd newsegment");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
      4 * GST_SECOND, 5 * GST_SECOND, 4 * GST_SECOND);
  gst_pad_push_event (mysrcpad, event);

  GST_DEBUG ("added 3rd newsegment");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* buffer1 will be leaked, keep a ref so refcount can be checked below */
  gst_buffer_ref (buffer1);
  /* pushing gives away one reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st buffer");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  buffer2 = gst_buffer_new_and_alloc (4);
  /* next push will cause overrun and leak all newsegment events and buffer1 */
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd buffer");
  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 0);

  /* wait for underrun and check that we got one accumulated newsegment event,
   * one real newsegment event and buffer2 only */
  UNDERRUN_LOCK ();
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 1);

  fail_unless (g_list_length (events) == 2);

  event = g_list_nth (events, 0)->data;
  fail_unless (event_equals_newsegment (event, FALSE, 1.0, GST_FORMAT_TIME, 0,
          4 * GST_SECOND, 0));

  event = g_list_nth (events, 1)->data;
  fail_unless (event_equals_newsegment (event, FALSE, 1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 5 * GST_SECOND, 4 * GST_SECOND));

  fail_unless (g_list_length (buffers) == 1);

  ASSERT_BUFFER_REFCOUNT (buffer1, "buffer", 1);
  gst_buffer_unref (buffer1);

  buffer = g_list_nth (buffers, 0)->data;
  fail_unless (buffer == buffer2);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

static Suite *
queue_suite (void)
{
  Suite *s = suite_create ("queue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, cleanup);
  tcase_add_test (tc_chain, test_non_leaky_underrun);
  tcase_add_test (tc_chain, test_non_leaky_overrun);
  tcase_add_test (tc_chain, test_leaky_upstream);
  tcase_add_test (tc_chain, test_leaky_downstream);
  tcase_add_test (tc_chain, test_time_level);
  tcase_add_test (tc_chain, test_time_level_task_not_started);
  tcase_add_test (tc_chain, test_newsegment);

  return s;
}

GST_CHECK_MAIN (queue);
