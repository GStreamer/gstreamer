/* Gnonlin
 * Copyright (C) <2009> Alessandro Decina <alessandro.decina@collabora.co.uk>
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
#include "common.h"

typedef struct
{
  GstElement *composition;
  GstElement *source3;
} TestClosure;

static int seek_events;

static GstPadProbeReturn
on_source1_pad_event_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  if (GST_EVENT_TYPE (info->data) == GST_EVENT_SEEK)
    ++seek_events;

  return GST_PAD_PROBE_OK;
}

GST_START_TEST (test_change_object_start_stop_in_current_stack)
{
  GstPad *srcpad;
  GstElement *pipeline;
  GstElement *comp, *source1, *def, *sink;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on, ret = FALSE;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");

  gst_element_set_state (comp, GST_STATE_READY);

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  gst_element_link (comp, sink);

  /*
     source1
     Start : 0s
     Duration : 2s
     Priority : 2
   */

  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 2, 2);
  srcpad = gst_element_get_static_pad (source1, "src");
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) on_source1_pad_event_cb, NULL, NULL);

  /*
     def (default source)
     Priority = G_MAXUINT32
   */
  def =
      videotest_nle_src ("default", 0 * GST_SECOND, 0 * GST_SECOND, 2,
      G_MAXUINT32);
  g_object_set (def, "expandable", TRUE, NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (def, "default", 1);

  /* Add source 1 */

  nle_composition_add (GST_BIN (comp), source1);
  nle_composition_add (GST_BIN (comp), def);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PAUSED");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  carry_on = TRUE;
  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_ASYNC_DONE:
        {
          carry_on = FALSE;
          GST_DEBUG ("Pipeline reached PAUSED, stopping polling");
          break;
        }
        case GST_MESSAGE_EOS:
        {
          GST_WARNING ("Saw EOS");

          fail_if (TRUE);
        }
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }


  /* pipeline is paused at this point */

  /* move source1 out of the active segment */
  g_object_set (source1, "start", (guint64) 4 * GST_SECOND, NULL);
  commit_and_wait (comp, &ret);

  /* remove source1 from the composition, which will become empty and remove the
   * ghostpad */


  /* keep an extra ref to source1 as we remove it from the bin */
  gst_object_ref (source1);
  fail_unless (nle_composition_remove (GST_BIN (comp), source1));
  g_object_set (source1, "start", (guint64) 0 * GST_SECOND, NULL);
  /* add the source again and check that the ghostpad is added again */
  nle_composition_add (GST_BIN (comp), source1);
  gst_object_unref (source1);
  commit_and_wait (comp, &ret);


  g_object_set (source1, "duration", (guint64) 1 * GST_SECOND, NULL);
  commit_and_wait (comp, &ret);

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);
  gst_element_set_state (source1, GST_STATE_NULL);
  gst_object_unref (source1);

  GST_DEBUG ("Resetted pipeline to NULL");

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_remove_invalid_object)
{
  GstBin *composition;
  GstElement *source1, *source2;

  composition = GST_BIN (gst_element_factory_make ("nlecomposition",
          "composition"));
  gst_element_set_state (GST_ELEMENT (composition), GST_STATE_READY);

  source1 = gst_element_factory_make ("nlesource", "source1");
  source2 = gst_element_factory_make ("nlesource", "source2");

  nle_composition_add (composition, source1);
  nle_composition_remove (composition, source2);
  fail_unless (nle_composition_remove (composition, source1));

  gst_element_set_state (GST_ELEMENT (composition), GST_STATE_NULL);
  gst_object_unref (composition);
  gst_object_unref (source2);
}

GST_END_TEST;

GST_START_TEST (test_dispose_on_commit)
{
  GstElement *composition;
  GstElement *nlesource;
  GstElement *audiotestsrc;
  GstElement *pipeline, *fakesink;
  gboolean ret;

  composition = gst_element_factory_make ("nlecomposition", "composition");
  pipeline = GST_ELEMENT (gst_pipeline_new (NULL));
  fakesink = gst_element_factory_make ("fakesink", NULL);

  nlesource = gst_element_factory_make ("nlesource", "nlesource1");
  audiotestsrc = gst_element_factory_make ("audiotestsrc", "audiotestsrc1");
  gst_bin_add (GST_BIN (nlesource), audiotestsrc);
  g_object_set (nlesource, "start", (guint64) 0 * GST_SECOND,
      "duration", 10 * GST_SECOND, "inpoint", (guint64) 0, "priority", 1, NULL);
  fail_unless (nle_composition_add (GST_BIN (composition), nlesource));

  gst_bin_add_many (GST_BIN (pipeline), composition, fakesink, NULL);
  fail_unless (gst_element_link (composition, fakesink) == TRUE);


  ASSERT_OBJECT_REFCOUNT (composition, "composition", 1);
  g_signal_emit_by_name (composition, "commit", TRUE, &ret);

  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_simple_adder)
{
  GstBus *bus;
  GstMessage *message;
  GstElement *pipeline;
  GstElement *nle_adder;
  GstElement *composition;
  GstElement *adder, *fakesink;
  GstClockTime start_playing_time;
  GstElement *nlesource1, *nlesource2;
  GstElement *audiotestsrc1, *audiotestsrc2;

  gboolean carry_on = TRUE, ret;
  GstClockTime total_time = 10 * GST_SECOND;

  pipeline = GST_ELEMENT (gst_pipeline_new (NULL));
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  GST_ERROR ("Pipeline refcounts: %i", ((GObject *) pipeline)->ref_count);
  composition = gst_element_factory_make ("nlecomposition", "composition");
  gst_element_set_state (composition, GST_STATE_READY);
  fakesink = gst_element_factory_make ("fakesink", NULL);

  /* nle_adder */
  nle_adder = gst_element_factory_make ("nleoperation", "nle_adder");
  adder = gst_element_factory_make ("adder", "adder");
  fail_unless (adder != NULL);
  gst_bin_add (GST_BIN (nle_adder), adder);
  g_object_set (nle_adder, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time, "inpoint", (guint64) 0 * GST_SECOND,
      "priority", 0, NULL);
  nle_composition_add (GST_BIN (composition), nle_adder);

  GST_ERROR ("Pipeline refcounts: %i", ((GObject *) pipeline)->ref_count);
  /* source 1 */
  nlesource1 = gst_element_factory_make ("nlesource", "nlesource1");
  audiotestsrc1 = gst_element_factory_make ("audiotestsrc", "audiotestsrc1");
  gst_bin_add (GST_BIN (nlesource1), audiotestsrc1);
  g_object_set (nlesource1, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time / 2, "inpoint", (guint64) 0, "priority", 1, NULL);
  fail_unless (nle_composition_add (GST_BIN (composition), nlesource1));

  /* nlesource2 */
  nlesource2 = gst_element_factory_make ("nlesource", "nlesource2");
  audiotestsrc2 = gst_element_factory_make ("audiotestsrc", "audiotestsrc2");
  gst_bin_add (GST_BIN (nlesource2), GST_ELEMENT (audiotestsrc2));
  g_object_set (nlesource2, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time, "inpoint", (guint64) 0 * GST_SECOND, "priority",
      2, NULL);

  GST_DEBUG ("Adding composition to pipeline");
  gst_bin_add_many (GST_BIN (pipeline), composition, fakesink, NULL);

  GST_ERROR ("Pipeline refcounts: %i", ((GObject *) pipeline)->ref_count);

  fail_unless (nle_composition_add (GST_BIN (composition), nlesource2));
  fail_unless (gst_element_link (composition, fakesink) == TRUE);

  GST_DEBUG ("Setting pipeline to PLAYING");

  commit_and_wait (composition, &ret);
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING)
      == GST_STATE_CHANGE_FAILURE);

  message = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);
  gst_mini_object_unref (GST_MINI_OBJECT (message));

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    fail_error_message (message);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "nle-simple-adder-test-play");

  /* Now play the 10 second composition */
  start_playing_time = gst_util_get_timestamp ();
  while (carry_on) {

    if (GST_CLOCK_DIFF (start_playing_time, gst_util_get_timestamp ()) >
        total_time + GST_SECOND) {
      GST_ERROR ("No EOS found after %" GST_TIME_FORMAT " sec",
          GST_TIME_ARGS ((total_time / GST_SECOND) + 1));
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "nle-simple-adder-test-fail");

      fail_unless ("No EOS received" == NULL);

      break;
    }

    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    GST_LOG ("poll: %" GST_PTR_FORMAT, message);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS");
          carry_on = FALSE;
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* We shouldn't see any segement messages, since we didn't do a segment seek */
          GST_WARNING ("Saw a Segment start/stop");
          fail_if (TRUE);
          carry_on = FALSE;
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("nlecomposition");
  TCase *tc_chain = tcase_create ("nlecomposition");

  ges_init ();
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_change_object_start_stop_in_current_stack);
  tcase_add_test (tc_chain, test_remove_invalid_object);

  tcase_add_test (tc_chain, test_dispose_on_commit);

  if (gst_registry_check_feature_version (gst_registry_get (), "adder", 1,
          0, 0)) {
    tcase_add_test (tc_chain, test_simple_adder);
  } else {
    GST_WARNING ("adder element not available, skipping 1 test");
  }

  return s;
}

GST_CHECK_MAIN (gnonlin)
