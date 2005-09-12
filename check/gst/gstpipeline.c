/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstpipeline.c: Unit test for GstPipeline
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

#if 0
static void
pop_messages (GstBus * bus, int count)
{
  GstMessage *message;

  int i;

  GST_DEBUG ("popping %d messages", count);
  for (i = 0; i < count; ++i) {
    fail_unless (gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1)
        == GST_MESSAGE_STATE_CHANGED, "did not get GST_MESSAGE_STATE_CHANGED");

    message = gst_bus_pop (bus);
    gst_message_unref (message);
  }
  GST_DEBUG ("popped %d messages", count);
}
#endif

/* an empty pipeline can go to PLAYING in one go */
GST_START_TEST (test_async_state_change_empty)
{
  GstPipeline *pipeline;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  g_object_set (G_OBJECT (pipeline), "play-timeout", 0LL, NULL);

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_async_state_change_fake_ready)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  g_object_set (G_OBJECT (pipeline), "play-timeout", 0LL, NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (pipeline);
}

GST_END_TEST;


GST_START_TEST (test_async_state_change_fake)
{
  GstPipeline *pipeline;
  GstElement *src, *sink;
  GstBus *bus;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  g_object_set (G_OBJECT (pipeline), "play-timeout", 0LL, NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  bus = gst_pipeline_get_bus (pipeline);

  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_ASYNC);

#if 0
  /* FIXME: Wim is implementing a set_state_async, which will
   * spawn a thread and make sure the pipeline gets to the
   * requested final state, or errors out before */
  gst_bin_watch_for_state_change (GST_BIN (pipeline));

  while ((type = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1))) {
    GstMessage *message;
    GstMessageType type;
    GstState old, new;
    GstState state, pending;
    GstStateChange ret;
    GTimeVal timeval;

    message = gst_bus_pop (bus);
    gst_message_parse_state_changed (message, &old, &new);
    GST_DEBUG_OBJECT (message->src, "state change from %d to %d", old, new);
    g_print ("message\n");
    g_print ("%s: %d -> %d\n", GST_OBJECT_NAME (message->src), old, new);
    gst_message_unref (message);

    timeval.tv_sec = 0;
    timeval.tv_usec = 0;
    ret = gst_element_get_state (GST_ELEMENT (pipeline), &state, &pending,
        &timeval);
  }
#endif

  gst_object_unref (pipeline);
}

GST_END_TEST;

Suite *
gst_pipeline_suite (void)
{
  Suite *s = suite_create ("GstPipeline");
  TCase *tc_chain = tcase_create ("pipeline tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_async_state_change_empty);
  tcase_add_test (tc_chain, test_async_state_change_fake_ready);
  tcase_add_test (tc_chain, test_async_state_change_fake);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_pipeline_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
