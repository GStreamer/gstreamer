/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 *
 * simple_launch_lines.c: Unit test for simple pipelines
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
  fail_unless (GST_IS_PIPELINE (pipeline));
  return pipeline;
}

/*
 * run_pipeline:
 * @pipe: the pipeline to run
 * @desc: the description for use in messages
 * @message_types: is a mask of expected message_types
 * @tmessage: is the expected terminal message
 *
 * the poll call will time out after half a second.
 */
static void
run_pipeline (GstElement * pipeline, const gchar * descr,
    GstMessageType message_types, GstMessageType tmessage)
{
  GstBus *bus;
  GstMessageType rmessage;
  GstStateChangeReturn ret;

  fail_if (pipeline == NULL);
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);

  GST_DEBUG ("running pipeline %s", descr);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret != GST_STATE_CHANGE_SUCCESS) {
    GST_WARNING ("have failed state change %d", ret);
    g_critical ("Couldn't set pipeline to PLAYING");
    goto done;
  }

  while (1) {
    GstMessage *message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 2);

    if (message) {
      rmessage = GST_MESSAGE_TYPE (message);
      gst_message_unref (message);
    } else {
      rmessage = GST_MESSAGE_UNKNOWN;
    }

    if (rmessage == tmessage) {
      break;
    } else if (rmessage == GST_MESSAGE_UNKNOWN) {
      g_critical ("Unexpected timeout in gst_bus_poll, looking for %d: %s",
          tmessage, descr);
      break;
    } else if (rmessage & message_types) {
      continue;
    }
    g_critical
        ("Unexpected message received of type %d, '%s', looking for %d: %s",
        rmessage, gst_message_type_get_name (rmessage), tmessage, descr);
  }

done:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_START_TEST (test_2_elements)
{
  const gchar *s;

  s = "fakesrc can-activate-push=false ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=true ! fakesink can-activate-pull=false";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=false num-buffers=10 ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=true num-buffers=10 ! fakesink can-activate-pull=false";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
      GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=false ! fakesink can-activate-pull=false";
  ASSERT_CRITICAL (run_pipeline (setup_pipeline (s), s,
          GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED |
          GST_MESSAGE_STREAM_STATUS | GST_MESSAGE_ASYNC_DONE,
          GST_MESSAGE_UNKNOWN));
}

GST_END_TEST;

static void
check_state_change_return (GstElement * pipeline, GstState state,
    GstStateChangeReturn immediate, GstStateChangeReturn final)
{
  GstStateChangeReturn ret;

  ret = gst_element_set_state (pipeline, state);
  if (ret != immediate)
    g_critical ("Unexpected set_state return ->%s: %d != %d",
        gst_element_state_get_name (state), ret, immediate);
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  if (ret != final)
    g_critical ("Unexpected get_state return ->%s: %d != %d",
        gst_element_state_get_name (state), ret, final);
}


GST_START_TEST (test_state_change_returns)
{
  const gchar *s;
  GstElement *pipeline;

  s = "fakesrc can-activate-pull=false ! fakesink";
  pipeline = gst_parse_launch (s, NULL);
  fail_unless (GST_IS_PIPELINE (pipeline));
  check_state_change_return (pipeline, GST_STATE_READY,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_PAUSED, GST_STATE_CHANGE_ASYNC,
      GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_PLAYING,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  /* can't check PAUSED, it's not deterministic */
  check_state_change_return (pipeline, GST_STATE_READY,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS,
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);

  s = "fakesrc can-activate-push=false ! fakesink can-activate-pull=true";
  pipeline = gst_parse_launch (s, NULL);
  fail_unless (GST_IS_PIPELINE (pipeline));
  check_state_change_return (pipeline, GST_STATE_READY,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_PAUSED,
      GST_STATE_CHANGE_ASYNC, GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_PLAYING,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  /* can't check PAUSED, it's not deterministic */
  check_state_change_return (pipeline, GST_STATE_READY,
      GST_STATE_CHANGE_SUCCESS, GST_STATE_CHANGE_SUCCESS);
  check_state_change_return (pipeline, GST_STATE_NULL, GST_STATE_CHANGE_SUCCESS,
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (pipeline);
}

GST_END_TEST;

G_GNUC_UNUSED
GST_START_TEST (test_tee)
{
  const gchar *s;

  s = "fakesrc can-activate-push=true ! tee ! fakesink can-activate-push=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=true num-buffers=10 ! tee ! fakesink can-activate-push=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=false can-activate-pull=true ! tee ! fakesink can-activate-pull=true";
  ASSERT_CRITICAL (run_pipeline (setup_pipeline (s), s,
          GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED,
          GST_MESSAGE_UNKNOWN));

  s = "fakesrc can-activate-push=false can-activate-pull=true "
      "! tee pull-mode=single ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=false can-activate-pull=true num-buffers=10 "
      "! tee pull-mode=single ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=false can-activate-pull=true "
      "! tee name=t pull-mode=single ! fakesink can-activate-pull=true "
      "t. ! queue ! fakesink can-activate-pull=true can-activate-push=false";
  ASSERT_CRITICAL (run_pipeline (setup_pipeline (s), s,
          GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED,
          GST_MESSAGE_UNKNOWN));

  s = "fakesrc can-activate-push=false can-activate-pull=true "
      "! tee name=t pull-mode=single ! fakesink can-activate-pull=true "
      "t. ! queue ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=false can-activate-pull=true num-buffers=10 "
      "! tee name=t pull-mode=single ! fakesink can-activate-pull=true "
      "t. ! queue ! fakesink";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_EOS);
}

GST_END_TEST;

static void
got_handoff (GstElement * sink, GstBuffer * buf, GstPad * pad, gpointer unused)
{
  gst_element_post_message
      (sink, gst_message_new_application (NULL, gst_structure_new ("foo",
              NULL)));
}

static void
assert_live_count (GType type, gint live)
{
  GstAllocTrace *trace;
  const gchar *name;

  if (gst_alloc_trace_available ()) {
    name = g_type_name (type);
    fail_if (name == NULL);
    trace = gst_alloc_trace_get (name);
    if (trace) {
      g_return_if_fail (trace->live == live);
    }
  } else {
    g_print ("\nSkipping live count tests; recompile with traces to enable\n");
  }
}

GST_START_TEST (test_stop_from_app)
{
  GstElement *fakesrc, *fakesink, *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;
  GstMessageType rmessage;
  GstMessage *message;

  assert_live_count (GST_TYPE_BUFFER, 0);

  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  pipeline = gst_element_factory_make ("pipeline", NULL);

  fail_unless (fakesrc && fakesink && pipeline);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  fail_unless (gst_element_link (fakesrc, fakesink) != FALSE);

  g_object_set (fakesink, "signal-handoffs", (gboolean) TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (got_handoff), NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);
  ret = gst_element_get_state (pipeline, NULL, NULL, 5 * GST_SECOND);
  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_error ("Failed to change state to PAUSED");
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_error ("Failed to change state to PAUSED within 5 seconds");
      break;
    default:
      break;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);

  /* will time out after half a second */
  message = gst_bus_poll (bus, GST_MESSAGE_APPLICATION, GST_SECOND / 2);
  if (message) {
    rmessage = GST_MESSAGE_TYPE (message);
    gst_message_unref (message);
  } else {
    rmessage = GST_MESSAGE_UNKNOWN;
  }
  fail_unless (rmessage == GST_MESSAGE_APPLICATION,
      "polled message is not APPLICATION but %s",
      gst_message_type_get_name (rmessage));

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);

  assert_live_count (GST_TYPE_BUFFER, 0);
}

GST_END_TEST;

static Suite *
simple_launch_lines_suite (void)
{
  Suite *s = suite_create ("Pipelines");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_2_elements);
  tcase_add_test (tc_chain, test_state_change_returns);
  /* tcase_add_test (tc_chain, test_tee); FIXME */
  tcase_add_test (tc_chain, test_stop_from_app);
  return s;
}

GST_CHECK_MAIN (simple_launch_lines);
