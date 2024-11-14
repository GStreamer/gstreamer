/* GStreamer unit tests for streamsynchronizer
 *
 * Copyright (C) 2012 Edward Hervey <edward@collabora.com>, Collabora Ltd
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
# include <config.h>
#endif

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#undef GST_CAT_DEFAULT
#include <gst/check/gstcheck.h>

static GstStaticPadTemplate mysinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate mysrctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

typedef struct
{
  GstPad *pad;
  GList *to_push;
  GMutex *push_mutex;
  GCond *push_cond;
} MyPushInfo;

typedef struct
{
  GList *expected;
  GMutex push_mutex;
  GCond push_cond;
  gboolean compare_segment_base;
} MyPadPrivateData;

MyPadPrivateData private_data_video, private_data_audio;

static GstPad *
get_other_pad (GstPad * pad)
{
  GstIterator *it;
  GValue item = G_VALUE_INIT;
  GstPad *otherpad;

  it = gst_pad_iterate_internal_links (pad);
  fail_unless (gst_iterator_next (it, &item) == GST_ITERATOR_OK);
  otherpad = g_value_dup_object (&item);
  g_value_unset (&item);
  gst_iterator_free (it);

  return otherpad;
}

static GstFlowReturn
my_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  MyPadPrivateData *private_data = GST_PAD_ELEMENT_PRIVATE (pad);
  GList *next;
  GstBuffer *exp;

  fail_if (private_data->expected == NULL,
      "streamsynchronizer pushed a buffer/event but we didn't expect any");

  next = (private_data->expected)->next;

  fail_if (GST_IS_EVENT ((private_data->expected)->data),
      "Expected an event (%s) but got a buffer instead",
      GST_EVENT_TYPE_NAME (GST_EVENT ((private_data->expected)->data)));

  exp = GST_BUFFER ((private_data->expected)->data);

  fail_unless_equals_uint64 (GST_BUFFER_TIMESTAMP (buf),
      GST_BUFFER_TIMESTAMP (exp));

  GST_DEBUG ("Properly received expected buffer: %p", buf);
  gst_buffer_unref (exp);
  gst_buffer_unref (buf);

  g_list_free1 (private_data->expected);
  private_data->expected = next;

  /* When done signal main thread */
  if (next == NULL) {
    g_mutex_lock (&private_data->push_mutex);
    g_cond_signal (&private_data->push_cond);
    g_mutex_unlock (&private_data->push_mutex);
  }

  return GST_FLOW_OK;
}

static gboolean
my_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  MyPadPrivateData *private_data = GST_PAD_ELEMENT_PRIVATE (pad);
  GList *next;
  GstEvent *exp;

  fail_if (private_data->expected == NULL,
      "streamsynchronizer pushed a buffer/event but we didn't expect any");

  next = (private_data->expected)->next;

  fail_unless (GST_IS_EVENT ((private_data->expected)->data),
      "We were not expecting an event (But got an event of type %s)",
      GST_EVENT_TYPE_NAME (event));
  exp = GST_EVENT ((private_data->expected)->data);
  fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_TYPE (exp),
      "Got event of type %s but expected of type %s",
      GST_EVENT_TYPE_NAME (event), GST_EVENT_TYPE_NAME (exp));
  fail_unless_equals_int (GST_EVENT_SEQNUM (event), GST_EVENT_SEQNUM (exp));
  /* FIXME : Check more types */

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      const GstSegment *recvseg, *expectseg;

      /* Compare segment values */
      gst_event_parse_segment (event, &recvseg);
      gst_event_parse_segment (exp, &expectseg);

      fail_unless_equals_int (recvseg->format, expectseg->format);
      if (private_data->compare_segment_base) {
        fail_unless_equals_uint64 (recvseg->base, expectseg->base);
      }
      fail_unless_equals_uint64 (recvseg->offset, expectseg->offset);
      fail_unless_equals_uint64 (recvseg->start, expectseg->start);
      fail_unless_equals_uint64 (recvseg->stop, expectseg->stop);
      fail_unless_equals_uint64 (recvseg->time, expectseg->time);
      fail_unless_equals_uint64 (recvseg->position, expectseg->position);
      fail_unless_equals_uint64 (recvseg->duration, expectseg->duration);
    }
      break;
    default:
      break;
  }

  GST_DEBUG ("Properly received expected event %s: %p",
      GST_EVENT_TYPE_NAME (exp), event);

  gst_event_unref (exp);
  gst_event_unref (event);

  g_list_free1 (private_data->expected);
  private_data->expected = next;

  /* When done signal main thread */
  if (next == NULL) {
    g_mutex_lock (&private_data->push_mutex);
    g_cond_signal (&private_data->push_cond);
    g_mutex_unlock (&private_data->push_mutex);
  }

  return TRUE;
}

static gpointer
my_push_thread (MyPushInfo * pushinfo)
{
  GList *tmp;

  GST_DEBUG ("starting thread");

  /* Nothing to do if the to_push list is empty in the first place. Signal main thread */
  if (pushinfo->to_push == NULL) {
    g_mutex_lock (pushinfo->push_mutex);
    g_cond_signal (pushinfo->push_cond);
    g_mutex_unlock (pushinfo->push_mutex);
  }

  /* FIXME : Do this in a thread */
  for (tmp = pushinfo->to_push; tmp; tmp = tmp->next) {
    if (GST_IS_EVENT (tmp->data)) {
      GST_DEBUG ("Pushing event %s: %p",
          GST_EVENT_TYPE_NAME (GST_EVENT (tmp->data)), GST_EVENT (tmp->data));
      gst_pad_push_event (pushinfo->pad, GST_EVENT (tmp->data));
    } else {
      GST_DEBUG ("Pushing buffer: %p", GST_BUFFER (tmp->data));
      gst_pad_push (pushinfo->pad, GST_BUFFER (tmp->data));
    }
  }

  GST_INFO ("leaving thread");
  return NULL;
}

GST_START_TEST (test_basic)
{
  GstElement *synchr;
  GstPad *sinkpad, *srcpad;
  GstPad *mysrcpad, *mysinkpad;
  GList *to_push = NULL;

  GstEvent *event;
  GstBuffer *buf;
  GThread *thread;
  MyPushInfo pushinfo;
  guint i;
  GstSegment segment;
  guint32 seqnum;

  synchr = gst_element_factory_make ("streamsynchronizer", NULL);

  /* Get sinkpad/srcpad */
  sinkpad = gst_element_request_pad_simple (synchr, "sink_%u");
  fail_unless (sinkpad != NULL);
  srcpad = get_other_pad (sinkpad);
  fail_unless (srcpad != NULL);

  gst_element_set_state (synchr, GST_STATE_PLAYING);

  mysrcpad = gst_pad_new_from_static_template (&mysrctemplate, "src");
  fail_if (mysrcpad == NULL);
  fail_unless (gst_pad_link (mysrcpad, sinkpad) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysrcpad, TRUE));

  mysinkpad = gst_pad_new_from_static_template (&mysinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad, my_sink_chain);
  gst_pad_set_event_function (mysinkpad, my_sink_event);
  fail_if (mysinkpad == NULL);
  fail_unless (gst_pad_link (srcpad, mysinkpad) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysinkpad, TRUE));
  GST_PAD_ELEMENT_PRIVATE (mysinkpad) = &private_data_video;

  /* The segment.base expected time is important for this test */
  private_data_video.compare_segment_base = TRUE;

  private_data_video.expected = NULL;

  /* Start with a stream START and a new segment */
  event = gst_event_new_stream_start ("lala");
  to_push = g_list_append (to_push, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  to_push = g_list_append (to_push, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  /* Then 10 buffers */
  for (i = 0; i < 10; i++) {
    buf = gst_buffer_new ();
    GST_BUFFER_TIMESTAMP (buf) = i * GST_SECOND;
    GST_BUFFER_DURATION (buf) = GST_SECOND;
    to_push = g_list_append (to_push, buf);
    private_data_video.expected =
        g_list_append (private_data_video.expected, gst_buffer_ref (buf));
  }

  /* Then a new stream start */
  event = gst_event_new_stream_start ("lala again");
  to_push = g_list_append (to_push, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  /* This newsegment will be updated */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  seqnum = gst_event_get_seqnum (event);
  to_push = g_list_append (to_push, event);
  /* The received segment's base should be updated by streamsynchronizer to
   * take into account the amount of data played before (i.e. 10s) */
  segment.base = 10 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  gst_event_set_seqnum (event, seqnum);
  private_data_video.expected =
      g_list_append (private_data_video.expected, event);

  /* Then 10 buffers */
  for (i = 0; i < 10; i++) {
    buf = gst_buffer_new ();
    GST_BUFFER_TIMESTAMP (buf) = i * GST_SECOND;
    GST_BUFFER_DURATION (buf) = GST_SECOND;
    to_push = g_list_append (to_push, buf);
    private_data_video.expected =
        g_list_append (private_data_video.expected, gst_buffer_ref (buf));
  }

  g_mutex_init (&private_data_video.push_mutex);
  pushinfo.push_mutex = &private_data_video.push_mutex;
  g_cond_init (&private_data_video.push_cond);
  pushinfo.push_cond = &private_data_video.push_cond;

  pushinfo.pad = mysrcpad;
  pushinfo.to_push = to_push;
  g_mutex_lock (&private_data_video.push_mutex);
  thread = g_thread_new ("pushthread", (GThreadFunc) my_push_thread, &pushinfo);
  fail_unless (thread != NULL);

  g_cond_wait (&private_data_video.push_cond, &private_data_video.push_mutex);
  g_mutex_unlock (&private_data_video.push_mutex);

  fail_if (private_data_video.expected != NULL);

  /* wait for thread to exit before freeing things */
  g_thread_join (thread);

  /* Cleanup */
  g_list_free (to_push);
  gst_element_release_request_pad (synchr, sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_object_unref (mysinkpad);
  gst_object_unref (mysrcpad);
  gst_element_set_state (synchr, GST_STATE_NULL);
  gst_object_unref (synchr);
}

GST_END_TEST;

GST_START_TEST (test_stream_start_wait)
{
  GstElement *synchr;
  GstPad *sinkpad_video, *srcpad_video, *sinkpad_audio, *srcpad_audio;
  GstPad *mysrcpad_video, *mysinkpad_video, *mysrcpad_audio, *mysinkpad_audio;
  GList *to_push_video = NULL, *to_push_audio = NULL;
  GstEvent *event;
  GstBuffer *buf;
  GThread *thread_video, *thread_audio;
  MyPushInfo pushinfo_video, pushinfo_audio;
  GstSegment segment;

  synchr = gst_element_factory_make ("streamsynchronizer", NULL);

  GST_DEBUG ("Get sinkpad/srcpad for a first V0 stream");

  sinkpad_video = gst_element_request_pad_simple (synchr, "sink_%u");
  fail_unless (sinkpad_video != NULL);
  srcpad_video = get_other_pad (sinkpad_video);
  fail_unless (srcpad_video != NULL);

  gst_element_set_state (synchr, GST_STATE_PLAYING);

  mysrcpad_video = gst_pad_new_from_static_template (&mysrctemplate, "src");
  fail_if (mysrcpad_video == NULL);
  fail_unless (gst_pad_link (mysrcpad_video, sinkpad_video) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysrcpad_video, TRUE));

  mysinkpad_video = gst_pad_new_from_static_template (&mysinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad_video, my_sink_chain);
  gst_pad_set_event_function (mysinkpad_video, my_sink_event);
  fail_if (mysinkpad_video == NULL);
  fail_unless (gst_pad_link (srcpad_video, mysinkpad_video) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysinkpad_video, TRUE));
  GST_PAD_ELEMENT_PRIVATE (mysinkpad_video) = &private_data_video;

  /* The segment.base expected time is important for this part of the test */
  private_data_video.compare_segment_base = TRUE;

  private_data_video.expected = NULL;

  GST_DEBUG ("Start with a stream-start and a segment event");

  event = gst_event_new_stream_start ("mse/V0");
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = 0 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;

  GST_DEBUG ("Then 1 video buffer %p", buf);

  to_push_video = g_list_append (to_push_video, buf);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_buffer_ref (buf));

  GST_DEBUG ("Simulate the effect of a seek to 6s with basic events with...");
  GST_DEBUG ("...a flush-start event");

  event = gst_event_new_flush_start ();
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...a flush-stop event");

  event = gst_event_new_flush_stop (TRUE);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...a stream-start event");

  event = gst_event_new_stream_start ("mse/V0");
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...and a segment event");

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.format = GST_FORMAT_TIME;
  segment.start = 6 * GST_SECOND;
  segment.time = 6 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  /* Run all these steps until completion before continuing */
  GST_DEBUG ("Run all these steps until completion before continuing");

  g_mutex_init (&private_data_video.push_mutex);
  pushinfo_video.push_mutex = &private_data_video.push_mutex;
  g_cond_init (&private_data_video.push_cond);
  pushinfo_video.push_cond = &private_data_video.push_cond;

  pushinfo_video.pad = mysrcpad_video;
  pushinfo_video.to_push = to_push_video;
  g_mutex_lock (&private_data_video.push_mutex);

  GST_DEBUG ("Creating video thread");

  thread_video =
      g_thread_new ("pushthread_video", (GThreadFunc) my_push_thread,
      &pushinfo_video);
  fail_unless (thread_video != NULL);

  GST_DEBUG
      ("Waiting for all expected video events/buffers to be processed and join the video thread");

  g_cond_wait (&private_data_video.push_cond, &private_data_video.push_mutex);
  g_mutex_unlock (&private_data_video.push_mutex);

  GST_DEBUG ("Wait completed");

  fail_if (private_data_video.expected != NULL);
  g_thread_join (thread_video);

  GST_DEBUG ("Now create a second stream, A0");

  sinkpad_audio = gst_element_request_pad_simple (synchr, "sink_%u");
  fail_unless (sinkpad_audio != NULL);
  srcpad_audio = get_other_pad (sinkpad_audio);
  fail_unless (srcpad_audio != NULL);

  mysrcpad_audio = gst_pad_new_from_static_template (&mysrctemplate, "src");
  fail_if (mysrcpad_audio == NULL);
  fail_unless (gst_pad_link (mysrcpad_audio, sinkpad_audio) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysrcpad_audio, TRUE));

  mysinkpad_audio = gst_pad_new_from_static_template (&mysinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad_audio, my_sink_chain);
  gst_pad_set_event_function (mysinkpad_audio, my_sink_event);
  fail_if (mysinkpad_audio == NULL);
  fail_unless (gst_pad_link (srcpad_audio, mysinkpad_audio) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysinkpad_audio, TRUE));
  GST_PAD_ELEMENT_PRIVATE (mysinkpad_audio) = &private_data_audio;

  /* The segment.base expected time is not important for this part of the test,
   * because it changes depending on the stream and sometimes is 1s for audio
   * and 0s for video and some other times it's the opposite. It's not
   * predictable. */
  private_data_video.compare_segment_base = FALSE;

  private_data_audio.expected = NULL;

  /* Start with a stream START and a new segment like the one used for the simulated seek */
  GST_DEBUG
      ("Start with a stream-start and a new segment like the one used for the simulated seek");

  event = gst_event_new_stream_start ("mse/A0");
  to_push_audio = g_list_append (to_push_audio, event);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_event_ref (event));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.format = GST_FORMAT_TIME;
  segment.start = 6 * GST_SECOND;
  segment.time = 6 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  to_push_audio = g_list_append (to_push_audio, event);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_event_ref (event));

  g_mutex_lock (&private_data_video.push_mutex);

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = 6 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;

  GST_DEBUG ("Send one video buffer at 6 sec: %p", buf);

  /* Discard old events/buffers from the list and start from scratch */
  g_list_free (to_push_video);
  to_push_video = NULL;

  to_push_video = g_list_append (to_push_video, buf);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_buffer_ref (buf));

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = 6 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;

  GST_DEBUG ("Send one audio buffer at 6 sec: %p", buf);

  to_push_audio = g_list_append (to_push_audio, buf);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_buffer_ref (buf));

  g_mutex_init (&private_data_video.push_mutex);
  pushinfo_video.push_mutex = &private_data_video.push_mutex;
  g_cond_init (&private_data_video.push_cond);
  pushinfo_video.push_cond = &private_data_video.push_cond;

  pushinfo_video.pad = mysrcpad_video;
  pushinfo_video.to_push = to_push_video;
  g_mutex_lock (&private_data_video.push_mutex);

  GST_DEBUG ("Creating video thread again");

  thread_video =
      g_thread_new ("pushthread_video", (GThreadFunc) my_push_thread,
      &pushinfo_video);
  fail_unless (thread_video != NULL);

  g_mutex_init (&private_data_audio.push_mutex);
  pushinfo_audio.push_mutex = &private_data_audio.push_mutex;
  g_cond_init (&private_data_audio.push_cond);
  pushinfo_audio.push_cond = &private_data_audio.push_cond;

  pushinfo_audio.pad = mysrcpad_audio;
  pushinfo_audio.to_push = to_push_audio;
  g_mutex_lock (&private_data_audio.push_mutex);

  GST_DEBUG ("Creating audio thread");

  thread_audio =
      g_thread_new ("pushthread_audio", (GThreadFunc) my_push_thread,
      &pushinfo_audio);
  fail_unless (thread_audio != NULL);

  GST_DEBUG ("Waiting for all expected video events/buffers to be processed");

  g_cond_wait (&private_data_video.push_cond, &private_data_video.push_mutex);
  g_mutex_unlock (&private_data_video.push_mutex);

  GST_DEBUG ("Video wait completed");

  fail_if (private_data_video.expected != NULL);

  GST_DEBUG ("Waiting for all expected audio events/buffers to be processed");

  g_cond_wait (&private_data_audio.push_cond, &private_data_audio.push_mutex);
  g_mutex_unlock (&private_data_audio.push_mutex);

  GST_DEBUG ("Audio wait completed");

  fail_if (private_data_audio.expected != NULL);

  g_thread_join (thread_video);
  g_thread_join (thread_audio);

  GST_DEBUG ("Cleanup");

  g_list_free (to_push_video);
  g_list_free (to_push_audio);
  gst_element_release_request_pad (synchr, sinkpad_video);
  gst_element_release_request_pad (synchr, sinkpad_audio);
  gst_object_unref (srcpad_video);
  gst_object_unref (sinkpad_video);
  gst_object_unref (mysinkpad_video);
  gst_object_unref (mysrcpad_video);
  gst_object_unref (srcpad_audio);
  gst_object_unref (sinkpad_audio);
  gst_object_unref (mysinkpad_audio);
  gst_object_unref (mysrcpad_audio);
  gst_element_set_state (synchr, GST_STATE_NULL);
  gst_object_unref (synchr);
}

GST_END_TEST;

GST_START_TEST (test_stream_start_wait_sparse)
{
  GstElement *synchr;
  GstPad *sinkpad_video, *srcpad_video, *sinkpad_audio, *srcpad_audio;
  GstPad *mysrcpad_video, *mysinkpad_video, *mysrcpad_audio, *mysinkpad_audio;
  GList *to_push_video = NULL, *to_push_audio = NULL;
  GstEvent *event;
  GstBuffer *buf;
  GThread *thread_video, *thread_audio;
  MyPushInfo pushinfo_video, pushinfo_audio;
  GstSegment segment;

  synchr = gst_element_factory_make ("streamsynchronizer", NULL);

  GST_DEBUG ("Get sinkpad/srcpad for a first V0 stream");

  sinkpad_video = gst_element_request_pad_simple (synchr, "sink_%u");
  fail_unless (sinkpad_video != NULL);
  srcpad_video = get_other_pad (sinkpad_video);
  fail_unless (srcpad_video != NULL);

  gst_element_set_state (synchr, GST_STATE_PLAYING);

  mysrcpad_video = gst_pad_new_from_static_template (&mysrctemplate, "src");
  fail_if (mysrcpad_video == NULL);
  fail_unless (gst_pad_link (mysrcpad_video, sinkpad_video) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysrcpad_video, TRUE));

  mysinkpad_video = gst_pad_new_from_static_template (&mysinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad_video, my_sink_chain);
  gst_pad_set_event_function (mysinkpad_video, my_sink_event);
  fail_if (mysinkpad_video == NULL);
  fail_unless (gst_pad_link (srcpad_video, mysinkpad_video) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysinkpad_video, TRUE));
  GST_PAD_ELEMENT_PRIVATE (mysinkpad_video) = &private_data_video;

  /* The segment.base expected time is important for this part of the test */
  private_data_video.compare_segment_base = TRUE;

  private_data_video.expected = NULL;

  GST_DEBUG ("Start with a stream-start (sparse) and a segment event");

  event = gst_event_new_stream_start ("mse/V0");
  gst_event_set_stream_flags (event, GST_STREAM_FLAG_SPARSE);

  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  event = gst_event_new_segment (&segment);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  buf = gst_buffer_new ();
  /* The stream is sparse. The segment starts at 0s but the first buffer
     comes at 1s */
  GST_BUFFER_TIMESTAMP (buf) = 1 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;

  GST_DEBUG ("Then 1 video buffer %p", buf);

  to_push_video = g_list_append (to_push_video, buf);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_buffer_ref (buf));

  GST_DEBUG ("Simulate the effect of a seek to 6s with basic events with...");
  GST_DEBUG ("...a flush-start event");

  event = gst_event_new_flush_start ();
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...a flush-stop event");

  event = gst_event_new_flush_stop (TRUE);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...a stream-start (sparse) event");

  event = gst_event_new_stream_start ("mse/V0");
  gst_event_set_stream_flags (event, GST_STREAM_FLAG_SPARSE);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  GST_DEBUG ("...and a segment event");

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.format = GST_FORMAT_TIME;
  segment.start = 6 * GST_SECOND;
  segment.time = 6 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  to_push_video = g_list_append (to_push_video, event);
  private_data_video.expected =
      g_list_append (private_data_video.expected, gst_event_ref (event));

  /* Run all these steps until completion before continuing */
  GST_DEBUG ("Run all these steps until completion before continuing");

  g_mutex_init (&private_data_video.push_mutex);
  pushinfo_video.push_mutex = &private_data_video.push_mutex;
  g_cond_init (&private_data_video.push_cond);
  pushinfo_video.push_cond = &private_data_video.push_cond;

  pushinfo_video.pad = mysrcpad_video;
  pushinfo_video.to_push = to_push_video;
  g_mutex_lock (&private_data_video.push_mutex);

  GST_DEBUG ("Creating video thread");

  thread_video =
      g_thread_new ("pushthread_video", (GThreadFunc) my_push_thread,
      &pushinfo_video);
  fail_unless (thread_video != NULL);

  GST_DEBUG
      ("Waiting for all expected video events/buffers to be processed and join the video thread");

  g_cond_wait (&private_data_video.push_cond, &private_data_video.push_mutex);
  g_mutex_unlock (&private_data_video.push_mutex);

  GST_DEBUG ("Wait completed");

  fail_if (private_data_video.expected != NULL);
  g_thread_join (thread_video);

  GST_DEBUG ("Now create a second stream, A0");

  sinkpad_audio = gst_element_request_pad_simple (synchr, "sink_%u");
  fail_unless (sinkpad_audio != NULL);
  srcpad_audio = get_other_pad (sinkpad_audio);
  fail_unless (srcpad_audio != NULL);

  mysrcpad_audio = gst_pad_new_from_static_template (&mysrctemplate, "src");
  fail_if (mysrcpad_audio == NULL);
  fail_unless (gst_pad_link (mysrcpad_audio, sinkpad_audio) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysrcpad_audio, TRUE));

  mysinkpad_audio = gst_pad_new_from_static_template (&mysinktemplate, "sink");
  gst_pad_set_chain_function (mysinkpad_audio, my_sink_chain);
  gst_pad_set_event_function (mysinkpad_audio, my_sink_event);
  fail_if (mysinkpad_audio == NULL);
  fail_unless (gst_pad_link (srcpad_audio, mysinkpad_audio) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_set_active (mysinkpad_audio, TRUE));
  GST_PAD_ELEMENT_PRIVATE (mysinkpad_audio) = &private_data_audio;

  /* The segment.base expected time is not important for this part of the test,
   * because it changes depending on the stream and sometimes is 1s for audio
   * and 0s for video and some other times it's the opposite. It's not
   * predictable. */
  private_data_video.compare_segment_base = FALSE;

  private_data_audio.expected = NULL;

  /* Start with a stream START and a new segment like the one used for the simulated seek */
  GST_DEBUG
      ("Start with a stream-start (not sparse) and a new segment like the one used for the simulated seek");

  event = gst_event_new_stream_start ("mse/A0");
  to_push_audio = g_list_append (to_push_audio, event);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_event_ref (event));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.format = GST_FORMAT_TIME;
  segment.start = 6 * GST_SECOND;
  segment.time = 6 * GST_SECOND;
  event = gst_event_new_segment (&segment);
  to_push_audio = g_list_append (to_push_audio, event);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_event_ref (event));

  /* Discard old events/buffers from the list and start from scratch */
  g_list_free (to_push_video);
  to_push_video = NULL;
  g_list_free (private_data_video.expected);
  private_data_video.expected = NULL;

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = 6 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_SECOND;

  GST_DEBUG ("Send one audio buffer at 6 sec: %p", buf);

  to_push_audio = g_list_append (to_push_audio, buf);
  private_data_audio.expected =
      g_list_append (private_data_audio.expected, gst_buffer_ref (buf));

  g_mutex_init (&private_data_video.push_mutex);
  pushinfo_video.push_mutex = &private_data_video.push_mutex;
  g_cond_init (&private_data_video.push_cond);
  pushinfo_video.push_cond = &private_data_video.push_cond;

  pushinfo_video.pad = mysrcpad_video;
  pushinfo_video.to_push = to_push_video;
  g_mutex_lock (&private_data_video.push_mutex);

  GST_DEBUG ("Creating video thread again");

  thread_video =
      g_thread_new ("pushthread_video", (GThreadFunc) my_push_thread,
      &pushinfo_video);
  fail_unless (thread_video != NULL);

  g_mutex_init (&private_data_audio.push_mutex);
  pushinfo_audio.push_mutex = &private_data_audio.push_mutex;
  g_cond_init (&private_data_audio.push_cond);
  pushinfo_audio.push_cond = &private_data_audio.push_cond;

  pushinfo_audio.pad = mysrcpad_audio;
  pushinfo_audio.to_push = to_push_audio;
  g_mutex_lock (&private_data_audio.push_mutex);

  GST_DEBUG ("Creating audio thread");

  thread_audio =
      g_thread_new ("pushthread_audio", (GThreadFunc) my_push_thread,
      &pushinfo_audio);
  fail_unless (thread_audio != NULL);

  GST_DEBUG ("Waiting for all expected audio events/buffers to be processed");

  g_cond_wait (&private_data_audio.push_cond, &private_data_audio.push_mutex);
  g_mutex_unlock (&private_data_audio.push_mutex);

  GST_DEBUG ("Audio wait completed");

  fail_if (private_data_audio.expected != NULL);

  GST_DEBUG ("Waiting for all expected video events/buffers to be processed");

  g_cond_wait (&private_data_video.push_cond, &private_data_video.push_mutex);
  g_mutex_unlock (&private_data_video.push_mutex);

  GST_DEBUG ("Video wait completed");

  fail_if (private_data_video.expected != NULL);

  g_thread_join (thread_video);
  g_thread_join (thread_audio);

  GST_DEBUG ("Cleanup");

  g_list_free (to_push_video);
  g_list_free (to_push_audio);
  gst_element_release_request_pad (synchr, sinkpad_video);
  gst_element_release_request_pad (synchr, sinkpad_audio);
  gst_object_unref (srcpad_video);
  gst_object_unref (sinkpad_video);
  gst_object_unref (mysinkpad_video);
  gst_object_unref (mysrcpad_video);
  gst_object_unref (srcpad_audio);
  gst_object_unref (sinkpad_audio);
  gst_object_unref (mysinkpad_audio);
  gst_object_unref (mysrcpad_audio);
  gst_element_set_state (synchr, GST_STATE_NULL);
  gst_object_unref (synchr);
}

GST_END_TEST;

static Suite *
streamsynchronizer_suite (void)
{
  Suite *s = suite_create ("streamsynchronizer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_basic);
  tcase_add_test (tc_chain, test_stream_start_wait);
  tcase_add_test (tc_chain, test_stream_start_wait_sparse);

  return s;
}

GST_CHECK_MAIN (streamsynchronizer);
