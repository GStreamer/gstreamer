/* GStreamer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * gstevent.c: Unit test for event handling
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

GST_START_TEST (create_events)
{
  GstEvent *event, *event2;
  GstStructure *structure;

  /* FLUSH_START */
  {
    event = gst_event_new_flush_start ();
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_START);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));
    gst_event_unref (event);
  }
  /* FLUSH_STOP */
  {
    gboolean reset_time;

    event = gst_event_new_flush_stop (TRUE);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_flush_stop (event, &reset_time);
    fail_unless (reset_time == TRUE);
    gst_event_unref (event);
  }

  /* SELECT_STREAMS */
  {
    GList *streams = NULL;
    GList *res = NULL;
    GList *tmp;
    streams = g_list_append (streams, (gpointer) "stream1");
    streams = g_list_append (streams, (gpointer) "stream2");
    event = gst_event_new_select_streams (streams);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SELECT_STREAMS);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));

    gst_event_parse_select_streams (event, &res);
    fail_if (res == NULL);
    fail_unless_equals_int (g_list_length (res), 2);
    tmp = res;
    fail_unless_equals_string (tmp->data, "stream1");
    tmp = tmp->next;
    fail_unless_equals_string (tmp->data, "stream2");

    gst_event_unref (event);

    g_list_free (streams);
    g_list_free_full (res, g_free);
  }

  /* STREAM_GROUP_DONE */
  {
    guint group_id = 0;

    event = gst_event_new_stream_group_done (0x42);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_GROUP_DONE);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
    gst_event_parse_stream_group_done (event, &group_id);
    fail_unless (group_id == 0x42);
    gst_event_unref (event);
  }

  /* EOS */
  {
    event = gst_event_new_eos ();
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_EOS);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
    gst_event_unref (event);
  }
  /* GAP */
  {
    GstClockTime ts = 0, dur = 0;

    ASSERT_CRITICAL (gst_event_new_gap (GST_CLOCK_TIME_NONE, GST_SECOND));

    event = gst_event_new_gap (90 * GST_SECOND, GST_SECOND);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_GAP);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
    gst_event_parse_gap (event, &ts, NULL);
    fail_unless_equals_int64 (ts, 90 * GST_SECOND);
    gst_event_parse_gap (event, &ts, &dur);
    fail_unless_equals_int64 (dur, GST_SECOND);
    gst_event_unref (event);
  }
  /* SEGMENT */
  {
    GstSegment segment, parsed;

    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.rate = 0.5;
    segment.applied_rate = 1.0;
    segment.start = 1;
    segment.stop = G_MAXINT64;
    segment.time = 0xdeadbeef;

    event = gst_event_new_segment (&segment);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));

    gst_event_copy_segment (event, &parsed);
    fail_unless (parsed.rate == 0.5);
    fail_unless (parsed.applied_rate == 1.0);
    fail_unless (parsed.format == GST_FORMAT_TIME);
    fail_unless (parsed.start == 1);
    fail_unless (parsed.stop == G_MAXINT64);
    fail_unless (parsed.time == 0xdeadbeef);

    gst_event_unref (event);
  }

  /* TAGS */
  {
    GstTagList *taglist = gst_tag_list_new_empty ();
    GstTagList *tl2 = NULL;

    event = gst_event_new_tag (taglist);
    fail_if (taglist == NULL);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_TAG);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_tag (event, &tl2);
    fail_unless (taglist == tl2);
    gst_event_unref (event);
  }

  /* QOS */
  {
    GstQOSType t1 = GST_QOS_TYPE_THROTTLE, t2;
    gdouble p1 = 1.0, p2;
    GstClockTimeDiff ctd1 = G_GINT64_CONSTANT (10), ctd2;
    GstClockTime ct1 = G_GUINT64_CONSTANT (20), ct2;

    event = gst_event_new_qos (t1, p1, ctd1, ct1);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_QOS);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_qos (event, &t2, &p2, &ctd2, &ct2);
    fail_unless (p1 == p2);
    fail_unless (ctd1 == ctd2);
    fail_unless (ct1 == ct2);
    gst_event_parse_qos (event, &t2, &p2, &ctd2, &ct2);
    fail_unless (t2 == GST_QOS_TYPE_THROTTLE);
    fail_unless (p1 == p2);
    fail_unless (ctd1 == ctd2);
    fail_unless (ct1 == ct2);
    gst_event_unref (event);

    ctd1 = G_GINT64_CONSTANT (-10);
    event = gst_event_new_qos (t1, p1, ctd1, ct1);
    gst_event_parse_qos (event, &t2, &p2, &ctd2, &ct2);
    fail_unless (t2 == GST_QOS_TYPE_THROTTLE);
    gst_event_unref (event);

    event = gst_event_new_qos (t1, p1, ctd1, ct1);
    gst_event_parse_qos (event, &t2, &p2, &ctd2, &ct2);
    fail_unless (t2 == GST_QOS_TYPE_THROTTLE);
    fail_unless (p1 == p2);
    fail_unless (ctd1 == ctd2);
    fail_unless (ct1 == ct2);
    gst_event_unref (event);
  }

  /* SEEK */
  {
    gdouble rate;
    GstFormat format;
    GstSeekFlags flags;
    GstSeekType start_type, stop_type;
    gint64 start, stop;

    event = gst_event_new_seek (0.5, GST_FORMAT_BYTES,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
        GST_SEEK_TYPE_SET, 1, GST_SEEK_TYPE_NONE, 0xdeadbeef);

    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEEK);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
        &stop_type, &stop);
    fail_unless (rate == 0.5);
    fail_unless (format == GST_FORMAT_BYTES);
    fail_unless (flags == (GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
    fail_unless (start_type == GST_SEEK_TYPE_SET);
    fail_unless (start == 1);
    fail_unless (stop_type == GST_SEEK_TYPE_NONE);
    fail_unless (stop == 0xdeadbeef);

    gst_event_unref (event);
  }

  /* STREAM_START */
  {
    GstStreamFlags flags = ~GST_STREAM_FLAG_NONE;

    event = gst_event_new_stream_start ("7f4b2f0/audio_02");
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
    gst_event_parse_stream_flags (event, &flags);
    fail_unless_equals_int (flags, GST_STREAM_FLAG_NONE);
    gst_event_set_stream_flags (event, GST_STREAM_FLAG_SPARSE);
    gst_event_parse_stream_flags (event, &flags);
    fail_unless_equals_int (flags, GST_STREAM_FLAG_SPARSE);
    gst_event_ref (event);
    ASSERT_CRITICAL (gst_event_set_stream_flags (event, GST_STREAM_FLAG_NONE));
    gst_event_unref (event);
    gst_event_unref (event);
  }

  /* STREAM_COLLECTION */
  {
    GstStreamCollection *collection, *res = NULL;
    GstStream *stream1, *stream2;
    GstCaps *caps1, *caps2;

    /* Create a collection of two streams */
    caps1 = gst_caps_from_string ("some/caps");
    caps2 = gst_caps_from_string ("some/other-string");

    stream1 = gst_stream_new ("stream-1", caps1, GST_STREAM_TYPE_AUDIO, 0);
    stream2 = gst_stream_new ("stream-2", caps2, GST_STREAM_TYPE_VIDEO, 0);

    collection = gst_stream_collection_new ("something");
    fail_unless (gst_stream_collection_add_stream (collection, stream1));
    fail_unless (gst_stream_collection_add_stream (collection, stream2));

    event = gst_event_new_stream_collection (collection);
    fail_unless (event != NULL);

    gst_event_parse_stream_collection (event, &res);
    fail_unless (res != NULL);
    fail_unless (res == collection);

    gst_event_unref (event);
    gst_object_unref (res);
    gst_object_unref (collection);
    gst_caps_unref (caps1);
    gst_caps_unref (caps2);
  }

  /* NAVIGATION */
  {
    structure = gst_structure_new ("application/x-gst-navigation", "event",
        G_TYPE_STRING, "key-press", "key", G_TYPE_STRING, "mon", NULL);
    fail_if (structure == NULL);
    event = gst_event_new_navigation (structure);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));

    fail_unless (gst_event_get_structure (event) == structure);
    gst_event_unref (event);
  }

  /* Protection */
  {
    GstBuffer *data;
    GstMemory *mem;
    const gchar *parsed_origin;
    const gchar *parsed_id;
    GstBuffer *parsed_data;
    const gchar clearkey_sys_id[] = "78f32170-d883-11e0-9572-0800200c9a66";
    gsize offset;

    data = gst_buffer_new ();
    mem = gst_allocator_alloc (NULL, 40, NULL);
    gst_buffer_insert_memory (data, -1, mem);
    for (offset = 0; offset < 40; offset += 4) {
      gst_buffer_fill (data, offset, "pssi", 4);
    }
    ASSERT_MINI_OBJECT_REFCOUNT (data, "data", 1);
    event = gst_event_new_protection (clearkey_sys_id, data, "test");
    fail_if (event == NULL);
    ASSERT_MINI_OBJECT_REFCOUNT (data, "data", 2);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_PROTECTION);
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
    gst_event_parse_protection (event, &parsed_id, &parsed_data,
        &parsed_origin);
    fail_if (parsed_id == NULL);
    fail_unless (g_strcmp0 (clearkey_sys_id, parsed_id) == 0);
    fail_if (parsed_data == NULL);
    fail_if (parsed_data != data);
    ASSERT_MINI_OBJECT_REFCOUNT (data, "data", 2);
    fail_if (parsed_origin == NULL);
    fail_unless (g_strcmp0 ("test", parsed_origin) == 0);
    gst_event_unref (event);
    ASSERT_MINI_OBJECT_REFCOUNT (data, "data", 1);
    gst_buffer_unref (data);
  }

  /* Custom event types */
  {
    structure = gst_structure_new_empty ("application/x-custom");
    fail_if (structure == NULL);
    event = gst_event_new_custom (GST_EVENT_CUSTOM_UPSTREAM, structure);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_UPSTREAM);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));
    fail_unless (gst_event_get_structure (event) == structure);
    fail_unless (gst_event_has_name (event, "application/x-custom"));
    gst_event_unref (event);

    /* Decided not to test the other custom enum types, as they
     * only differ by the value of the enum passed to gst_event_new_custom
     */
  }

  /* Event copying */
  {
    structure = gst_structure_new_empty ("application/x-custom");
    fail_if (structure == NULL);
    event = gst_event_new_custom (GST_EVENT_CUSTOM_BOTH, structure);

    fail_if (event == NULL);
    event2 = gst_event_copy (event);
    fail_if (event2 == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_TYPE (event2));
    fail_unless (gst_event_has_name (event, "application/x-custom"));

    /* The structure should have been duplicated */
    fail_if (gst_event_get_structure (event) ==
        gst_event_get_structure (event2));

    gst_event_unref (event);
    gst_event_unref (event2);
  }

  /* Make events writable */
  {
    structure = gst_structure_new_empty ("application/x-custom");
    fail_if (structure == NULL);
    event = gst_event_new_custom (GST_EVENT_CUSTOM_BOTH, structure);
    /* ref the event so that it becomes non-writable */
    gst_event_ref (event);
    gst_event_ref (event);
    /* this should fail if the structure isn't writable */
    ASSERT_CRITICAL (gst_structure_remove_all_fields ((GstStructure *)
            gst_event_get_structure (event)));
    fail_unless (gst_event_has_name (event, "application/x-custom"));

    /* now make writable */
    event2 =
        GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));
    fail_unless (event != event2);
    /* this fail if the structure isn't writable */
    gst_structure_remove_all_fields ((GstStructure *)
        gst_event_get_structure (event2));
    fail_unless (gst_event_has_name (event2, "application/x-custom"));

    gst_event_unref (event);
    gst_event_unref (event);
    gst_event_unref (event2);
  }
}

GST_END_TEST;

static GTimeVal sent_event_time;
static GstEvent *got_event_before_q, *got_event_after_q;
static GTimeVal got_event_time;

static GstPadProbeReturn
event_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstMiniObject *data = GST_PAD_PROBE_INFO_DATA (info);
  gboolean before_q = (gboolean) GPOINTER_TO_INT (user_data);

  GST_DEBUG ("event probe called %p", data);

  fail_unless (GST_IS_EVENT (data));

  if (before_q) {
    switch (GST_EVENT_TYPE (GST_EVENT (data))) {
      case GST_EVENT_CUSTOM_UPSTREAM:
      case GST_EVENT_CUSTOM_BOTH:
      case GST_EVENT_CUSTOM_BOTH_OOB:
        if (got_event_before_q != NULL)
          break;
        gst_event_ref ((GstEvent *) data);
        g_get_current_time (&got_event_time);
        got_event_before_q = GST_EVENT (data);
        break;
      default:
        break;
    }
  } else {
    switch (GST_EVENT_TYPE (GST_EVENT (data))) {
      case GST_EVENT_CUSTOM_DOWNSTREAM:
      case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
      case GST_EVENT_CUSTOM_BOTH:
      case GST_EVENT_CUSTOM_BOTH_OOB:
        if (got_event_after_q != NULL)
          break;
        gst_event_ref ((GstEvent *) data);
        g_get_current_time (&got_event_time);
        got_event_after_q = GST_EVENT (data);
        break;
      default:
        break;
    }
  }

  return GST_PAD_PROBE_OK;
}


typedef struct
{
  GMutex lock;
  GCond cond;
  gboolean signaled;
} SignalData;

static void
signal_data_init (SignalData * data)
{
  GST_DEBUG ("init %p", data);
  g_mutex_init (&data->lock);
  g_cond_init (&data->cond);
  data->signaled = FALSE;
}

static void
signal_data_cleanup (SignalData * data)
{
  GST_DEBUG ("free %p", data);
  g_mutex_clear (&data->lock);
  g_cond_clear (&data->cond);
}

static void
signal_data_signal (SignalData * data)
{
  g_mutex_lock (&data->lock);
  data->signaled = TRUE;
  g_cond_broadcast (&data->cond);
  GST_DEBUG ("signaling %p", data);
  g_mutex_unlock (&data->lock);
}

static void
signal_data_wait (SignalData * data)
{
  g_mutex_lock (&data->lock);
  GST_DEBUG ("signal wait %p", data);
  while (!data->signaled)
    g_cond_wait (&data->cond, &data->lock);
  GST_DEBUG ("signal wait done %p", data);
  g_mutex_unlock (&data->lock);
}

static GstPadProbeReturn
signal_blocked (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  SignalData *data = (SignalData *) user_data;

  GST_DEBUG ("signal called %p", data);
  signal_data_signal (data);
  GST_DEBUG ("signal done %p", data);

  return GST_PAD_PROBE_OK;
}

static void test_event
    (GstBin * pipeline, GstEventType type, GstPad * pad,
    gboolean expect_before_q, GstPad * fake_srcpad)
{
  GstEvent *event;
  GstPad *peer;
  gint i;
  SignalData data;
  gulong id;

  got_event_before_q = got_event_after_q = NULL;

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  GST_DEBUG ("test event called");

  event = gst_event_new_custom (type,
      gst_structure_new_empty ("application/x-custom"));
  g_get_current_time (&sent_event_time);
  got_event_time.tv_sec = 0;
  got_event_time.tv_usec = 0;

  signal_data_init (&data);

  /* We block the pad so the stream lock is released and we can send the event */
  id = gst_pad_add_probe (fake_srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      signal_blocked, &data, NULL);
  fail_unless (id != 0);

  signal_data_wait (&data);

  /* We send on the peer pad, since the pad is blocked */
  GST_DEBUG ("sending event %p", event);
  fail_unless ((peer = gst_pad_get_peer (pad)) != NULL);
  gst_pad_send_event (peer, event);
  gst_object_unref (peer);

  gst_pad_remove_probe (fake_srcpad, id);

  if (expect_before_q) {
    /* Wait up to 5 seconds for the event to appear */
    for (i = 0; i < 500; i++) {
      g_usleep (G_USEC_PER_SEC / 100);
      if (got_event_before_q != NULL)
        break;
    }
    fail_if (got_event_before_q == NULL,
        "Expected event failed to appear upstream of the queue "
        "within 5 seconds");
    fail_unless (GST_EVENT_TYPE (got_event_before_q) == type);
  } else {
    /* Wait up to 10 seconds for the event to appear */
    for (i = 0; i < 1000; i++) {
      g_usleep (G_USEC_PER_SEC / 100);
      if (got_event_after_q != NULL)
        break;
    }
    fail_if (got_event_after_q == NULL,
        "Expected event failed to appear after the queue within 10 seconds");
    fail_unless (GST_EVENT_TYPE (got_event_after_q) == type);
  }

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  if (got_event_before_q)
    gst_event_unref (got_event_before_q);
  if (got_event_after_q)
    gst_event_unref (got_event_after_q);

  got_event_before_q = got_event_after_q = NULL;

  signal_data_cleanup (&data);
}

static gint64
timediff (GTimeVal * end, GTimeVal * start)
{
  return (end->tv_sec - start->tv_sec) * G_USEC_PER_SEC +
      (end->tv_usec - start->tv_usec);
}

GST_START_TEST (send_custom_events)
{
  /* Run some tests on custom events. Checking for serialisation and whatnot.
   * pipeline is fakesrc ! queue ! fakesink */
  GstBin *pipeline;
  GstElement *fakesrc, *fakesink, *queue;
  GstPad *srcpad, *sinkpad;

  fail_if ((pipeline = (GstBin *) gst_pipeline_new ("testpipe")) == NULL);
  fail_if ((fakesrc = gst_element_factory_make ("fakesrc", NULL)) == NULL);
  fail_if ((fakesink = gst_element_factory_make ("fakesink", NULL)) == NULL);
  fail_if ((queue = gst_element_factory_make ("queue", NULL)) == NULL);

  gst_bin_add_many (pipeline, fakesrc, queue, fakesink, NULL);
  fail_unless (gst_element_link_many (fakesrc, queue, fakesink, NULL));

  g_object_set (G_OBJECT (fakesink), "sync", FALSE, NULL);

  /* Send 100 buffers per sec */
  g_object_set (G_OBJECT (fakesrc), "silent", TRUE, "datarate", 100,
      "sizemax", 1, "sizetype", 2, NULL);
  g_object_set (G_OBJECT (queue), "max-size-buffers", 0, "max-size-time",
      (guint64) GST_SECOND, "max-size-bytes", 0, NULL);
  g_object_set (G_OBJECT (fakesink), "silent", TRUE, "sync", TRUE, NULL);

  /* add pad-probes to faksrc.src and fakesink.sink */
  fail_if ((srcpad = gst_element_get_static_pad (fakesrc, "src")) == NULL);
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      event_probe, GINT_TO_POINTER (TRUE), NULL);

  fail_if ((sinkpad = gst_element_get_static_pad (fakesink, "sink")) == NULL);
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      event_probe, GINT_TO_POINTER (FALSE), NULL);

  /* Upstream events */
  test_event (pipeline, GST_EVENT_CUSTOM_UPSTREAM, sinkpad, TRUE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) < G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_UP took too long to reach source: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  test_event (pipeline, GST_EVENT_CUSTOM_BOTH, sinkpad, TRUE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) < G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_BOTH took too long to reach source: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  test_event (pipeline, GST_EVENT_CUSTOM_BOTH_OOB, sinkpad, TRUE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) < G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_BOTH_OOB took too long to reach source: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  /* Out of band downstream events */
  test_event (pipeline, GST_EVENT_CUSTOM_DOWNSTREAM_OOB, srcpad, FALSE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) < G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_DS_OOB took too long to reach source: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  test_event (pipeline, GST_EVENT_CUSTOM_BOTH_OOB, srcpad, FALSE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) < G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_BOTH_OOB took too long to reach source: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  /* In-band downstream events are expected to take at least 1 second
   * to traverse the queue */
  test_event (pipeline, GST_EVENT_CUSTOM_DOWNSTREAM, srcpad, FALSE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) >= G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_DS arrived too quickly for an in-band event: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  test_event (pipeline, GST_EVENT_CUSTOM_BOTH, srcpad, FALSE, srcpad);
  fail_unless (timediff (&got_event_time,
          &sent_event_time) >= G_USEC_PER_SEC / 2,
      "GST_EVENT_CUSTOM_BOTH arrived too quickly for an in-band event: %"
      G_GINT64_FORMAT " us", timediff (&got_event_time, &sent_event_time));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gst_event_suite (void)
{
  Suite *s = suite_create ("GstEvent");
  TCase *tc_chain = tcase_create ("events");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, create_events);
  tcase_add_test (tc_chain, send_custom_events);
  return s;
}

GST_CHECK_MAIN (gst_event);
