/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * cleanup.c: Unit test for cleanup of pipelines
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


static GstElement *
setup_pipeline (const gchar * pipe_descr)
{
  GstElement *pipeline;

  pipeline = gst_parse_launch (pipe_descr, NULL);
  g_return_val_if_fail (GST_IS_PIPELINE (pipeline), NULL);
  return pipeline;
}

/* events is a mask of expected events. tevent is the expected terminal event.
   the poll call will time out after half a second.
 */
static void
run_pipeline (GstElement * pipeline, const gchar * descr,
    GstMessageType events, GstMessageType tevent)
{
  GstBus *bus;
  GstMessageType revent;

  bus = gst_element_get_bus (pipeline);
  g_assert (bus);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (1) {
    GstMessage *message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);

    if (message) {
      revent = GST_MESSAGE_TYPE (message);
      gst_message_unref (message);
    } else {
      revent = GST_MESSAGE_UNKNOWN;
    }

    if (revent == tevent) {
      break;
    } else if (revent == GST_MESSAGE_UNKNOWN) {
      g_critical ("Unexpected timeout in gst_bus_poll, looking for %d: %s",
          tevent, descr);
      break;
    } else if (revent & events) {
      continue;
    }
    g_critical
        ("Unexpected message received of type %d, '%s', looking for %d: %s",
        revent, gst_message_type_get_name (revent), tevent, descr);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_START_TEST (test_pipeline_unref)
{
  const gchar *s;
  GstElement *pipeline, *src, *sink;

  s = "fakesrc name=src num-buffers=20 ! fakesink name=sink";
  pipeline = setup_pipeline (s);
  /* get_by_name takes a ref */
  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_if (src == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_if (sink == NULL);

  run_pipeline (pipeline, s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE, GST_MESSAGE_EOS);
  while (GST_OBJECT_REFCOUNT_VALUE (src) > 1)
    THREAD_SWITCH ();
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

static Suite *
cleanup_suite (void)
{
  Suite *s = suite_create ("Pipeline cleanup");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pipeline_unref);
  return s;
}

GST_CHECK_MAIN (cleanup);
