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
  gboolean done = FALSE;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  g_object_set (G_OBJECT (pipeline), "play-timeout", 0LL, NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  bus = gst_pipeline_get_bus (pipeline);

  fail_unless_equals_int (gst_element_set_state_async (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_ASYNC);

  while (!done) {
    GstMessage *message;
    GstState old, new, pending;

    message = gst_bus_poll (bus, GST_MESSAGE_STATE_CHANGED, -1);
    if (message) {
      gst_message_parse_state_changed (message, &old, &new, &pending);
      GST_DEBUG_OBJECT (message->src, "state change from %d to %d", old, new);
      if (message->src == GST_OBJECT (pipeline) && new == GST_STATE_PLAYING)
        done = TRUE;
      gst_message_unref (message);
    }
  }

  g_object_set (G_OBJECT (pipeline), "play-timeout", 3 * GST_SECOND, NULL);
  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_get_bus)
{
  GstPipeline *pipeline;
  GstBus *bus;

  pipeline = GST_PIPELINE (gst_pipeline_new (NULL));
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  bus = gst_pipeline_get_bus (pipeline);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after get_bus", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  gst_object_unref (pipeline);

  ASSERT_OBJECT_REFCOUNT (bus, "bus after unref pipeline", 1);
  gst_object_unref (bus);
}

GST_END_TEST;

GMainLoop *loop = NULL;

gboolean
message_received (GstBus * bus, GstMessage * message, gpointer data)
{
  GstElement *pipeline = GST_ELEMENT (data);
  GstMessageType type = message->type;

  GST_DEBUG ("message received");
  switch (type) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old, new, pending;

      GST_DEBUG ("state change message received");
      gst_message_parse_state_changed (message, &old, &new, &pending);
      GST_DEBUG ("new state %d", new);
      if (message->src == GST_OBJECT (pipeline) && new == GST_STATE_PLAYING) {
        GST_DEBUG ("quitting main loop");
        g_main_loop_quit (loop);
      }
    }
      break;
    case GST_MESSAGE_ERROR:
    {
      g_print ("error\n");
    }
      break;
    default:
      break;
  }

  return TRUE;
}

GST_START_TEST (test_bus)
{
  GstElement *pipeline;
  GstElement *src, *sink;
  GstBus *bus;
  guint id;
  GstState current;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (pipeline != NULL, "Could not create pipeline");
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  g_object_set (pipeline, "play-timeout", 0LL, NULL);

  src = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (src != NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  fail_unless (gst_element_link (src, sink));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after get_bus", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus", 2);

  id = gst_bus_add_watch (bus, message_received, pipeline);
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline after add_watch", 1);
  ASSERT_OBJECT_REFCOUNT (bus, "bus after add_watch", 3);

  gst_element_set_state_async (pipeline, GST_STATE_PLAYING);
  loop = g_main_loop_new (NULL, FALSE);
  GST_DEBUG ("going into main loop");
  g_main_loop_run (loop);
  GST_DEBUG ("left main loop");

  /* PLAYING now */

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "pipeline after gone to playing", 1,
      3);

  /* cleanup */
  GST_DEBUG ("cleanup");

  /* current semantics require us to go step by step; this will change */
  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  gst_element_set_state (pipeline, GST_STATE_READY);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (gst_element_get_state (pipeline, &current, NULL, NULL) ==
      GST_STATE_CHANGE_SUCCESS);
  fail_unless (current == GST_STATE_NULL, "state is not NULL but %d", current);

  /* FIXME: need to figure out an extra refcount, checks disabled */
//  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline at start of cleanup", 1);
//  ASSERT_OBJECT_REFCOUNT (bus, "bus at start of cleanup", 3);

  fail_unless (g_source_remove (id));
//  ASSERT_OBJECT_REFCOUNT (bus, "bus after removing source", 2);

  GST_DEBUG ("unreffing pipeline");
  gst_object_unref (pipeline);


//  ASSERT_OBJECT_REFCOUNT (bus, "bus after unref pipeline", 1);
  gst_object_unref (bus);
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
  tcase_add_test (tc_chain, test_get_bus);
  tcase_add_test (tc_chain, test_bus);

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
