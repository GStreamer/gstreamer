/* GStreamer unit tests for multiqueue
 *
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
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

static GMutex _check_lock;

static GstElement *
setup_multiqueue (GstElement * pipe, GstElement * inputs[],
    GstElement * outputs[], guint num)
{
  GstElement *mq;
  guint i;

  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL, "failed to create 'multiqueue' element");

  gst_bin_add (GST_BIN (pipe), mq);

  for (i = 0; i < num; ++i) {
    GstPad *sinkpad = NULL;
    GstPad *srcpad = NULL;

    /* create multiqueue sink (and source) pad */
    sinkpad = gst_element_get_request_pad (mq, "sink_%u");
    fail_unless (sinkpad != NULL,
        "failed to create multiqueue request pad #%u", i);

    /* link input element N to the N-th multiqueue sink pad we just created */
    if (inputs != NULL && inputs[i] != NULL) {
      gst_bin_add (GST_BIN (pipe), inputs[i]);

      srcpad = gst_element_get_static_pad (inputs[i], "src");
      fail_unless (srcpad != NULL, "failed to find src pad for input #%u", i);

      fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));

      gst_object_unref (srcpad);
      srcpad = NULL;
    }
    gst_object_unref (sinkpad);
    sinkpad = NULL;

    /* link output element N to the N-th multiqueue src pad */
    if (outputs != NULL && outputs[i] != NULL) {
      gchar padname[10];

      /* only the sink pads are by request, the source pads are sometimes pads,
       * so this should return NULL */
      srcpad = gst_element_get_request_pad (mq, "src_%u");
      fail_unless (srcpad == NULL);

      g_snprintf (padname, sizeof (padname), "src_%u", i);
      srcpad = gst_element_get_static_pad (mq, padname);
      fail_unless (srcpad != NULL, "failed to get multiqueue src pad #%u", i);
      fail_unless (GST_PAD_IS_SRC (srcpad),
          "%s:%s is not a source pad?!", GST_DEBUG_PAD_NAME (srcpad));

      gst_bin_add (GST_BIN (pipe), outputs[i]);

      sinkpad = gst_element_get_static_pad (outputs[i], "sink");
      fail_unless (sinkpad != NULL, "failed to find sink pad of output #%u", i);
      fail_unless (GST_PAD_IS_SINK (sinkpad));

      fail_unless_equals_int (GST_PAD_LINK_OK, gst_pad_link (srcpad, sinkpad));

      gst_object_unref (srcpad);
      gst_object_unref (sinkpad);
    }
  }

  return mq;
}

GST_START_TEST (test_simple_pipeline)
{
  GstElement *pipe;
  GstElement *inputs[1];
  GstElement *outputs[1];
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  inputs[0] = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (inputs[0] != NULL, "failed to create 'fakesrc' element");
  g_object_set (inputs[0], "num-buffers", 256, NULL);

  outputs[0] = gst_element_factory_make ("fakesink", NULL);
  fail_unless (outputs[0] != NULL, "failed to create 'fakesink' element");

  setup_multiqueue (pipe, inputs, outputs, 1);

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected EOS message, got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("Got EOS, cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_simple_shutdown_while_running)
{
  GstElement *pipe;
  GstElement *inputs[1];
  GstElement *outputs[1];
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  inputs[0] = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (inputs[0] != NULL, "failed to create 'fakesrc' element");

  outputs[0] = gst_element_factory_make ("fakesink", NULL);
  fail_unless (outputs[0] != NULL, "failed to create 'fakesink' element");

  setup_multiqueue (pipe, inputs, outputs, 1);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* wait until pipeline is up and running */
  msg = gst_bus_poll (GST_ELEMENT_BUS (pipe),
      GST_MESSAGE_ERROR | GST_MESSAGE_ASYNC_DONE, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR, "Got ERROR message");
  gst_message_unref (msg);

  GST_LOG ("pipeline is running now");
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* wait a bit to accumulate some buffers in the queue (while it's blocking
   * in the sink) */
  msg =
      gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, GST_SECOND / 4);
  if (msg)
    g_error ("Got ERROR message");

  /* now shut down only the sink, so the queue gets a wrong-state flow return */
  gst_element_set_state (outputs[0], GST_STATE_NULL);
  msg =
      gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR, GST_SECOND / 2);
  if (msg)
    g_error ("Got ERROR message");

  GST_LOG ("Cleaning up");

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_simple_create_destroy)
{
  GstElement *mq;

  mq = gst_element_factory_make ("multiqueue", NULL);
  gst_object_unref (mq);
}

GST_END_TEST;

GST_START_TEST (test_request_pads)
{
  GstElement *mq;
  GstPad *sink1, *sink2;

  mq = gst_element_factory_make ("multiqueue", NULL);

  sink1 = gst_element_get_request_pad (mq, "foo_%u");
  fail_unless (sink1 == NULL,
      "Expected NULL pad, as there is no request pad template for 'foo_%%u'");

  sink1 = gst_element_get_request_pad (mq, "src_%u");
  fail_unless (sink1 == NULL,
      "Expected NULL pad, as there is no request pad template for 'src_%%u'");

  sink1 = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (sink1 != NULL);
  fail_unless (GST_IS_PAD (sink1));
  fail_unless (GST_PAD_IS_SINK (sink1));
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink1));

  sink2 = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (sink2 != NULL);
  fail_unless (GST_IS_PAD (sink2));
  fail_unless (GST_PAD_IS_SINK (sink2));
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink2));

  fail_unless (sink1 != sink2);

  GST_LOG ("Cleaning up");
  gst_object_unref (sink1);
  gst_object_unref (sink2);
  gst_object_unref (mq);
}

GST_END_TEST;

static GstPad *
mq_sinkpad_to_srcpad (GstElement * mq, GstPad * sink)
{
  GstPad *srcpad = NULL;

  gchar *mq_sinkpad_name;
  gchar *mq_srcpad_name;

  mq_sinkpad_name = gst_pad_get_name (sink);
  fail_unless (g_str_has_prefix (mq_sinkpad_name, "sink"));
  mq_srcpad_name = g_strdup_printf ("src_%s", mq_sinkpad_name + 5);
  srcpad = gst_element_get_static_pad (mq, mq_srcpad_name);
  fail_unless (srcpad != NULL);

  g_free (mq_sinkpad_name);
  g_free (mq_srcpad_name);

  return srcpad;
}

GST_START_TEST (test_request_pads_named)
{
  GstElement *mq;
  GstPad *sink1, *sink2, *sink3, *sink4;

  mq = gst_element_factory_make ("multiqueue", NULL);

  sink1 = gst_element_get_request_pad (mq, "sink_1");
  fail_unless (sink1 != NULL);
  fail_unless (GST_IS_PAD (sink1));
  fail_unless (GST_PAD_IS_SINK (sink1));
  fail_unless_equals_string (GST_PAD_NAME (sink1), "sink_1");
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink1));

  sink3 = gst_element_get_request_pad (mq, "sink_3");
  fail_unless (sink3 != NULL);
  fail_unless (GST_IS_PAD (sink3));
  fail_unless (GST_PAD_IS_SINK (sink3));
  fail_unless_equals_string (GST_PAD_NAME (sink3), "sink_3");
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink3));

  sink2 = gst_element_get_request_pad (mq, "sink_2");
  fail_unless (sink2 != NULL);
  fail_unless (GST_IS_PAD (sink2));
  fail_unless (GST_PAD_IS_SINK (sink2));
  fail_unless_equals_string (GST_PAD_NAME (sink2), "sink_2");
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink2));

  /* This gets us the first unused id, sink0 */
  sink4 = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (sink4 != NULL);
  fail_unless (GST_IS_PAD (sink4));
  fail_unless (GST_PAD_IS_SINK (sink4));
  fail_unless_equals_string (GST_PAD_NAME (sink4), "sink_0");
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink4));

  GST_LOG ("Cleaning up");
  gst_object_unref (sink1);
  gst_object_unref (sink2);
  gst_object_unref (sink3);
  gst_object_unref (sink4);
  gst_object_unref (mq);
}

GST_END_TEST;

static gboolean
mq_dummypad_query (GstPad * sinkpad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = (filter ? gst_caps_ref (filter) : gst_caps_new_any ());
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      break;
    }
    default:
      res = gst_pad_query_default (sinkpad, parent, query);
      break;
  }
  return res;
}

struct PadData
{
  GstPad *input_pad;
  GstPad *out_pad;
  guint8 pad_num;
  guint32 *max_linked_id_ptr;
  guint32 *eos_count_ptr;
  gboolean is_linked;
  gboolean first_buf;
  gint n_linked;

  GMutex *mutex;
  GCond *cond;

  /* used by initial_events_nodelay */
  gint event_count;
};

static GstFlowReturn
mq_dummypad_chain (GstPad * sinkpad, GstObject * parent, GstBuffer * buf)
{
  guint32 cur_id;
  struct PadData *pad_data;
  GstMapInfo info;

  pad_data = gst_pad_get_element_private (sinkpad);

  g_mutex_lock (&_check_lock);
  fail_if (pad_data == NULL);
  /* Read an ID from the first 4 bytes of the buffer data and check it's
   * what we expect */
  fail_unless (gst_buffer_map (buf, &info, GST_MAP_READ));
  fail_unless (info.size >= 4);
  g_mutex_unlock (&_check_lock);
  cur_id = GST_READ_UINT32_BE (info.data);
  gst_buffer_unmap (buf, &info);

  g_mutex_lock (pad_data->mutex);

  /* For not-linked pads, ensure that we're not running ahead of the 'linked'
   * pads. The first buffer is allowed to get ahead, because otherwise things can't
   * always pre-roll correctly */
  if (pad_data->max_linked_id_ptr) {
    if (!pad_data->is_linked) {
      /* If there are no linked pads, we can't track a max_id for them :) */
      if (pad_data->n_linked > 0 && !pad_data->first_buf) {
        g_mutex_lock (&_check_lock);
        fail_unless (cur_id <= *(pad_data->max_linked_id_ptr) + 1,
            "Got buffer %u on pad %u before buffer %u was seen on a "
            "linked pad (max: %u)", cur_id, pad_data->pad_num, cur_id - 1,
            *(pad_data->max_linked_id_ptr));
        g_mutex_unlock (&_check_lock);
      }
    } else {
      /* Update the max_id value */
      if (cur_id > *(pad_data->max_linked_id_ptr))
        *(pad_data->max_linked_id_ptr) = cur_id;
    }
  }
  pad_data->first_buf = FALSE;

  g_mutex_unlock (pad_data->mutex);

  /* Unref the buffer */
  gst_buffer_unref (buf);

  /* Return OK or not-linked as indicated */
  return pad_data->is_linked ? GST_FLOW_OK : GST_FLOW_NOT_LINKED;
}

static gboolean
mq_dummypad_event (GstPad * sinkpad, GstObject * parent, GstEvent * event)
{
  struct PadData *pad_data;

  pad_data = gst_pad_get_element_private (sinkpad);
  g_mutex_lock (&_check_lock);
  fail_if (pad_data == NULL);
  g_mutex_unlock (&_check_lock);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    g_mutex_lock (pad_data->mutex);

    /* Accumulate that we've seen the EOS and signal the main thread */
    if (pad_data->eos_count_ptr)
      *(pad_data->eos_count_ptr) += 1;

    GST_DEBUG ("EOS on pad %u", pad_data->pad_num);

    g_cond_broadcast (pad_data->cond);
    g_mutex_unlock (pad_data->mutex);
  }

  gst_event_unref (event);
  return TRUE;
}

static void
construct_n_pads (GstElement * mq, struct PadData *pad_data, gint n_pads,
    gint n_linked)
{
  gint i;
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  /* Construct NPADS dummy output pads. The first 'n_linked' return FLOW_OK, the rest
   * return NOT_LINKED. The not-linked ones check the expected ordering of 
   * output buffers */
  for (i = 0; i < n_pads; i++) {
    GstPad *mq_srcpad, *mq_sinkpad, *inpad, *outpad;
    gchar *name;

    name = g_strdup_printf ("dummysrc%d", i);
    inpad = gst_pad_new (name, GST_PAD_SRC);
    g_free (name);
    gst_pad_set_query_function (inpad, mq_dummypad_query);

    mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
    fail_unless (mq_sinkpad != NULL);
    fail_unless (gst_pad_link (inpad, mq_sinkpad) == GST_PAD_LINK_OK);

    gst_pad_set_active (inpad, TRUE);

    gst_pad_push_event (inpad, gst_event_new_stream_start ("test"));
    gst_pad_push_event (inpad, gst_event_new_segment (&segment));

    mq_srcpad = mq_sinkpad_to_srcpad (mq, mq_sinkpad);

    name = g_strdup_printf ("dummysink%d", i);
    outpad = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);
    gst_pad_set_chain_function (outpad, mq_dummypad_chain);
    gst_pad_set_event_function (outpad, mq_dummypad_event);
    gst_pad_set_query_function (outpad, mq_dummypad_query);

    pad_data[i].pad_num = i;
    pad_data[i].input_pad = inpad;
    pad_data[i].out_pad = outpad;
    pad_data[i].max_linked_id_ptr = NULL;
    pad_data[i].eos_count_ptr = NULL;
    pad_data[i].is_linked = (i < n_linked ? TRUE : FALSE);
    pad_data[i].n_linked = n_linked;
    pad_data[i].cond = NULL;
    pad_data[i].mutex = NULL;
    pad_data[i].first_buf = TRUE;
    gst_pad_set_element_private (outpad, pad_data + i);

    fail_unless (gst_pad_link (mq_srcpad, outpad) == GST_PAD_LINK_OK);
    gst_pad_set_active (outpad, TRUE);

    gst_object_unref (mq_sinkpad);
    gst_object_unref (mq_srcpad);
  }
}

static void
push_n_buffers (struct PadData *pad_data, gint num_buffers,
    const guint8 * pad_pattern, guint pattern_size)
{
  gint i;

  for (i = 0; i < num_buffers; i++) {
    guint8 cur_pad;
    GstBuffer *buf;
    GstFlowReturn ret;
    GstMapInfo info;

    cur_pad = pad_pattern[i % pattern_size];

    buf = gst_buffer_new_and_alloc (4);
    g_mutex_lock (&_check_lock);
    fail_if (buf == NULL);
    g_mutex_unlock (&_check_lock);

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
    GST_WRITE_UINT32_BE (info.data, i + 1);
    gst_buffer_unmap (buf, &info);
    GST_BUFFER_TIMESTAMP (buf) = (i + 1) * GST_SECOND;

    ret = gst_pad_push (pad_data[cur_pad].input_pad, buf);
    g_mutex_lock (&_check_lock);
    if (pad_data[cur_pad].is_linked) {
      fail_unless (ret == GST_FLOW_OK,
          "Push on pad %d returned %d when FLOW_OK was expected", cur_pad, ret);
    } else {
      /* Expect OK initially, then NOT_LINKED when the srcpad starts pushing */
      fail_unless (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED,
          "Push on pad %d returned %d when FLOW_OK or NOT_LINKED  was expected",
          cur_pad, ret);
    }
    g_mutex_unlock (&_check_lock);
  }
}

static void
run_output_order_test (gint n_linked)
{
  /* This test creates a multiqueue with 2 linked output, and 3 outputs that
   * return 'not-linked' when data is pushed, then verifies that all buffers
   * are received on not-linked pads only after earlier buffers on the
   * 'linked' pads are made */
  GstElement *pipe;
  GstElement *mq;
  struct PadData pad_data[5];
  guint32 max_linked_id;
  guint32 eos_seen;
  GMutex mutex;
  GCond cond;
  gint i;
  const gint NPADS = 5;
  const gint NBUFFERS = 1000;

  g_mutex_init (&mutex);
  g_cond_init (&cond);

  pipe = gst_bin_new ("testbin");

  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  /* No limits */
  g_object_set (mq,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0, "extra-size-time", (guint64) 0, NULL);

  construct_n_pads (mq, pad_data, NPADS, n_linked);
  for (i = 0; i < NPADS; i++) {
    pad_data[i].max_linked_id_ptr = &max_linked_id;
    /* Only look for EOS on the linked pads */
    pad_data[i].eos_count_ptr = (i < n_linked) ? &eos_seen : NULL;
    pad_data[i].cond = &cond;
    pad_data[i].mutex = &mutex;
  }

  /* Run the test. Push 1000 buffers through the multiqueue in a pattern */
  max_linked_id = 0;
  eos_seen = 0;
  gst_element_set_state (pipe, GST_STATE_PLAYING);

  {
    const guint8 pad_pattern[] =
        { 0, 0, 0, 0, 1, 1, 2, 1, 0, 2, 3, 2, 3, 1, 4 };
    const guint n = sizeof (pad_pattern) / sizeof (guint8);
    push_n_buffers (pad_data, NBUFFERS, pad_pattern, n);
  }

  for (i = 0; i < NPADS; i++) {
    gst_pad_push_event (pad_data[i].input_pad, gst_event_new_eos ());
  }

  /* Wait while the buffers are processed */
  g_mutex_lock (&mutex);
  /* We wait until EOS has been pushed on all linked pads */
  while (eos_seen < n_linked) {
    g_cond_wait (&cond, &mutex);
  }
  g_mutex_unlock (&mutex);

  /* Clean up */
  for (i = 0; i < NPADS; i++) {
    GstPad *mq_input = gst_pad_get_peer (pad_data[i].input_pad);

    gst_pad_unlink (pad_data[i].input_pad, mq_input);
    gst_element_release_request_pad (mq, mq_input);
    gst_object_unref (mq_input);
    gst_object_unref (pad_data[i].input_pad);
    gst_object_unref (pad_data[i].out_pad);
  }

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  g_cond_clear (&cond);
  g_mutex_clear (&mutex);
}

GST_START_TEST (test_output_order)
{
  run_output_order_test (2);
  run_output_order_test (0);
}

GST_END_TEST;

GST_START_TEST (test_not_linked_eos)
{
  /* This test creates a multiqueue with 1 linked output and 1 not-linked
   * pad. It pushes a few buffers through each, then EOS on the linked
   * pad and waits until that arrives. After that, it pushes some more
   * buffers on the not-linked pad and then EOS and checks that those
   * are all output */
  GstElement *pipe;
  GstElement *mq;
  struct PadData pad_data[2];
  guint32 eos_seen;
  GMutex mutex;
  GCond cond;
  gint i;
  const gint NPADS = 2;
  const gint NBUFFERS = 20;
  GstSegment segment;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  g_mutex_init (&mutex);
  g_cond_init (&cond);

  pipe = gst_bin_new ("testbin");

  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  /* No limits */
  g_object_set (mq,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0, "extra-size-time", (guint64) 0, NULL);

  /* Construct NPADS dummy output pads. The first 1 returns FLOW_OK, the rest
   * return NOT_LINKED. */
  construct_n_pads (mq, pad_data, NPADS, 1);
  for (i = 0; i < NPADS; i++) {
    /* Only look for EOS on the linked pads */
    pad_data[i].eos_count_ptr = &eos_seen;
    pad_data[i].cond = &cond;
    pad_data[i].mutex = &mutex;
  }

  /* Run the test. Push 20 buffers through the multiqueue in a pattern */
  eos_seen = 0;
  gst_element_set_state (pipe, GST_STATE_PLAYING);

  {
    const guint8 pad_pattern[] = { 0, 1 };
    const guint n = sizeof (pad_pattern) / sizeof (guint8);
    push_n_buffers (pad_data, NBUFFERS, pad_pattern, n);
  }

  /* Make the linked pad go EOS */
  gst_pad_push_event (pad_data[0].input_pad, gst_event_new_eos ());

  g_mutex_lock (&mutex);
  /* Wait until EOS has been seen on the linked pad */
  while (eos_seen == 0)
    g_cond_wait (&cond, &mutex);
  g_mutex_unlock (&mutex);

  /* Now push some more buffers to the not-linked pad */
  {
    const guint8 pad_pattern[] = { 1, 1 };
    const guint n = sizeof (pad_pattern) / sizeof (guint8);
    push_n_buffers (pad_data, NBUFFERS, pad_pattern, n);
  }
  /* And EOS on the not-linked pad */
  gst_pad_push_event (pad_data[1].input_pad, gst_event_new_eos ());

  g_mutex_lock (&mutex);
  while (eos_seen < NPADS)
    g_cond_wait (&cond, &mutex);
  g_mutex_unlock (&mutex);

  /* Clean up */
  for (i = 0; i < NPADS; i++) {
    GstPad *mq_input = gst_pad_get_peer (pad_data[i].input_pad);

    gst_pad_unlink (pad_data[i].input_pad, mq_input);
    gst_element_release_request_pad (mq, mq_input);
    gst_object_unref (mq_input);
    gst_object_unref (pad_data[i].input_pad);
    gst_object_unref (pad_data[i].out_pad);
  }

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  g_cond_clear (&cond);
  g_mutex_clear (&mutex);
}

GST_END_TEST;

GST_START_TEST (test_sparse_stream)
{
  /* This test creates a multiqueue with 2 streams. One receives
   * a constant flow of buffers, the other only gets one buffer, and then
   * new-segment events, and returns not-linked. The multiqueue should not fill.
   */
  GstElement *pipe;
  GstElement *mq;
  GstPad *inputpads[2];
  GstPad *sinkpads[2];
  GstEvent *event;
  struct PadData pad_data[2];
  guint32 eos_seen, max_linked_id;
  GMutex mutex;
  GCond cond;
  gint i;
  const gint NBUFFERS = 100;
  GstSegment segment;

  g_mutex_init (&mutex);
  g_cond_init (&cond);

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  /* 1 second limit */
  g_object_set (mq,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) GST_SECOND,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0, "extra-size-time", (guint64) 0, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  /* Construct 2 dummy output pads. */
  for (i = 0; i < 2; i++) {
    GstPad *mq_srcpad, *mq_sinkpad;
    gchar *name;

    name = g_strdup_printf ("dummysrc%d", i);
    inputpads[i] = gst_pad_new (name, GST_PAD_SRC);
    g_free (name);
    gst_pad_set_query_function (inputpads[i], mq_dummypad_query);

    mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
    fail_unless (mq_sinkpad != NULL);
    fail_unless (gst_pad_link (inputpads[i], mq_sinkpad) == GST_PAD_LINK_OK);

    gst_pad_set_active (inputpads[i], TRUE);

    gst_pad_push_event (inputpads[i], gst_event_new_stream_start ("test"));
    gst_pad_push_event (inputpads[i], gst_event_new_segment (&segment));

    mq_srcpad = mq_sinkpad_to_srcpad (mq, mq_sinkpad);

    name = g_strdup_printf ("dummysink%d", i);
    sinkpads[i] = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);
    gst_pad_set_chain_function (sinkpads[i], mq_dummypad_chain);
    gst_pad_set_event_function (sinkpads[i], mq_dummypad_event);
    gst_pad_set_query_function (sinkpads[i], mq_dummypad_query);

    pad_data[i].pad_num = i;
    pad_data[i].max_linked_id_ptr = &max_linked_id;
    if (i == 0)
      pad_data[i].eos_count_ptr = &eos_seen;
    else
      pad_data[i].eos_count_ptr = NULL;
    pad_data[i].is_linked = (i == 0) ? TRUE : FALSE;
    pad_data[i].n_linked = 1;
    pad_data[i].cond = &cond;
    pad_data[i].mutex = &mutex;
    pad_data[i].first_buf = TRUE;
    gst_pad_set_element_private (sinkpads[i], pad_data + i);

    fail_unless (gst_pad_link (mq_srcpad, sinkpads[i]) == GST_PAD_LINK_OK);
    gst_pad_set_active (sinkpads[i], TRUE);

    gst_object_unref (mq_sinkpad);
    gst_object_unref (mq_srcpad);
  }

  /* Run the test. Push 100 buffers through the multiqueue */
  max_linked_id = 0;
  eos_seen = 0;

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  for (i = 0; i < NBUFFERS; i++) {
    GstBuffer *buf;
    GstFlowReturn ret;
    GstClockTime ts;
    GstMapInfo info;

    ts = gst_util_uint64_scale_int (GST_SECOND, i, 10);

    buf = gst_buffer_new_and_alloc (4);
    g_mutex_lock (&_check_lock);
    fail_if (buf == NULL);
    g_mutex_unlock (&_check_lock);

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
    GST_WRITE_UINT32_BE (info.data, i + 1);
    gst_buffer_unmap (buf, &info);

    GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (GST_SECOND, i, 10);

    /* If i == 0, also push the buffer to the 2nd pad */
    if (i == 0)
      ret = gst_pad_push (inputpads[1], gst_buffer_ref (buf));

    ret = gst_pad_push (inputpads[0], buf);
    g_mutex_lock (&_check_lock);
    fail_unless (ret == GST_FLOW_OK,
        "Push on pad %d returned %d when FLOW_OK was expected", 0, ret);
    g_mutex_unlock (&_check_lock);

    /* Push a new segment update on the 2nd pad */
    gst_segment_init (&segment, GST_FORMAT_TIME);
    segment.start = ts;
    segment.time = ts;
    event = gst_event_new_segment (&segment);
    gst_pad_push_event (inputpads[1], event);
  }

  event = gst_event_new_eos ();
  gst_pad_push_event (inputpads[0], gst_event_ref (event));
  gst_pad_push_event (inputpads[1], event);

  /* Wait while the buffers are processed */
  g_mutex_lock (&mutex);
  /* We wait until EOS has been pushed on pad 1 */
  while (eos_seen < 1) {
    g_cond_wait (&cond, &mutex);
  }
  g_mutex_unlock (&mutex);

  /* Clean up */
  for (i = 0; i < 2; i++) {
    GstPad *mq_input = gst_pad_get_peer (inputpads[i]);

    gst_pad_unlink (inputpads[i], mq_input);
    gst_element_release_request_pad (mq, mq_input);
    gst_object_unref (mq_input);
    gst_object_unref (inputpads[i]);

    gst_object_unref (sinkpads[i]);
  }

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  g_cond_clear (&cond);
  g_mutex_clear (&mutex);
}

GST_END_TEST;

static gpointer
pad_push_datablock_thread (gpointer data)
{
  GstPad *pad = data;
  GstBuffer *buf;

  buf = gst_buffer_new_allocate (NULL, 80 * 1000, NULL);
  gst_pad_push (pad, buf);

  return NULL;
}

static GstPadProbeReturn
block_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  return GST_PAD_PROBE_OK;
}

static void
check_for_buffering_msg (GstElement * pipeline, gint expected_perc)
{
  gint buf_perc;
  GstMessage *msg;

  GST_LOG ("waiting for %d%% buffering message", expected_perc);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline),
      GST_MESSAGE_BUFFERING | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected BUFFERING message, got ERROR message");

  gst_message_parse_buffering (msg, &buf_perc);
  fail_unless (buf_perc == expected_perc,
      "Got incorrect percentage: %d%% expected: %d%%", buf_perc, expected_perc);

  gst_message_unref (msg);
}

GST_START_TEST (test_initial_fill_above_high_threshold)
{
  /* This test checks what happens if the first buffer that enters
   * the queue immediately fills it above the high-threshold. */
  GstElement *pipe;
  GstElement *mq, *fakesink;
  GstPad *inputpad;
  GstPad *mq_sinkpad;
  GstPad *sinkpad;
  GstSegment segment;
  GThread *thread;


  /* Setup test pipeline with one multiqueue and one fakesink */

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  /* Block fakesink sinkpad flow to ensure the queue isn't emptied
   * by the prerolling sink */
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK, block_probe, NULL,
      NULL);
  gst_object_unref (sinkpad);

  /* Set size limit to 1000000 byte, low threshold to 1%, high
   * threshold to 5%, to make sure that even just one data push
   * will exceed both thresholds.*/
  g_object_set (mq,
      "use-buffering", (gboolean) TRUE,
      "max-size-bytes", (guint) 1000 * 1000,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0,
      "extra-size-time", (guint64) 0,
      "low-percent", (gint) 1, "high-percent", (gint) 5, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, mq_dummypad_query);

  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (mq_sinkpad);

  fail_unless (gst_element_link (mq, fakesink));

  /* Start pipeline in paused state to ensure the sink remains
   * in preroll mode and blocks */
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* Feed data. queue will be filled to 8% (because it pushes 80000 bytes),
   * which is above both the low- and the high-threshold. This should
   * produce a 100% buffering message. */
  thread = g_thread_new ("push1", pad_push_datablock_thread, inputpad);
  g_thread_join (thread);
  check_for_buffering_msg (pipe, 100);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_watermark_and_fill_level)
{
  /* This test checks the behavior of the fill level and
   * the low/high watermarks. It also checks if the
   * low/high-percent and low/high-watermark properties
   * are coupled together properly. */
  GstElement *pipe;
  GstElement *mq, *fakesink;
  GstPad *inputpad;
  GstPad *mq_sinkpad;
  GstPad *sinkpad;
  GstSegment segment;
  GThread *thread;
  gint low_perc, high_perc;


  /* Setup test pipeline with one multiqueue and one fakesink */

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  /* Block fakesink sinkpad flow to ensure the queue isn't emptied
   * by the prerolling sink */
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK, block_probe, NULL,
      NULL);
  gst_object_unref (sinkpad);

  g_object_set (mq,
      "use-buffering", (gboolean) TRUE,
      "max-size-bytes", (guint) 1000 * 1000,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0,
      "extra-size-time", (guint64) 0,
      "low-watermark", (gdouble) 0.01, "high-watermark", (gdouble) 0.10, NULL);

  g_object_get (mq, "low-percent", &low_perc, "high-percent", &high_perc, NULL);

  /* Check that low/high-watermark and low/high-percent are
   * coupled properly. (low/high-percent are deprecated and
   * exist for backwards compatibility.) */
  fail_unless_equals_int (low_perc, 1);
  fail_unless_equals_int (high_perc, 10);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, mq_dummypad_query);

  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (mq_sinkpad);

  fail_unless (gst_element_link (mq, fakesink));

  /* Start pipeline in paused state to ensure the sink remains
   * in preroll mode and blocks */
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* Feed data. queue will be filled to 8% (because it pushes 80000 bytes),
   * which is below the high-threshold, provoking a buffering message. */
  thread = g_thread_new ("push1", pad_push_datablock_thread, inputpad);
  g_thread_join (thread);

  /* Check for the buffering message; it should indicate 80% fill level
   * (Note that the percentage from the message is normalized) */
  check_for_buffering_msg (pipe, 80);

  /* Increase the buffer size and lower the watermarks to test
   * if <1% watermarks are supported. */
  g_object_set (mq,
      "max-size-bytes", (guint) 20 * 1000 * 1000,
      "low-watermark", (gdouble) 0.0001, "high-watermark", (gdouble) 0.005,
      NULL);
  /* First buffering message is posted after the max-size-bytes limit
   * is set to 20000000 bytes & the low-watermark is set. Since the
   * multiqueue contains 80000 bytes, and the high watermark still is
   * 0.1 at this point, and the buffer level 80000 / 20000000 = 0.004 is
   * normalized by 0.1: 0.004 / 0.1 => buffering percentage 4%. */
  check_for_buffering_msg (pipe, 4);
  /* Second buffering message is posted after the high-watermark limit
   * is set to 0.005. This time, the buffer level is normalized this way:
   * 0.004 / 0.005 => buffering percentage 80%. */
  check_for_buffering_msg (pipe, 80);


  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_high_threshold_change)
{
  /* This test checks what happens if the high threshold is changed to a
   * value below the current buffer fill level. Expected behavior is for
   * multiqueue to emit a 100% buffering message in that case. */
  GstElement *pipe;
  GstElement *mq, *fakesink;
  GstPad *inputpad;
  GstPad *mq_sinkpad;
  GstPad *sinkpad;
  GstSegment segment;
  GThread *thread;


  /* Setup test pipeline with one multiqueue and one fakesink */

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  /* Block fakesink sinkpad flow to ensure the queue isn't emptied
   * by the prerolling sink */
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK, block_probe, NULL,
      NULL);
  gst_object_unref (sinkpad);

  g_object_set (mq,
      "use-buffering", (gboolean) TRUE,
      "max-size-bytes", (guint) 1000 * 1000,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0,
      "extra-size-time", (guint64) 0,
      "low-percent", (gint) 1, "high-percent", (gint) 99, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, mq_dummypad_query);

  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (mq_sinkpad);

  fail_unless (gst_element_link (mq, fakesink));

  /* Start pipeline in paused state to ensure the sink remains
   * in preroll mode and blocks */
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* Feed data. queue will be filled to 8% (because it pushes 80000 bytes),
   * which is below the high-threshold, provoking a buffering message. */
  thread = g_thread_new ("push1", pad_push_datablock_thread, inputpad);
  g_thread_join (thread);

  /* Check for the buffering message; it should indicate 8% fill level
   * (Note that the percentage from the message is normalized, but since
   * the high threshold is at 99%, it should still apply) */
  check_for_buffering_msg (pipe, 8);

  /* Set high threshold to half of what it was before. This means that the
   * relative fill level doubles. As a result, this should trigger a buffering
   * message with a percentage of 16%. */
  g_object_set (mq, "high-percent", (gint) 50, NULL);
  check_for_buffering_msg (pipe, 16);

  /* Set high threshold to a value that lies below the current fill level.
   * This should trigger a 100% buffering message immediately, even without
   * pushing in extra data. */
  g_object_set (mq, "high-percent", (gint) 5, NULL);
  check_for_buffering_msg (pipe, 100);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

GST_START_TEST (test_low_threshold_change)
{
  /* This tests what happens if the queue isn't currently buffering and the
   * low-threshold is raised above the current fill level. */
  GstElement *pipe;
  GstElement *mq, *fakesink;
  GstPad *inputpad;
  GstPad *mq_sinkpad;
  GstPad *sinkpad;
  GstSegment segment;
  GThread *thread;


  /* Setup test pipeline with one multiqueue and one fakesink */

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  /* Block fakesink sinkpad flow to ensure the queue isn't emptied
   * by the prerolling sink */
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BLOCK, block_probe, NULL,
      NULL);
  gst_object_unref (sinkpad);

  /* Enable buffering and set the low/high thresholds to 1%/5%. This ensures
   * that after pushing one data block, the high threshold is reached, and
   * buffering ceases. */
  g_object_set (mq,
      "use-buffering", (gboolean) TRUE,
      "max-size-bytes", (guint) 1000 * 1000,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0,
      "extra-size-time", (guint64) 0,
      "low-percent", (gint) 1, "high-percent", (gint) 5, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, mq_dummypad_query);

  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (mq_sinkpad);

  fail_unless (gst_element_link (mq, fakesink));

  /* Start pipeline in paused state to ensure the sink remains
   * in preroll mode and blocks */
  gst_element_set_state (pipe, GST_STATE_PAUSED);

  /* Feed data. queue will be filled to 8% (because it pushes 80000 bytes),
   * which is above the high-threshold, ensuring that the queue disables
   * its buffering mode internally. */
  thread = g_thread_new ("push1", pad_push_datablock_thread, inputpad);
  g_thread_join (thread);

  /* Check for the buffering message; it should indicate 100% relative fill
   * level (Note that the percentage from the message is normalized) */
  check_for_buffering_msg (pipe, 100);

  /* Set low threshold to a 10%, which is above the current fill level of 8%.
   * As a result, the queue must re-enable its buffering mode, and post the
   * current relative fill level of 40% (since high-percent is also set to 20%
   * and 8%/20% = 40%). */
  g_object_set (mq, "high-percent", (gint) 20, "low-percent", (gint) 10, NULL);
  check_for_buffering_msg (pipe, 40);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

static gpointer
pad_push_thread (gpointer data)
{
  GstPad *pad = data;
  GstBuffer *buf;

  buf = gst_buffer_new ();
  gst_pad_push (pad, buf);

  return NULL;
}

GST_START_TEST (test_limit_changes)
{
  /* This test creates a multiqueue with 1 stream. The limit of the queue
   * is two buffers, we check if we block once this is reached. Then we
   * change the limit to three buffers and check if this is waking up
   * the queue and we get the third buffer.
   */
  GstElement *pipe;
  GstElement *mq, *fakesink;
  GstPad *inputpad;
  GstPad *mq_sinkpad;
  GstSegment segment;
  GThread *thread;

  pipe = gst_pipeline_new ("testbin");
  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  gst_bin_add (GST_BIN (pipe), fakesink);

  g_object_set (mq,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 2,
      "max-size-time", (guint64) 0,
      "extra-size-bytes", (guint) 0,
      "extra-size-buffers", (guint) 0, "extra-size-time", (guint64) 0, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  gst_pad_set_query_function (inputpad, mq_dummypad_query);

  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_object_unref (mq_sinkpad);

  fail_unless (gst_element_link (mq, fakesink));

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  thread = g_thread_new ("push1", pad_push_thread, inputpad);
  g_thread_join (thread);
  thread = g_thread_new ("push2", pad_push_thread, inputpad);
  g_thread_join (thread);
  thread = g_thread_new ("push3", pad_push_thread, inputpad);
  g_thread_join (thread);
  thread = g_thread_new ("push4", pad_push_thread, inputpad);

  /* Wait until we are actually blocking... we unfortunately can't
   * know that without sleeping */
  g_usleep (G_USEC_PER_SEC);
  g_object_set (mq, "max-size-buffers", (guint) 3, NULL);
  g_thread_join (thread);

  g_object_set (mq, "max-size-buffers", (guint) 4, NULL);
  thread = g_thread_new ("push5", pad_push_thread, inputpad);
  g_thread_join (thread);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (pipe);
}

GST_END_TEST;

static GMutex block_mutex;
static GCond block_cond;
static gint unblock_count;
static gboolean expect_overrun;

static GstFlowReturn
pad_chain_block (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  g_mutex_lock (&block_mutex);
  while (unblock_count == 0) {
    g_cond_wait (&block_cond, &block_mutex);
  }
  if (unblock_count > 0) {
    unblock_count--;
  }
  g_mutex_unlock (&block_mutex);

  gst_buffer_unref (buffer);
  return GST_FLOW_OK;
}

static gboolean
pad_event_always_ok (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gst_event_unref (event);
  return TRUE;
}

static void
mq_overrun (GstElement * mq, gpointer udata)
{
  fail_unless (expect_overrun);

  /* unblock always so we don't get stuck */
  g_mutex_lock (&block_mutex);
  unblock_count = 2;            /* let the PTS=0 and PTS=none go */
  g_cond_signal (&block_cond);
  g_mutex_unlock (&block_mutex);
}

GST_START_TEST (test_buffering_with_none_pts)
{
  /*
   * This test creates a multiqueue where source pushing blocks so we can check
   * how its buffering level is reacting to GST_CLOCK_TIME_NONE buffers
   * mixed with properly timestamped buffers.
   *
   * Sequence of pushing:
   * pts=0
   * pts=none
   * pts=1 (it gets full now)
   * pts=none (overrun expected)
   */
  GstElement *mq;
  GstPad *inputpad;
  GstPad *outputpad;
  GstPad *mq_sinkpad;
  GstPad *mq_srcpad;
  GstSegment segment;
  GstBuffer *buffer;

  g_mutex_init (&block_mutex);
  g_cond_init (&block_cond);
  unblock_count = 0;
  expect_overrun = FALSE;

  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);

  g_object_set (mq,
      "max-size-bytes", (guint) 0,
      "max-size-buffers", (guint) 0,
      "max-size-time", (guint64) GST_SECOND, NULL);
  g_signal_connect (mq, "overrun", (GCallback) mq_overrun, NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);
  outputpad = gst_pad_new ("dummysink", GST_PAD_SINK);
  gst_pad_set_chain_function (outputpad, pad_chain_block);
  gst_pad_set_event_function (outputpad, pad_event_always_ok);
  mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
  mq_srcpad = gst_element_get_static_pad (mq, "src_0");
  fail_unless (mq_sinkpad != NULL);
  fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (mq_srcpad, outputpad) == GST_PAD_LINK_OK);

  gst_pad_set_active (inputpad, TRUE);
  gst_pad_set_active (outputpad, TRUE);
  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  gst_element_set_state (mq, GST_STATE_PAUSED);

  /* push a buffer with PTS = 0 */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = 0;
  fail_unless (gst_pad_push (inputpad, buffer) == GST_FLOW_OK);

  /* push a buffer with PTS = NONE */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push (inputpad, buffer) == GST_FLOW_OK);

  /* push a buffer with PTS = 1s, so we have 1s of data in multiqueue, we are
   * full */
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = GST_SECOND;
  fail_unless (gst_pad_push (inputpad, buffer) == GST_FLOW_OK);

  /* push a buffer with PTS = NONE, the queue is full so it should overrun */
  expect_overrun = TRUE;
  buffer = gst_buffer_new ();
  GST_BUFFER_PTS (buffer) = GST_CLOCK_TIME_NONE;
  fail_unless (gst_pad_push (inputpad, buffer) == GST_FLOW_OK);

  g_mutex_lock (&block_mutex);
  unblock_count = -1;
  g_cond_signal (&block_cond);
  g_mutex_unlock (&block_mutex);

  gst_element_set_state (mq, GST_STATE_NULL);
  gst_object_unref (inputpad);
  gst_object_unref (outputpad);
  gst_object_unref (mq_sinkpad);
  gst_object_unref (mq_srcpad);
  gst_object_unref (mq);
  g_mutex_clear (&block_mutex);
  g_cond_clear (&block_cond);
}

GST_END_TEST;

static gboolean
event_func_signal (GstPad * sinkpad, GstObject * parent, GstEvent * event)
{
  struct PadData *pad_data;

  GST_LOG_OBJECT (sinkpad, "%s event", GST_EVENT_TYPE_NAME (event));

  pad_data = gst_pad_get_element_private (sinkpad);

  g_mutex_lock (pad_data->mutex);
  ++pad_data->event_count;
  g_cond_broadcast (pad_data->cond);
  g_mutex_unlock (pad_data->mutex);

  gst_event_unref (event);
  return TRUE;
}

GST_START_TEST (test_initial_events_nodelay)
{
  struct PadData pad_data = { 0, };
  GstElement *pipe;
  GstElement *mq;
  GstPad *inputpad;
  GstPad *sinkpad;
  GstSegment segment;
  GstCaps *caps;
  GMutex mutex;
  GCond cond;

  g_mutex_init (&mutex);
  g_cond_init (&cond);

  pipe = gst_pipeline_new ("testbin");

  mq = gst_element_factory_make ("multiqueue", NULL);
  fail_unless (mq != NULL);
  gst_bin_add (GST_BIN (pipe), mq);

  {
    GstPad *mq_srcpad, *mq_sinkpad;

    inputpad = gst_pad_new ("dummysrc", GST_PAD_SRC);

    mq_sinkpad = gst_element_get_request_pad (mq, "sink_%u");
    fail_unless (mq_sinkpad != NULL);
    fail_unless (gst_pad_link (inputpad, mq_sinkpad) == GST_PAD_LINK_OK);

    gst_pad_set_active (inputpad, TRUE);

    mq_srcpad = mq_sinkpad_to_srcpad (mq, mq_sinkpad);

    sinkpad = gst_pad_new ("dummysink", GST_PAD_SINK);
    gst_pad_set_event_function (sinkpad, event_func_signal);

    pad_data.event_count = 0;
    pad_data.cond = &cond;
    pad_data.mutex = &mutex;
    gst_pad_set_element_private (sinkpad, &pad_data);

    fail_unless (gst_pad_link (mq_srcpad, sinkpad) == GST_PAD_LINK_OK);
    gst_pad_set_active (sinkpad, TRUE);

    gst_object_unref (mq_sinkpad);
    gst_object_unref (mq_srcpad);
  }

  /* Run the test: push events through multiqueue */
  gst_element_set_state (pipe, GST_STATE_PLAYING);

  gst_pad_push_event (inputpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (inputpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (inputpad, gst_event_new_segment (&segment));

  g_mutex_lock (&mutex);
  while (pad_data.event_count < 3) {
    GST_LOG ("%d events so far, waiting for more", pad_data.event_count);
    g_cond_wait (&cond, &mutex);
  }
  g_mutex_unlock (&mutex);

  /* Clean up */
  {
    GstPad *mq_input = gst_pad_get_peer (inputpad);

    gst_pad_unlink (inputpad, mq_input);
    gst_element_release_request_pad (mq, mq_input);
    gst_object_unref (mq_input);
    gst_object_unref (inputpad);

    gst_object_unref (sinkpad);
  }

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  g_cond_clear (&cond);
  g_mutex_clear (&mutex);
}

GST_END_TEST;

static void
check_for_stream_status_msg (GstElement * pipeline, GstElement * multiqueue,
    GstStreamStatusType expected_type)
{
  GEnumClass *klass;
  const gchar *expected_nick, *nick;
  GstMessage *msg;
  GstStreamStatusType type;
  GstElement *owner;

  klass = g_type_class_ref (GST_TYPE_STREAM_STATUS_TYPE);
  expected_nick = g_enum_get_value (klass, expected_type)->value_nick;

  GST_LOG ("waiting for stream-status %s message", expected_nick);

  msg = gst_bus_poll (GST_ELEMENT_BUS (pipeline),
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
      "Expected stream-status message, got error message");

  gst_message_parse_stream_status (msg, &type, &owner);
  nick = g_enum_get_value (klass, type)->value_nick;
  fail_unless (owner == multiqueue,
      "Got incorrect owner: %" GST_PTR_FORMAT " expected: %" GST_PTR_FORMAT,
      owner, multiqueue);
  fail_unless (type == expected_type,
      "Got incorrect type: %s expected: %s", nick, expected_nick);

  gst_message_unref (msg);
  g_type_class_unref (klass);
}

GST_START_TEST (test_stream_status_messages)
{
  GstElement *pipe, *mq;
  GstPad *pad;

  pipe = gst_pipeline_new ("pipeline");
  mq = gst_element_factory_make ("multiqueue", NULL);

  gst_bin_add (GST_BIN (pipe), mq);

  pad = gst_element_get_request_pad (mq, "sink_%u");
  gst_object_unref (pad);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  check_for_stream_status_msg (pipe, mq, GST_STREAM_STATUS_TYPE_CREATE);
  check_for_stream_status_msg (pipe, mq, GST_STREAM_STATUS_TYPE_ENTER);

  pad = gst_element_get_request_pad (mq, "sink_%u");
  gst_object_unref (pad);

  check_for_stream_status_msg (pipe, mq, GST_STREAM_STATUS_TYPE_CREATE);
  check_for_stream_status_msg (pipe, mq, GST_STREAM_STATUS_TYPE_ENTER);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_END_TEST;

static Suite *
multiqueue_suite (void)
{
  Suite *s = suite_create ("multiqueue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_create_destroy);
  tcase_add_test (tc_chain, test_simple_pipeline);
  tcase_add_test (tc_chain, test_simple_shutdown_while_running);

  tcase_add_test (tc_chain, test_request_pads);
  tcase_add_test (tc_chain, test_request_pads_named);

  /* Disabled, The test (and not multiqueue itself) is racy.
   * See https://bugzilla.gnome.org/show_bug.cgi?id=708661 */
  tcase_skip_broken_test (tc_chain, test_output_order);

  tcase_add_test (tc_chain, test_not_linked_eos);

  tcase_add_test (tc_chain, test_sparse_stream);
  tcase_add_test (tc_chain, test_initial_fill_above_high_threshold);
  tcase_add_test (tc_chain, test_watermark_and_fill_level);
  tcase_add_test (tc_chain, test_high_threshold_change);
  tcase_add_test (tc_chain, test_low_threshold_change);
  tcase_add_test (tc_chain, test_limit_changes);

  tcase_add_test (tc_chain, test_buffering_with_none_pts);
  tcase_add_test (tc_chain, test_initial_events_nodelay);

  tcase_add_test (tc_chain, test_stream_status_messages);

  return s;
}

GST_CHECK_MAIN (multiqueue)
