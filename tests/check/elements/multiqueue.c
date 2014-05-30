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

#include <unistd.h>

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
  guint8 pad_num;
  guint32 *max_linked_id_ptr;
  guint32 *eos_count_ptr;
  gboolean is_linked;
  gboolean first_buf;
  gint n_linked;

  GMutex *mutex;
  GCond *cond;
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
run_output_order_test (gint n_linked)
{
  /* This test creates a multiqueue with 2 linked output, and 3 outputs that 
   * return 'not-linked' when data is pushed, then verifies that all buffers 
   * are received on not-linked pads only after earlier buffers on the 
   * 'linked' pads are made */
  GstElement *pipe;
  GstElement *mq;
  GstPad *inputpads[5];
  GstPad *sinkpads[5];
  struct PadData pad_data[5];
  guint32 max_linked_id;
  guint32 eos_seen;
  GMutex mutex;
  GCond cond;
  gint i;
  const gint NPADS = 5;
  const gint NBUFFERS = 1000;
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

  /* Construct NPADS dummy output pads. The first 'n_linked' return FLOW_OK, the rest
   * return NOT_LINKED. The not-linked ones check the expected ordering of 
   * output buffers */
  for (i = 0; i < NPADS; i++) {
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
    pad_data[i].eos_count_ptr = &eos_seen;
    pad_data[i].is_linked = (i < n_linked ? TRUE : FALSE);
    pad_data[i].n_linked = n_linked;
    pad_data[i].cond = &cond;
    pad_data[i].mutex = &mutex;
    pad_data[i].first_buf = TRUE;
    gst_pad_set_element_private (sinkpads[i], pad_data + i);

    fail_unless (gst_pad_link (mq_srcpad, sinkpads[i]) == GST_PAD_LINK_OK);
    gst_pad_set_active (sinkpads[i], TRUE);

    gst_object_unref (mq_sinkpad);
    gst_object_unref (mq_srcpad);
  }

  /* Run the test. Push 1000 buffers through the multiqueue in a pattern */

  max_linked_id = 0;
  eos_seen = 0;
  gst_element_set_state (pipe, GST_STATE_PLAYING);

  for (i = 0; i < NBUFFERS; i++) {
    const guint8 pad_pattern[] =
        { 0, 0, 0, 0, 1, 1, 2, 1, 0, 2, 3, 2, 3, 1, 4 };
    const guint n = sizeof (pad_pattern) / sizeof (guint8);
    guint8 cur_pad;
    GstBuffer *buf;
    GstFlowReturn ret;
    GstMapInfo info;

    cur_pad = pad_pattern[i % n];

    buf = gst_buffer_new_and_alloc (4);
    g_mutex_lock (&_check_lock);
    fail_if (buf == NULL);
    g_mutex_unlock (&_check_lock);

    fail_unless (gst_buffer_map (buf, &info, GST_MAP_WRITE));
    GST_WRITE_UINT32_BE (info.data, i + 1);
    gst_buffer_unmap (buf, &info);
    GST_BUFFER_TIMESTAMP (buf) = (i + 1) * GST_SECOND;

    ret = gst_pad_push (inputpads[cur_pad], buf);
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
  for (i = 0; i < NPADS; i++) {
    gst_pad_push_event (inputpads[i], gst_event_new_eos ());
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

GST_START_TEST (test_output_order)
{
  run_output_order_test (2);
  run_output_order_test (0);
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
  sleep (1);
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

  tcase_add_test (tc_chain, test_sparse_stream);
  tcase_add_test (tc_chain, test_limit_changes);

  tcase_add_test (tc_chain, test_buffering_with_none_pts);

  return s;
}

GST_CHECK_MAIN (multiqueue)
