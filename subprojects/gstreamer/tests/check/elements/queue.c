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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#define UNDERRUN_LOCK() (g_mutex_lock (&underrun_mutex))
#define UNDERRUN_UNLOCK() (g_mutex_unlock (&underrun_mutex))
#define UNDERRUN_SIGNAL() (g_cond_signal (&underrun_cond))
#define UNDERRUN_WAIT() (g_cond_wait (&underrun_cond, &underrun_mutex))

static GstElement *queue;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mysrcpad;
static GstPad *mysinkpad;
static GstPad *qsrcpad;
static gulong probe_id;

static gint overrun_count;

static GMutex underrun_mutex;
static GCond underrun_cond;
static gint underrun_count;

static GMutex events_lock;
static GCond events_cond;
static gint events_count;
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
  overrun_count++;
  GST_DEBUG ("queue overrun %d", overrun_count);
}

static void
queue_underrun (GstElement * queue, gpointer user_data)
{
  UNDERRUN_LOCK ();
  underrun_count++;
  GST_DEBUG ("queue underrun %d", underrun_count);
  UNDERRUN_SIGNAL ();
  UNDERRUN_UNLOCK ();
}

static gboolean
event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_DEBUG ("%s event", GST_EVENT_TYPE_NAME (event));

  g_mutex_lock (&events_lock);

  events = g_list_append (events, event);
  ++events_count;

  g_cond_broadcast (&events_cond);
  g_mutex_unlock (&events_lock);

  return TRUE;
}

static void
block_src (void)
{
  qsrcpad = gst_element_get_static_pad (queue, "src");
  probe_id = gst_pad_add_probe (qsrcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      NULL, NULL, NULL);
}

static void
unblock_src (void)
{
  gst_pad_remove_probe (qsrcpad, probe_id);
  gst_object_unref (qsrcpad);
}

static void
setup (void)
{
  GST_DEBUG ("setup_queue");

  queue = gst_check_setup_element ("queue");
  g_signal_connect (queue, "underrun", G_CALLBACK (queue_underrun), NULL);

  mysrcpad = gst_check_setup_src_pad (queue, &srctemplate);
  gst_pad_set_active (mysrcpad, TRUE);

  mysinkpad = NULL;

  overrun_count = 0;

  underrun_count = 0;


  g_mutex_init (&events_lock);
  g_cond_init (&events_cond);
  events_count = 0;
  events = NULL;
}

static void
cleanup (void)
{
  GST_DEBUG ("cleanup_queue");

  gst_check_drop_buffers ();

  while (events != NULL) {
    gst_event_unref (GST_EVENT (events->data));
    events = g_list_delete_link (events, events);
  }
  events_count = 0;
  g_mutex_clear (&events_lock);
  g_cond_clear (&events_cond);

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
 * check over/underruns
 */
GST_START_TEST (test_non_leaky_underrun)
{
  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);
  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate);
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

  unblock_src ();
}

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underruns
 * push 1 more buffer
 * check over/underruns again
 */
GST_START_TEST (test_non_leaky_overrun)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;
  GstSegment segment;

  g_signal_connect (queue, "overrun",
      G_CALLBACK (queue_overrun_link_and_activate), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, NULL);

  block_src ();

  GST_DEBUG ("starting");

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  fail_unless (underrun_count == 1);
  fail_unless (overrun_count == 0);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer3 = gst_buffer_new_and_alloc (4);
  /* the next call to gst_pad_push will emit the overrun signal. The signal
   * handler queue_overrun_link_and_activate() (above) increases overrun_count,
   * activates and links mysinkpad. The queue task then dequeues a buffer and
   * gst_pad_push() will return. */
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3rd");
  fail_unless (overrun_count == 1);

  /* lock the check_mutex to block the first buffer pushed to mysinkpad */
  g_mutex_lock (&check_mutex);
  /* now let the queue push all buffers */
  while (g_list_length (buffers) < 3) {
    g_cond_wait (&check_cond, &check_mutex);
  }
  g_mutex_unlock (&check_mutex);

  fail_unless (overrun_count == 1);
  /* make sure we get the underrun signal before we check underrun_count */
  UNDERRUN_LOCK ();
  while (underrun_count < 2) {
    UNDERRUN_WAIT ();
  }
  /* we can't check the underrun_count here safely because when adding the 3rd
   * buffer, the queue lock is released to emit the overrun signal and the
   * downstream part can then push and empty the queue and signal an additional
   * underrun */
  /* fail_unless_equals_int (underrun_count, 2); */
  UNDERRUN_UNLOCK ();

  buffer = g_list_nth (buffers, 0)->data;
  fail_unless (buffer == buffer1);

  buffer = g_list_nth (buffers, 1)->data;
  fail_unless (buffer == buffer2);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

/* set queue size to 2 buffers
 * push 2 buffers
 * check over/underruns
 * push 1 more buffer
 * check over/underruns again
 * check which buffer was leaked
 */
GST_START_TEST (test_leaky_upstream)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;
  GstSegment segment;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, "leaky", 1, NULL);

  GST_DEBUG ("starting");

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer3 = gst_buffer_new_and_alloc (4);
  /* buffer4 will be leaked, keep a ref so refcount can be checked below */
  gst_buffer_ref (buffer3);
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3nd");
  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 1);

  /* wait for underrun and check that we got buffer1 and buffer2 only */
  UNDERRUN_LOCK ();
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
  unblock_src ();
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 2);

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
 * check over/underruns
 * push 1 more buffer
 * check over/underruns again
 * check which buffer was leaked
 */
GST_START_TEST (test_leaky_downstream)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstBuffer *buffer3;
  GstBuffer *buffer;
  GstSegment segment;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 2, "leaky", 2, NULL);

  GST_DEBUG ("starting");

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer1 = gst_buffer_new_and_alloc (4);
  /* pushing gives away one reference */
  /* buffer1 will be leaked, keep a ref so refcount can be checked below */
  gst_buffer_ref (buffer1);
  gst_pad_push (mysrcpad, buffer1);

  GST_DEBUG ("added 1st");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  GST_DEBUG ("added 2nd");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 1);

  buffer3 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer3);

  GST_DEBUG ("added 3rd");
  /* it still triggers overrun when leaking */
  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 1);

  /* wait for underrun and check that we got buffer1 and buffer2 only */
  UNDERRUN_LOCK ();
  mysinkpad = setup_sink_pad (queue, &sinktemplate);
  unblock_src ();
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  fail_unless (overrun_count == 1);
  fail_unless (underrun_count == 2);

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
  GstSegment segment;

  g_signal_connect (queue, "overrun",
      G_CALLBACK (queue_overrun_link_and_activate), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 6, NULL);
  g_object_set (G_OBJECT (queue), "max-size-time", 7 * GST_SECOND, NULL);

  GST_DEBUG ("starting");

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  /* push buffer without duration */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = GST_SECOND;
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer);

  /* level should be zero because buffer has no duration */
  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 0);

  /* second push should set the level to 1 second */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 2 * GST_SECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != GST_SECOND);

  /* third push should set the level to 3 seconds, the 1 second diff with the
   * previous buffer (without duration) and the 1 second duration of this
   * buffer. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 3 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 3 * GST_SECOND);

  /* fourth push should set the level to 5 seconds, the 2 second diff with the
   * previous buffer, same duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 5 * GST_SECOND);

  /* fifth push should not adjust the level, the timestamp and duration are the
   * same, meaning the previous buffer did not really have a duration. */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5 * GST_SECOND;
  GST_BUFFER_DURATION (buffer) = 1 * GST_SECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 5 * GST_SECOND);

  /* sixth push should adjust the level with 1 second, we now know the
   * previous buffer actually had a duration of 2 SECONDS */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 7 * GST_SECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 6 * GST_SECOND);

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
  GstSegment segment;

  GST_DEBUG ("starting");

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 1 * GST_SECOND;
  segment.stop = 5 * GST_SECOND;
  segment.time = 0;
  segment.position = 1 * GST_SECOND;

  event = gst_event_new_segment (&segment);
  gst_pad_push_event (mysrcpad, event);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_if (time != 0);

  segment.base = 4 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  gst_pad_push_event (mysrcpad, event);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  GST_DEBUG ("time now %" GST_TIME_FORMAT, GST_TIME_ARGS (time));
  fail_if (time != 0);

  unblock_src ();

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

GST_START_TEST (test_sticky_not_linked)
{
  GstEvent *event;
  GstSegment segment;
  gboolean ret;
  GstFlowReturn flow_ret;

  GST_DEBUG ("starting");

  g_object_set (queue, "max-size-buffers", 1, NULL);

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 1 * GST_SECOND;
  segment.stop = 5 * GST_SECOND;
  segment.time = 0;
  segment.position = 1 * GST_SECOND;

  event = gst_event_new_segment (&segment);
  ret = gst_pad_push_event (mysrcpad, event);
  fail_unless (ret == TRUE);

  /* the first few buffers can return OK as they are queued and gst_queue_loop
   * is woken up, tries to push and sets ->srcresult to NOT_LINKED
   */
  flow_ret = GST_FLOW_OK;
  while (flow_ret != GST_FLOW_NOT_LINKED)
    flow_ret = gst_pad_push (mysrcpad, gst_buffer_new ());

  /* send a new sticky event so that it will be pushed on the next gst_pad_push
   */
  event = gst_event_new_segment (&segment);
  ret = gst_pad_push_event (mysrcpad, event);
  fail_unless (ret == TRUE);

  /* make sure that gst_queue_sink_event doesn't return FALSE if the queue is
   * unlinked, as that would make gst_pad_push return ERROR
   */
  flow_ret = gst_pad_push (mysrcpad, gst_buffer_new ());
  fail_unless_equals_int (flow_ret, GST_FLOW_NOT_LINKED);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

#if 0
static gboolean
event_equals_newsegment (GstEvent * event, gboolean update, gdouble rate,
    GstFormat format, gint64 start, gint64 stop, gint64 position)
{
  gboolean ns_update;
  gdouble ns_rate, ns_arate;
  GstFormat ns_format;
  gint64 ns_start;
  gint64 ns_stop;
  gint64 ns_position;

  if (GST_EVENT_TYPE (event) != GST_EVENT_SEGMENT) {
    return FALSE;
  }

  gst_event_parse_new_segment (event, &ns_update, &ns_rate, &ns_arate,
      &ns_format, &ns_start, &ns_stop, &ns_position);

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

  event = gst_event_new_new_segment (FALSE, 2.0, 1.0, GST_FORMAT_TIME, 0,
      2 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  GST_DEBUG ("added 1st newsegment");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  event = gst_event_new_new_segment (FALSE, 1.0, 1.0, GST_FORMAT_TIME, 0,
      3 * GST_SECOND, 0);
  gst_pad_push_event (mysrcpad, event);

  GST_DEBUG ("added 2nd newsegment");
  fail_unless (overrun_count == 0);
  fail_unless (underrun_count == 0);

  event = gst_event_new_new_segment (FALSE, 1.0, 1.0, GST_FORMAT_TIME,
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
#endif

static gpointer
thread_func (gpointer data)
{
  int i = 0;
  for (i = 0; i < 100; i++) {
    GstCaps *caps;
    GstQuery *query;
    gboolean ok;
    caps = gst_caps_new_any ();
    query = gst_query_new_allocation (caps, FALSE);
    ok = gst_pad_peer_query (mysrcpad, query);
    gst_query_unref (query);
    gst_caps_unref (caps);
    query = NULL;
    caps = NULL;

    if (!ok)
      break;
  }

  return NULL;
}

static gboolean query_func (GstPad * pad, GstObject * parent, GstQuery * query);

static gboolean
query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{

  g_usleep (1000);
  return TRUE;
}

GST_START_TEST (test_queries_while_flushing)
{
  GstEvent *event;
  GThread *thread;
  int i;

  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate);
  gst_pad_set_query_function (mysinkpad, query_func);
  gst_pad_set_active (mysinkpad, TRUE);

  /* hard to reproduce, so just run it a few times in a row */
  for (i = 0; i < 500; ++i) {
    GST_DEBUG ("starting");
    UNDERRUN_LOCK ();
    fail_unless (gst_element_set_state (queue,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
        "could not set to playing");
    UNDERRUN_WAIT ();
    UNDERRUN_UNLOCK ();

    thread = g_thread_new ("deactivating thread", thread_func, NULL);
    g_usleep (1000);

    event = gst_event_new_flush_start ();
    gst_pad_push_event (mysrcpad, event);

    g_thread_join (thread);

    GST_DEBUG ("stopping");
    fail_unless (gst_element_set_state (queue,
            GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS,
        "could not set to null");
  }
}

GST_END_TEST;


GST_START_TEST (test_serialized_query_with_threshold)
{
  GstQuery *query;
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);

  g_object_set (queue, "min-threshold-buffers", 10, NULL);

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  gst_pad_push (mysrcpad, gst_buffer_new ());

  query = gst_query_new_drain ();
  gst_pad_peer_query (mysrcpad, query);
  gst_query_unref (query);

  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;


static gpointer
push_event_thread_func (gpointer data)
{
  GstEvent *event;

  event = GST_EVENT (data);

  GST_DEBUG ("pushing event %p on pad %p", event, mysrcpad);
  gst_pad_push_event (mysrcpad, event);

  return NULL;
}

GST_START_TEST (test_state_change_when_flushing)
{
  GstEvent *event;
  GThread *thread;

  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate);
  gst_pad_set_active (mysinkpad, TRUE);

  fail_unless (gst_element_set_state (queue, GST_STATE_PAUSED) ==
      GST_STATE_CHANGE_SUCCESS);

  event = gst_event_new_flush_start ();
  gst_pad_push_event (mysrcpad, event);

  event = gst_event_new_flush_stop (TRUE);
  thread = g_thread_new ("send event", push_event_thread_func, event);

  GST_DEBUG ("changing state to READY");
  fail_unless (gst_element_set_state (queue, GST_STATE_READY) ==
      GST_STATE_CHANGE_SUCCESS);
  GST_DEBUG ("state changed");

  g_thread_join (thread);

  fail_unless (gst_element_set_state (queue, GST_STATE_NULL) ==
      GST_STATE_CHANGE_SUCCESS);
}

GST_END_TEST;

GST_START_TEST (test_time_level_buffer_list)
{
  GstBuffer *buffer = NULL;
  GstBufferList *buffer_list = NULL;
  GstClockTime time;
  guint buffers;
  GstSegment segment;

  g_signal_connect (queue, "overrun",
      G_CALLBACK (queue_overrun_link_and_activate), NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 11, NULL);
  g_object_set (G_OBJECT (queue), "max-size-time",
      G_GUINT64_CONSTANT (7000) * GST_MSECOND, NULL);

  GST_DEBUG ("starting");

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  /* push buffer without duration */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 1000 * GST_MSECOND;
  /* pushing gives away my reference */
  gst_pad_push (mysrcpad, buffer);

  /* level should be zero because buffer has no duration */
  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 0);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 1);

  /* second push should set the level to 2 second */
  buffer_list = gst_buffer_list_new ();
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 1500 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 2000 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  gst_pad_push_list (mysrcpad, buffer_list);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 1000 * GST_MSECOND);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 3);

  /* third push should set the level to 3 seconds, the 1 second diff with the
   * previous buffer (without duration) and the 1 second duration of this
   * buffer. */
  buffer_list = gst_buffer_list_new ();
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 3000 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 500 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_list_add (buffer_list, buffer);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 3500 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 500 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_list_add (buffer_list, buffer);
  gst_pad_push_list (mysrcpad, buffer_list);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 3000 * GST_MSECOND);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 5);

  /* fourth push should set the level to 5 seconds, the 2 second diff with the
   * previous buffer, same duration. */
  buffer_list = gst_buffer_list_new ();
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5000 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 1000 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (buffer, "buffer", 1);
  gst_buffer_list_add (buffer_list, buffer);
  gst_pad_push_list (mysrcpad, buffer_list);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 5000 * GST_MSECOND);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 6);

  /* fifth push should not adjust the level, the timestamp and duration are the
   * same, meaning the previous buffer did not really have a duration. */
  buffer_list = gst_buffer_list_new ();
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5000 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5250 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5500 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 5750 * GST_MSECOND;
  GST_BUFFER_DURATION (buffer) = 250 * GST_MSECOND;
  gst_buffer_list_add (buffer_list, buffer);
  gst_pad_push_list (mysrcpad, buffer_list);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 5000 * GST_MSECOND);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 10);

  /* sixth push should adjust the level with 1 second, we now know the
   * previous buffer actually had a duration of 2 SECONDS */
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 7000 * GST_MSECOND;
  gst_pad_push (mysrcpad, buffer);

  g_object_get (G_OBJECT (queue), "current-level-time", &time, NULL);
  fail_unless_equals_uint64 (time, 6000 * GST_MSECOND);
  g_object_get (G_OBJECT (queue), "current-level-buffers", &buffers, NULL);
  fail_unless_equals_int (buffers, 11);

  /* eighth push should cause overrun */
  fail_unless (overrun_count == 0);
  buffer_list = gst_buffer_list_new ();
  buffer = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer) = 8000 * GST_MSECOND;
  /* the next call to gst_pad_push will emit the overrun signal. The signal
   * handler queue_overrun_link_and_activate() (above) increases overrun_count,
   * activates and links mysinkpad. The queue task then dequeues a buffer and
   * gst_pad_push() will return. */
  gst_buffer_list_add (buffer_list, buffer);
  gst_pad_push_list (mysrcpad, buffer_list);

  fail_unless (overrun_count == 1);

  GST_DEBUG ("stopping");
  fail_unless (gst_element_set_state (queue,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
}

GST_END_TEST;

GST_START_TEST (test_initial_events_nodelay)
{
  GstSegment segment;
  GstEvent *event;
  GstCaps *caps;
  gboolean ret;

  mysinkpad = gst_check_setup_sink_pad (queue, &sinktemplate);
  gst_pad_set_event_function (mysinkpad, event_func);
  gst_pad_set_active (mysinkpad, TRUE);

  GST_DEBUG ("starting");

  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  ret = gst_pad_push_event (mysrcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);
  fail_unless (ret == TRUE);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  ret = gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));
  fail_unless (ret == TRUE);

  g_mutex_lock (&events_lock);
  while (events_count < 3) {
    g_cond_wait (&events_cond, &events_lock);
  }
  g_mutex_unlock (&events_lock);

  fail_unless_equals_int (g_list_length (events), 3);
  event = g_list_nth_data (events, 0);
  fail_unless_equals_int (GST_EVENT_TYPE (event), GST_EVENT_STREAM_START);
  event = g_list_nth_data (events, 1);
  fail_unless_equals_int (GST_EVENT_TYPE (event), GST_EVENT_CAPS);
  event = g_list_nth_data (events, 2);
  fail_unless_equals_int (GST_EVENT_TYPE (event), GST_EVENT_SEGMENT);

  gst_element_set_state (queue, GST_STATE_NULL);
}

GST_END_TEST;

typedef struct
{
  GstBuffer *buffer;
  GMutex lock;
  GCond cond;
  gboolean blocked;
} FlushOnErrorData;

static GstPadProbeReturn
flush_on_error_block_probe (GstPad * pad, GstPadProbeInfo * info,
    FlushOnErrorData * data)
{
  g_mutex_lock (&data->lock);
  data->blocked = TRUE;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);

  return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn
flush_on_error_probe (GstPad * pad, GstPadProbeInfo * info,
    FlushOnErrorData * data)
{
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info)))
    return GST_PAD_PROBE_DROP;

  g_mutex_lock (&data->lock);
  data->buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);

  GST_PAD_PROBE_INFO_FLOW_RETURN (info) = GST_FLOW_ERROR;

  return GST_PAD_PROBE_HANDLED;
}

static gpointer
alloc_thread (GstBufferPool * pool)
{
  GstFlowReturn ret;
  GstBuffer *buf;

  /* This call will be blocked */
  ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (ret == GST_FLOW_OK);

  gst_buffer_unref (buf);

  return NULL;
}

GST_START_TEST (test_flush_on_error)
{
  GstElement *elem;
  GstPad *sinkpad;
  GstPad *srcpad;
  GstSegment segment;
  GstCaps *caps;
  gboolean ret;
  gulong block_id;
  FlushOnErrorData data;
  GstBufferPool *pool;
  GstStructure *config;
  GstBuffer *buf;
  GstFlowReturn flow_ret;
  GThread *thread;

  data.buffer = NULL;
  data.blocked = FALSE;
  g_mutex_init (&data.lock);
  g_cond_init (&data.cond);

  /* Setup bufferpool with max-buffers 2 */
  caps = gst_caps_new_empty_simple ("foo/x-bar");
  pool = gst_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, 4, 0, 2);
  gst_buffer_pool_set_config (pool, config);
  gst_buffer_pool_set_active (pool, TRUE);

  elem = gst_element_factory_make ("queue", NULL);
  gst_object_ref_sink (elem);
  sinkpad = gst_element_get_static_pad (elem, "sink");
  srcpad = gst_element_get_static_pad (elem, "src");

  block_id = gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) flush_on_error_block_probe, &data, NULL);
  gst_pad_add_probe (srcpad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) flush_on_error_probe, &data, NULL);

  fail_unless (gst_element_set_state (elem,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  ret = gst_pad_send_event (sinkpad,
      gst_event_new_stream_start ("test-stream-start"));
  fail_unless (ret);

  ret = gst_pad_send_event (sinkpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);
  fail_unless (ret);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  ret = gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));
  fail_unless (ret);

  flow_ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (flow_ret == GST_FLOW_OK);
  GST_BUFFER_PTS (buf) = 0;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  flow_ret = gst_buffer_pool_acquire_buffer (pool, &buf, NULL);
  fail_unless (flow_ret == GST_FLOW_OK);
  GST_BUFFER_PTS (buf) = GST_SECOND;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  /* Acquire buffer from other thread. The acquire_buffer() will be blocked
   * due to max-buffers 2 */
  thread = g_thread_new (NULL, (GThreadFunc) alloc_thread, pool);

  g_mutex_lock (&data.lock);
  while (!data.blocked)
    g_cond_wait (&data.cond, &data.lock);
  g_mutex_unlock (&data.lock);

  gst_pad_remove_probe (srcpad, block_id);

  /* Then now acquire thread can be unblocked since queue will flush
   * internal queue on flow error */
  g_thread_join (thread);

  gst_element_set_state (elem, GST_STATE_NULL);
  gst_clear_buffer (&data.buffer);
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (elem);
  g_mutex_clear (&data.lock);
  g_cond_clear (&data.cond);
}

GST_END_TEST;

GST_START_TEST (test_time_level_before_output)
{
  GstBuffer *buffer1;
  GstBuffer *buffer2;
  GstSegment segment;
  GstClockTime time;

  g_signal_connect (queue, "overrun", G_CALLBACK (queue_overrun), NULL);
  g_object_set (queue, "max-size-time", 5 * GST_SECOND, "leaky", 2, NULL);

  block_src ();

  UNDERRUN_LOCK ();
  fail_unless (gst_element_set_state (queue,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  UNDERRUN_WAIT ();
  UNDERRUN_UNLOCK ();

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (mysrcpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment));

  fail_unless_equals_int (overrun_count, 0);
  fail_unless_equals_int (underrun_count, 1);

  buffer1 = gst_buffer_new_and_alloc (4);
  GST_BUFFER_TIMESTAMP (buffer1) = 25 * GST_SECOND;
  GST_BUFFER_DURATION (buffer1) = GST_SECOND;
  gst_pad_push (mysrcpad, buffer1);

  /* Pushed 1 second duration buffer, should report 1 seconds */
  g_object_get (queue, "current-level-time", &time, NULL);
  fail_unless_equals_int64 (time, GST_SECOND);
  fail_unless_equals_int (overrun_count, 0);
  fail_unless_equals_int (underrun_count, 1);

  buffer2 = gst_buffer_new_and_alloc (4);
  gst_pad_push (mysrcpad, buffer2);

  /* Pushed with unknown duration, should not cause overrun and
   * timelevel should not be changed */
  g_object_get (queue, "current-level-time", &time, NULL);
  fail_unless_equals_int64 (time, GST_SECOND);
  fail_unless_equals_int (overrun_count, 0);
  fail_unless_equals_int (underrun_count, 1);

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
  tcase_add_test (tc_chain, test_queries_while_flushing);
  tcase_add_test (tc_chain, test_serialized_query_with_threshold);
  tcase_add_test (tc_chain, test_state_change_when_flushing);
#if 0
  tcase_add_test (tc_chain, test_newsegment);
#endif
  tcase_add_test (tc_chain, test_sticky_not_linked);
  tcase_add_test (tc_chain, test_time_level_buffer_list);
  tcase_add_test (tc_chain, test_initial_events_nodelay);
  tcase_add_test (tc_chain, test_flush_on_error);
  tcase_add_test (tc_chain, test_time_level_before_output);

  return s;
}

GST_CHECK_MAIN (queue);
