/* GStreamer
 *
 * some unit tests for GstBaseSrc
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstconsistencychecker.h>
#include <gst/base/gstbasesrc.h>

static GstPadProbeReturn
eos_event_counter (GstObject * pad, GstPadProbeInfo * info, guint * p_num_eos)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

  fail_unless (event != NULL);
  fail_unless (GST_IS_EVENT (event));

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
    *p_num_eos += 1;

  return GST_PAD_PROBE_OK;
}

/* basesrc_eos_events_push_live_op:
 *  - make sure source does send an EOS event when operating in push
 *    mode and being set to READY explicitly (like one might with
 *    live sources)
 */
GST_START_TEST (basesrc_eos_events_push_live_op)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *srcpad;
  guint probe, num_eos = 0;
  GstStreamConsistency *consistency;
  GstEvent *eos_event;
  guint32 eos_event_seqnum;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", TRUE, NULL);
  g_object_set (sink, "can-activate-pull", FALSE, NULL);

  g_object_set (src, "can-activate-push", TRUE, NULL);
  g_object_set (src, "can-activate-pull", FALSE, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  consistency = gst_consistency_checker_new (srcpad);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  bus = gst_element_get_bus (pipe);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* wait a second, then do controlled shutdown */
  g_usleep (GST_USECOND * 1);

  /* shut down pipeline (should send EOS message) ... */
  eos_event = gst_event_new_eos ();
  eos_event_seqnum = gst_event_get_seqnum (eos_event);
  gst_element_send_event (pipe, eos_event);

  /* ... and wait for the EOS message from the sink */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* should be exactly one EOS event */
  fail_unless (num_eos == 1);
  fail_unless (gst_message_get_seqnum (msg) == eos_event_seqnum);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent a second one when going PAUSED => READY */
  fail_unless (num_eos == 1);

  gst_consistency_checker_free (consistency);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;



/* basesrc_eos_events_push:
 *  - make sure source only sends one EOS when operating in push-mode,
 *    reaching the max number of buffers, and is then shut down.
 */
GST_START_TEST (basesrc_eos_events_push)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *srcpad;
  guint probe, num_eos = 0;
  GstStreamConsistency *consistency;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", TRUE, NULL);
  g_object_set (sink, "can-activate-pull", FALSE, NULL);

  g_object_set (src, "can-activate-push", TRUE, NULL);
  g_object_set (src, "can-activate-pull", FALSE, NULL);
  g_object_set (src, "num-buffers", 8, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  consistency = gst_consistency_checker_new (srcpad);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  bus = gst_element_get_bus (pipe);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* should be exactly one EOS event */
  fail_unless (num_eos == 1);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent a second one when going PAUSED => READY */
  fail_unless (num_eos == 1);

  gst_consistency_checker_free (consistency);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;

/* basesrc_eos_events_pull_live_op:
 *  - make sure source doesn't send an EOS event when operating in
 *    pull mode and being set to READY explicitly (like one might with
 *    live sources)
 */
GST_START_TEST (basesrc_eos_events_pull_live_op)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstPad *srcpad;
  guint probe, num_eos = 0;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", FALSE, NULL);
  g_object_set (sink, "can-activate-pull", TRUE, NULL);

  g_object_set (src, "can-activate-push", FALSE, NULL);
  g_object_set (src, "can-activate-pull", TRUE, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* wait a second, then do controlled shutdown */
  g_usleep (GST_USECOND * 1);

  /* shut down source only ... */
  gst_element_set_state (src, GST_STATE_NULL);
  state_ret = gst_element_get_state (src, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  fail_unless (gst_element_set_locked_state (src, TRUE) == TRUE);

  /* source shouldn't have sent any EOS event in pull mode */
  fail_unless (num_eos == 0);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent an EOS when going PAUSED => READY either */
  fail_unless (num_eos == 0);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

/* basesrc_eos_events_pull:
 *  - makes sure source doesn't send EOS event when reaching the max.
 *    number of buffers configured in pull-mode
 *  - make sure source doesn't send EOS event either when being shut down
 *    (PAUSED => READY state change) after EOSing in pull mode 
 */
GST_START_TEST (basesrc_eos_events_pull)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *srcpad;
  guint probe, num_eos = 0;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", FALSE, NULL);
  g_object_set (sink, "can-activate-pull", TRUE, NULL);

  g_object_set (src, "can-activate-push", FALSE, NULL);
  g_object_set (src, "can-activate-pull", TRUE, NULL);
  g_object_set (src, "num-buffers", 8, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  bus = gst_element_get_bus (pipe);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* source shouldn't have sent any EOS event in pull mode */
  fail_unless (num_eos == 0);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent an EOS when going PAUSED => READY either */
  fail_unless (num_eos == 0);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;


/* basesrc_eos_events_push_live_eos:
 *  - make sure the source stops and emits EOS when we send an EOS event to the
 *    pipeline.
 */
GST_START_TEST (basesrc_eos_events_push_live_eos)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *srcpad;
  guint probe, num_eos = 0;
  gboolean res;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", TRUE, NULL);
  g_object_set (sink, "can-activate-pull", FALSE, NULL);

  g_object_set (src, "can-activate-push", TRUE, NULL);
  g_object_set (src, "can-activate-pull", FALSE, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  bus = gst_element_get_bus (pipe);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* wait a second, then emit the EOS */
  g_usleep (GST_USECOND * 1);

  /* shut down source only (should send EOS event) ... */
  res = gst_element_send_event (pipe, gst_event_new_eos ());
  fail_unless (res == TRUE);

  /* ... and wait for the EOS message from the sink */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* should be exactly one EOS event */
  fail_unless (num_eos == 1);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent a second one when going PAUSED => READY */
  fail_unless (num_eos == 1);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;

/* basesrc_eos_events_pull_live_eos:
 *  - make sure the source stops and emits EOS when we send an EOS event to the
 *    pipeline.
 */
GST_START_TEST (basesrc_eos_events_pull_live_eos)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *srcpad;
  guint probe, num_eos = 0;
  gboolean res;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  g_object_set (sink, "can-activate-push", FALSE, NULL);
  g_object_set (sink, "can-activate-pull", TRUE, NULL);

  g_object_set (src, "can-activate-push", FALSE, NULL);
  g_object_set (src, "can-activate-pull", TRUE, NULL);

  /* set up event probe to count EOS events */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);

  probe = gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) eos_event_counter, &num_eos, NULL);

  bus = gst_element_get_bus (pipe);

  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* wait a second, then emit the EOS */
  g_usleep (GST_USECOND * 1);

  /* shut down source only (should send EOS event) ... */
  res = gst_element_send_event (pipe, gst_event_new_eos ());
  fail_unless (res == TRUE);

  /* ... and wait for the EOS message from the sink */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* no EOS in pull mode */
  fail_unless (num_eos == 0);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  /* make sure source hasn't sent a second one when going PAUSED => READY */
  fail_unless (num_eos == 0);

  gst_pad_remove_probe (srcpad, probe);
  gst_object_unref (srcpad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;


static GstPadProbeReturn
segment_event_catcher (GstObject * pad, GstPadProbeInfo * info,
    gpointer * user_data)
{
  GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
  GstEvent **last_event = (GstEvent **) user_data;
  fail_unless (event != NULL);
  fail_unless (GST_IS_EVENT (event));
  fail_unless (user_data != NULL);

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    g_mutex_lock (&check_mutex);
    fail_unless (*last_event == NULL);
    *last_event = gst_event_copy (event);
    g_cond_signal (&check_cond);
    g_mutex_unlock (&check_mutex);
  }

  return GST_PAD_PROBE_OK;
}

/* basesrc_seek_events_rate_update:
 *  - make sure we get expected segment after sending a seek event
 */
GST_START_TEST (basesrc_seek_events_rate_update)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *probe_pad;
  guint probe;
  GstEvent *seg_event = NULL;
  GstEvent *rate_seek;
  gboolean event_ret;
  const GstSegment *segment;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipe);

  /* set up event probe to catch new segment event */
  probe_pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (probe_pad != NULL);

  probe = gst_pad_add_probe (probe_pad, GST_PAD_PROBE_TYPE_EVENT_BOTH,
      (GstPadProbeCallback) segment_event_catcher, &seg_event, NULL);

  /* prepare the seek */
  rate_seek = gst_event_new_seek (0.5, GST_FORMAT_TIME, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE,
      GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE);

  GST_INFO ("going to playing");

  /* play */
  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* wait for the first segment to be posted, and flush it ... */
  g_mutex_lock (&check_mutex);
  while (seg_event == NULL)
    g_cond_wait (&check_cond, &check_mutex);
  gst_event_unref (seg_event);
  seg_event = NULL;
  g_mutex_unlock (&check_mutex);

  GST_INFO ("seeking");

  /* seek */
  event_ret = gst_element_send_event (pipe, rate_seek);
  fail_unless (event_ret == TRUE);

  /* wait for the updated segment to be posted, posting EOS make the loop
   * thread exit before the updated segment is posted ... */
  g_mutex_lock (&check_mutex);
  while (seg_event == NULL)
    g_cond_wait (&check_cond, &check_mutex);
  g_mutex_unlock (&check_mutex);

  /* shut down pipeline only (should send EOS message) ... */
  gst_element_send_event (pipe, gst_event_new_eos ());

  /* ... and wait for the EOS message from the sink */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  GST_INFO ("stopped");

  /* check that we have go the event */
  fail_unless (seg_event != NULL);

  gst_event_parse_segment (seg_event, &segment);
  fail_unless (segment->rate == 0.5);

  gst_pad_remove_probe (probe_pad, probe);
  gst_object_unref (probe_pad);
  gst_message_unref (msg);
  gst_event_unref (seg_event);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;


typedef struct
{
  gboolean seeked;
  gint buffer_count;
  GList *events;
} LastBufferSeekData;

static GstPadProbeReturn
seek_on_buffer (GstObject * pad, GstPadProbeInfo * info, gpointer * user_data)
{
  LastBufferSeekData *data = (LastBufferSeekData *) user_data;

  fail_unless (user_data != NULL);

  if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
    data->buffer_count++;

    if (!data->seeked) {
      fail_unless (gst_pad_push_event (GST_PAD (pad),
              gst_event_new_seek (1.0, GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH,
                  GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 1)));
      data->seeked = TRUE;
    }
  } else if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    data->events = g_list_append (data->events, gst_event_ref (info->data));
  } else {
    fail ("Should not be reached");
  }
  return GST_PAD_PROBE_OK;
}

/* basesrc_seek_on_last_buffer:
 *  - make sure basesrc doesn't go eos if a seek is sent
 * after the last buffer push
 *
 * This is just a test and is a controlled environment.
 * For testing purposes sending the seek from the streaming
 * thread is ok but doing this in an application might not
 * be a good idea.
 */
GST_START_TEST (basesrc_seek_on_last_buffer)
{
  GstStateChangeReturn state_ret;
  GstElement *src, *sink, *pipe;
  GstMessage *msg;
  GstBus *bus;
  GstPad *probe_pad;
  guint probe;
  GstEvent *seek;
  LastBufferSeekData seek_data;

  pipe = gst_pipeline_new ("pipeline");
  sink = gst_element_factory_make ("fakesink", "sink");
  src = gst_element_factory_make ("fakesrc", "src");

  g_assert (pipe != NULL);
  g_assert (sink != NULL);
  g_assert (src != NULL);

  /* use 'sizemax' buffers to avoid receiving empty buffers */
  g_object_set (src, "sizetype", 2, NULL);

  fail_unless (gst_bin_add (GST_BIN (pipe), src) == TRUE);
  fail_unless (gst_bin_add (GST_BIN (pipe), sink) == TRUE);

  fail_unless (gst_element_link (src, sink) == TRUE);

  bus = gst_element_get_bus (pipe);

  /* set up probe to catch the last buffer and send a seek event */
  probe_pad = gst_element_get_static_pad (sink, "sink");
  fail_unless (probe_pad != NULL);

  seek_data.buffer_count = 0;
  seek_data.seeked = FALSE;
  seek_data.events = NULL;

  probe =
      gst_pad_add_probe (probe_pad,
      GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) seek_on_buffer, &seek_data, NULL);

  /* prepare the segment so that it has only one buffer */
  seek = gst_event_new_seek (1, GST_FORMAT_BYTES, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 1);

  gst_element_set_state (pipe, GST_STATE_READY);
  fail_unless (gst_element_send_event (src, seek));

  GST_INFO ("going to playing");

  /* play */
  gst_element_set_state (pipe, GST_STATE_PLAYING);
  state_ret = gst_element_get_state (pipe, NULL, NULL, -1);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* ... and wait for the EOS message from the sink */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ERROR);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_element_get_state (pipe, NULL, NULL, -1);

  GST_INFO ("stopped");

  /* check that we have go the event */
  fail_unless (seek_data.buffer_count == 2);
  fail_unless (seek_data.seeked);

  /* events: stream-start -> segment -> segment -> eos */
  fail_unless (g_list_length (seek_data.events) == 4);
  {
    GstEvent *event;

    event = seek_data.events->data;
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_STREAM_START);
    gst_event_unref (event);
    seek_data.events = g_list_delete_link (seek_data.events, seek_data.events);

    event = seek_data.events->data;
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
    gst_event_unref (event);
    seek_data.events = g_list_delete_link (seek_data.events, seek_data.events);

    event = seek_data.events->data;
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT);
    gst_event_unref (event);
    seek_data.events = g_list_delete_link (seek_data.events, seek_data.events);

    event = seek_data.events->data;
    fail_unless (GST_EVENT_TYPE (event) == GST_EVENT_EOS);
    gst_event_unref (event);
    seek_data.events = g_list_delete_link (seek_data.events, seek_data.events);
  }

  gst_pad_remove_probe (probe_pad, probe);
  gst_object_unref (probe_pad);
  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_object_unref (pipe);
}

GST_END_TEST;

static Suite *
gst_basesrc_suite (void)
{
  Suite *s = suite_create ("GstBaseSrc");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, basesrc_eos_events_pull);
  tcase_add_test (tc, basesrc_eos_events_push);
  tcase_add_test (tc, basesrc_eos_events_push_live_op);
  tcase_add_test (tc, basesrc_eos_events_pull_live_op);
  tcase_add_test (tc, basesrc_eos_events_push_live_eos);
  tcase_add_test (tc, basesrc_eos_events_pull_live_eos);
  tcase_add_test (tc, basesrc_seek_events_rate_update);
  tcase_add_test (tc, basesrc_seek_on_last_buffer);

  return s;
}

GST_CHECK_MAIN (gst_basesrc);
