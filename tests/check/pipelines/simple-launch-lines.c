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
setup_pipeline (gchar * pipe_descr)
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
run_pipeline (GstElement * pipeline, gchar * descr,
    GstMessageType message_types, GstMessageType tmessage)
{
  GstBus *bus;
  GstMessageType rmessage;
  GstStateChangeReturn ret;

  fail_if (pipeline == NULL);
  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  ret = gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ret != GST_STATE_CHANGE_SUCCESS) {
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
    g_critical ("Unexpected message received of type %d, looking for %d: %s",
        rmessage, tmessage, descr);
  }

done:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_START_TEST (test_2_elements)
{
  gchar *s;

  s = "fakesrc can-activate-push=false ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=true ! fakesink can-activate-pull=false";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_UNKNOWN);

  s = "fakesrc can-activate-push=false num-buffers=10 ! fakesink can-activate-pull=true";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=true num-buffers=10 ! fakesink can-activate-pull=false";
  run_pipeline (setup_pipeline (s), s,
      GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED, GST_MESSAGE_EOS);

  s = "fakesrc can-activate-push=false ! fakesink can-activate-pull=false";
  ASSERT_CRITICAL (run_pipeline (setup_pipeline (s), s,
          GST_MESSAGE_NEW_CLOCK | GST_MESSAGE_STATE_CHANGED,
          GST_MESSAGE_UNKNOWN));
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
  GstMessageType rmessage;
  GstMessage *message;

  assert_live_count (GST_TYPE_BUFFER, 0);

  fakesrc = gst_element_factory_make ("fakesrc", NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  pipeline = gst_element_factory_make ("pipeline", NULL);

  fail_unless (fakesrc && fakesink && pipeline);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, fakesink, NULL);
  gst_element_link (fakesrc, fakesink);

  g_object_set (fakesink, "signal-handoffs", (gboolean) TRUE, NULL);
  g_signal_connect (fakesink, "handoff", G_CALLBACK (got_handoff), NULL);

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

Suite *
simple_launch_lines_suite (void)
{
  Suite *s = suite_create ("Pipelines");
  TCase *tc_chain = tcase_create ("linear");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_2_elements);
  tcase_add_test (tc_chain, test_stop_from_app);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = simple_launch_lines_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
