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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


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
    event = gst_event_new_flush_stop ();
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_FLUSH_STOP);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));
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
  /* NEWSEGMENT */
  {
    gdouble rate, applied_rate;
    GstFormat format;
    gint64 start, end, base;
    gboolean update;

    event =
        gst_event_new_new_segment (FALSE, 0.5, GST_FORMAT_TIME, 1, G_MAXINT64,
        0xdeadbeef);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_new_segment (event, &update, &rate, &format, &start, &end,
        &base);
    fail_unless (update == FALSE);
    fail_unless (rate == 0.5);
    fail_unless (format == GST_FORMAT_TIME);
    fail_unless (start == 1);
    fail_unless (end == G_MAXINT64);
    fail_unless (base == 0xdeadbeef);

    /* Check that the new segment was created with applied_rate of 1.0 */
    gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
        &format, &start, &end, &base);

    fail_unless (update == FALSE);
    fail_unless (rate == 0.5);
    fail_unless (applied_rate == 1.0);
    fail_unless (format == GST_FORMAT_TIME);
    fail_unless (start == 1);
    fail_unless (end == G_MAXINT64);

    gst_event_unref (event);

    event =
        gst_event_new_new_segment_full (TRUE, 0.75, 0.5, GST_FORMAT_BYTES, 0,
        G_MAXINT64 - 1, 0xdeadbeef);

    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_NEWSEGMENT);
    fail_if (GST_EVENT_IS_UPSTREAM (event));
    fail_unless (GST_EVENT_IS_DOWNSTREAM (event));
    fail_unless (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
        &format, &start, &end, &base);

    fail_unless (update == TRUE);
    fail_unless (rate == 0.75);
    fail_unless (applied_rate == 0.5);
    fail_unless (format == GST_FORMAT_BYTES);
    fail_unless (start == 0);
    fail_unless (end == (G_MAXINT64 - 1));

    gst_event_unref (event);
  }

  /* TAGS */
  {
    GstTagList *taglist = gst_tag_list_new ();
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
    gdouble p1 = 1.0, p2;
    GstClockTimeDiff ctd1 = G_GINT64_CONSTANT (10), ctd2;
    GstClockTime ct1 = G_GUINT64_CONSTANT (20), ct2;

    event = gst_event_new_qos (p1, ctd1, ct1);
    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_QOS);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_qos (event, &p2, &ctd2, &ct2);
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
    GstSeekType cur_type, stop_type;
    gint64 cur, stop;

    event = gst_event_new_seek (0.5, GST_FORMAT_BYTES,
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
        GST_SEEK_TYPE_SET, 1, GST_SEEK_TYPE_NONE, 0xdeadbeef);

    fail_if (event == NULL);
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEEK);
    fail_unless (GST_EVENT_IS_UPSTREAM (event));
    fail_if (GST_EVENT_IS_DOWNSTREAM (event));
    fail_if (GST_EVENT_IS_SERIALIZED (event));

    gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
        &stop_type, &stop);
    fail_unless (rate == 0.5);
    fail_unless (format == GST_FORMAT_BYTES);
    fail_unless (flags == (GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
    fail_unless (cur_type == GST_SEEK_TYPE_SET);
    fail_unless (cur == 1);
    fail_unless (stop_type == GST_SEEK_TYPE_NONE);
    fail_unless (stop == 0xdeadbeef);

    gst_event_unref (event);
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

  /* Custom event types */
  {
    structure = gst_structure_empty_new ("application/x-custom");
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
    structure = gst_structure_empty_new ("application/x-custom");
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
    structure = gst_structure_empty_new ("application/x-custom");
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

static gboolean
event_probe (GstPad * pad, GstMiniObject ** data, gpointer user_data)
{
  gboolean before_q = (gboolean) GPOINTER_TO_INT (user_data);

  fail_unless (GST_IS_EVENT (data));

  GST_DEBUG ("event probe called");

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

  return TRUE;
}

static void test_event
    (GstBin * pipeline, GstEventType type, GstPad * pad,
    gboolean expect_before_q, GstPad * fake_srcpad)
{
  GstEvent *event;
  GstPad *peer;
  gint i;

  got_event_before_q = got_event_after_q = NULL;

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  GST_DEBUG ("test event called");

  event = gst_event_new_custom (type,
      gst_structure_empty_new ("application/x-custom"));
  g_get_current_time (&sent_event_time);
  got_event_time.tv_sec = 0;
  got_event_time.tv_usec = 0;

  /* We block the pad so the stream lock is released and we can send the event */
  fail_unless (gst_pad_set_blocked (fake_srcpad, TRUE) == TRUE);

  /* We send on the peer pad, since the pad is blocked */
  fail_unless ((peer = gst_pad_get_peer (pad)) != NULL);
  gst_pad_send_event (peer, event);
  gst_object_unref (peer);

  fail_unless (gst_pad_set_blocked (fake_srcpad, FALSE) == TRUE);

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
  gst_pad_add_event_probe (srcpad, (GCallback) event_probe,
      GINT_TO_POINTER (TRUE));

  fail_if ((sinkpad = gst_element_get_static_pad (fakesink, "sink")) == NULL);
  gst_pad_add_event_probe (sinkpad, (GCallback) event_probe,
      GINT_TO_POINTER (FALSE));

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
   * to traverse the the queue */
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
