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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <unistd.h>

#include <gst/check/gstcheck.h>

static GStaticMutex _check_lock = G_STATIC_MUTEX_INIT;

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
    sinkpad = gst_element_get_request_pad (mq, "sink%d");
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
      srcpad = gst_element_get_request_pad (mq, "src%d");
      fail_unless (srcpad == NULL);

      g_snprintf (padname, sizeof (padname), "src%d", i);
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
  GstElement *pipe, *mq;
  GstElement *inputs[1];
  GstElement *outputs[1];
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  inputs[0] = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (inputs[0] != NULL, "failed to create 'fakesrc' element");
  g_object_set (inputs[0], "num-buffers", 256, NULL);

  outputs[0] = gst_element_factory_make ("fakesink", NULL);
  fail_unless (outputs[0] != NULL, "failed to create 'fakesink' element");

  mq = setup_multiqueue (pipe, inputs, outputs, 1);

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
  GstElement *pipe, *mq;
  GstElement *inputs[1];
  GstElement *outputs[1];
  GstMessage *msg;

  pipe = gst_pipeline_new ("pipeline");

  inputs[0] = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (inputs[0] != NULL, "failed to create 'fakesrc' element");

  outputs[0] = gst_element_factory_make ("fakesink", NULL);
  fail_unless (outputs[0] != NULL, "failed to create 'fakesink' element");

  mq = setup_multiqueue (pipe, inputs, outputs, 1);

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
  gboolean change_state_before_cleanup = TRUE;
  GstElement *mq;
  GstPad *sink1, *sink2;

again:

  mq = gst_element_factory_make ("multiqueue", NULL);

  sink1 = gst_element_get_request_pad (mq, "foo%d");
  fail_unless (sink1 == NULL,
      "Expected NULL pad, as there is no request pad template for 'foo%%d'");

  sink1 = gst_element_get_request_pad (mq, "src%d");
  fail_unless (sink1 == NULL,
      "Expected NULL pad, as there is no request pad template for 'src%%d'");

  sink1 = gst_element_get_request_pad (mq, "sink%d");
  fail_unless (sink1 != NULL);
  fail_unless (GST_IS_PAD (sink1));
  fail_unless (GST_PAD_IS_SINK (sink1));
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink1));

  sink2 = gst_element_get_request_pad (mq, "sink%d");
  fail_unless (sink2 != NULL);
  fail_unless (GST_IS_PAD (sink2));
  fail_unless (GST_PAD_IS_SINK (sink2));
  GST_LOG ("Got pad %s:%s", GST_DEBUG_PAD_NAME (sink2));

  fail_unless (sink1 != sink2);

  if (change_state_before_cleanup) {
    /* FIXME: if we don't change state, it will deadlock forever when unref'ing
     * the queue (waiting for pad tasks to join) */
    GST_LOG ("Changing state to PLAYING");
    gst_element_set_state (mq, GST_STATE_PLAYING);
    g_usleep (G_USEC_PER_SEC);
    GST_LOG ("Changing state to NULL");
    gst_element_set_state (mq, GST_STATE_NULL);
  }

  GST_LOG ("Cleaning up");
  gst_object_unref (sink1);
  gst_object_unref (sink2);
  gst_object_unref (mq);

  /* FIXME: this should work without state change before cleanup as well,
   * but currently doesn't, see above, so disable this for now */
  if (change_state_before_cleanup && 0) {
    change_state_before_cleanup = FALSE;
    goto again;
  }

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
  mq_srcpad_name = g_strdup_printf ("src%s", mq_sinkpad_name + 4);
  srcpad = gst_element_get_static_pad (mq, mq_srcpad_name);
  fail_unless (srcpad != NULL);

  g_free (mq_sinkpad_name);
  g_free (mq_srcpad_name);

  return srcpad;
}

static GstCaps *
mq_dummypad_getcaps (GstPad * sinkpad)
{
  return gst_caps_new_any ();
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
mq_dummypad_chain (GstPad * sinkpad, GstBuffer * buf)
{
  guint32 cur_id;
  struct PadData *pad_data;

  pad_data = gst_pad_get_element_private (sinkpad);

  g_static_mutex_lock (&_check_lock);
  fail_if (pad_data == NULL);
  /* Read an ID from the first 4 bytes of the buffer data and check it's
   * what we expect */
  fail_unless (GST_BUFFER_SIZE (buf) >= 4);
  g_static_mutex_unlock (&_check_lock);
  cur_id = GST_READ_UINT32_BE (GST_BUFFER_DATA (buf));

  g_mutex_lock (pad_data->mutex);

  /* For not-linked pads, ensure that we're not running ahead of the 'linked'
   * pads. The first buffer is allowed to get ahead, because otherwise things can't
   * always pre-roll correctly */
  if (!pad_data->is_linked) {
    /* If there are no linked pads, we can't track a max_id for them :) */
    if (pad_data->n_linked > 0 && !pad_data->first_buf) {
      g_static_mutex_lock (&_check_lock);
      fail_unless (cur_id <= *(pad_data->max_linked_id_ptr) + 1,
          "Got buffer %u on pad %u before buffer %u was seen on a "
          "linked pad (max: %u)", cur_id, pad_data->pad_num, cur_id - 1,
          *(pad_data->max_linked_id_ptr));
      g_static_mutex_unlock (&_check_lock);
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
mq_dummypad_event (GstPad * sinkpad, GstEvent * event)
{
  struct PadData *pad_data;

  pad_data = gst_pad_get_element_private (sinkpad);
  g_static_mutex_lock (&_check_lock);
  fail_if (pad_data == NULL);
  g_static_mutex_unlock (&_check_lock);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    g_mutex_lock (pad_data->mutex);

    /* Accumulate that we've seen the EOS and signal the main thread */
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
  GMutex *mutex;
  GCond *cond;
  gint i;
  const gint NPADS = 5;
  const gint NBUFFERS = 1000;

  mutex = g_mutex_new ();
  cond = g_cond_new ();

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
    gst_pad_set_getcaps_function (inputpads[i], mq_dummypad_getcaps);

    mq_sinkpad = gst_element_get_request_pad (mq, "sink%d");
    fail_unless (mq_sinkpad != NULL);
    gst_pad_link (inputpads[i], mq_sinkpad);

    gst_pad_set_active (inputpads[i], TRUE);

    mq_srcpad = mq_sinkpad_to_srcpad (mq, mq_sinkpad);

    name = g_strdup_printf ("dummysink%d", i);
    sinkpads[i] = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);
    gst_pad_set_chain_function (sinkpads[i], mq_dummypad_chain);
    gst_pad_set_event_function (sinkpads[i], mq_dummypad_event);
    gst_pad_set_getcaps_function (sinkpads[i], mq_dummypad_getcaps);

    pad_data[i].pad_num = i;
    pad_data[i].max_linked_id_ptr = &max_linked_id;
    pad_data[i].eos_count_ptr = &eos_seen;
    pad_data[i].is_linked = (i < n_linked ? TRUE : FALSE);
    pad_data[i].n_linked = n_linked;
    pad_data[i].cond = cond;
    pad_data[i].mutex = mutex;
    pad_data[i].first_buf = TRUE;
    gst_pad_set_element_private (sinkpads[i], pad_data + i);

    gst_pad_link (mq_srcpad, sinkpads[i]);
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

    cur_pad = pad_pattern[i % n];

    buf = gst_buffer_new_and_alloc (4);
    g_static_mutex_lock (&_check_lock);
    fail_if (buf == NULL);
    g_static_mutex_unlock (&_check_lock);
    GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), i + 1);
    GST_BUFFER_TIMESTAMP (buf) = (i + 1) * GST_SECOND;

    ret = gst_pad_push (inputpads[cur_pad], buf);
    g_static_mutex_lock (&_check_lock);
    if (pad_data[cur_pad].is_linked) {
      fail_unless (ret == GST_FLOW_OK,
          "Push on pad %d returned %d when FLOW_OK was expected", cur_pad, ret);
    } else {
      /* Expect OK initially, then NOT_LINKED when the srcpad starts pushing */
      fail_unless (ret == GST_FLOW_OK || ret == GST_FLOW_NOT_LINKED,
          "Push on pad %d returned %d when FLOW_OK or NOT_LINKED  was expected",
          cur_pad, ret);
    }
    g_static_mutex_unlock (&_check_lock);
  }
  for (i = 0; i < NPADS; i++) {
    gst_pad_push_event (inputpads[i], gst_event_new_eos ());
  }

  /* Wait while the buffers are processed */
  g_mutex_lock (mutex);
  /* We wait until EOS has been pushed on all linked pads */
  while (eos_seen < n_linked) {
    g_cond_wait (cond, mutex);
  }
  g_mutex_unlock (mutex);

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

  g_cond_free (cond);
  g_mutex_free (mutex);
}

GST_START_TEST (test_output_order)
{
  run_output_order_test (2);
  run_output_order_test (0);
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

  /* FIXME: test_request_pads() needs some more fixes, see comments there */
  tcase_add_test (tc_chain, test_request_pads);

  tcase_add_test (tc_chain, test_output_order);
  return s;
}

GST_CHECK_MAIN (multiqueue)
