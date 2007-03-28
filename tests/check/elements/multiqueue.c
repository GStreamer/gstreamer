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

      srcpad = gst_element_get_pad (inputs[i], "src");
      if (srcpad == NULL)
        srcpad = gst_element_get_pad (inputs[i], "src%d");
      if (srcpad == NULL)
        srcpad = gst_element_get_pad (inputs[i], "src_%d");
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
      srcpad = gst_element_get_pad (mq, padname);
      fail_unless (srcpad != NULL, "failed to get multiqueue src pad #%u", i);
      fail_unless (GST_PAD_IS_SRC (srcpad),
          "%s:%s is not a source pad?!", GST_DEBUG_PAD_NAME (srcpad));

      gst_bin_add (GST_BIN (pipe), outputs[i]);

      sinkpad = gst_element_get_pad (outputs[i], "sink");
      if (sinkpad == NULL)
        sinkpad = gst_element_get_pad (outputs[i], "sink%d");
      if (sinkpad == NULL)
        sinkpad = gst_element_get_pad (outputs[i], "sink_%d");
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

static Suite *
multiqueue_suite (void)
{
  Suite *s = suite_create ("multiqueue");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_create_destroy);
  tcase_add_test (tc_chain, test_simple_pipeline);

  if (0) {
    /* FIXME: this leaks buffers, disabled for now */
    tcase_add_test (tc_chain, test_simple_shutdown_while_running);
  }

  /* FIXME: test_request_pads() needs some more fixes, see comments there */
  tcase_add_test (tc_chain, test_request_pads);

  return s;
}

GST_CHECK_MAIN (multiqueue)
