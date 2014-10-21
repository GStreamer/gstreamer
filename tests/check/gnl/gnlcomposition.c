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

static int composition_pad_added;
static int composition_pad_removed;
static int seek_events;
static gulong blockprobeid = 0;
static GMutex pad_added_lock;
static GCond pad_added_cond;

static GstPadProbeReturn
on_source1_pad_event_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  if (GST_EVENT_TYPE (info->data) == GST_EVENT_SEEK)
    ++seek_events;

  return GST_PAD_PROBE_OK;
}

static void
on_source1_pad_added_cb (GstElement * source, GstPad * pad, gpointer user_data)
{
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
      (GstPadProbeCallback) on_source1_pad_event_cb, NULL, NULL);
}

static void
on_composition_pad_added_cb (GstElement * composition, GstPad * pad,
    GstElement * sink)
{
  GstPad *s = gst_element_get_static_pad (sink, "sink");
  gst_pad_link (pad, s);
  ++composition_pad_added;
  g_mutex_lock (&pad_added_lock);
  g_cond_broadcast (&pad_added_cond);
  g_mutex_unlock (&pad_added_lock);
  gst_object_unref (s);
}

static void
on_composition_pad_removed_cb (GstElement * composition, GstPad * pad,
    GstElement * sink)
{
  ++composition_pad_removed;
}

GST_START_TEST (test_change_object_start_stop_in_current_stack)
{
  GstElement *pipeline;
  GstElement *comp, *source1, *def, *sink;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on, ret = FALSE;
  int seek_events_before;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("gnlcomposition", "test_composition");

  sink = gst_element_factory_make_or_warn ("fakesink", "sink");
  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* connect to pad-added */
  g_object_connect (comp, "signal::pad-added",
      on_composition_pad_added_cb, sink, NULL);
  g_object_connect (comp, "signal::pad-removed",
      on_composition_pad_removed_cb, NULL, NULL);

  /*
     source1
     Start : 0s
     Duration : 2s
     Priority : 2
   */

  source1 = videotest_gnl_src ("source1", 0, 2 * GST_SECOND, 2, 2);
  g_object_connect (source1, "signal::pad-added",
      on_source1_pad_added_cb, NULL, NULL);

  /*
     def (default source)
     Priority = G_MAXUINT32
   */
  def =
      videotest_gnl_src ("default", 0 * GST_SECOND, 0 * GST_SECOND, 2,
      G_MAXUINT32);
  g_object_set (def, "expandable", TRUE, NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (def, "default", 1);

  /* Add source 1 */

  /* keep an extra ref to source1 as we remove it from the bin */
  gst_object_ref (source1);
  gst_bin_add (GST_BIN (comp), source1);

  /* Add default */
  gst_bin_add (GST_BIN (comp), def);
  g_signal_emit_by_name (comp, "commit", TRUE, &ret);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 2);

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

  fail_unless_equals_int (composition_pad_added, 1);
  fail_unless_equals_int (composition_pad_removed, 0);

  seek_events_before = seek_events;

  /* pipeline is paused at this point */

  /* move source1 out of the active segment */
  g_object_set (source1, "start", (guint64) 4 * GST_SECOND, NULL);
  g_signal_emit_by_name (comp, "commit", TRUE, &ret);
  fail_unless (seek_events > seek_events_before);

  /* remove source1 from the composition, which will become empty and remove the
   * ghostpad */
  gst_bin_remove (GST_BIN (comp), source1);

  fail_unless_equals_int (composition_pad_added, 1);
  fail_unless_equals_int (composition_pad_removed, 1);

  g_object_set (source1, "start", (guint64) 0 * GST_SECOND, NULL);
  /* add the source again and check that the ghostpad is added again */
  gst_bin_add (GST_BIN (comp), source1);
  g_signal_emit_by_name (comp, "commit", TRUE, &ret);

  g_mutex_lock (&pad_added_lock);
  g_cond_wait (&pad_added_cond, &pad_added_lock);
  fail_unless_equals_int (composition_pad_added, 2);
  fail_unless_equals_int (composition_pad_removed, 1);
  g_mutex_unlock (&pad_added_lock);

  seek_events_before = seek_events;

  g_object_set (source1, "duration", (guint64) 1 * GST_SECOND, NULL);
  g_signal_emit_by_name (comp, "commit", TRUE, &ret);
  fail_unless (seek_events > seek_events_before);

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);
  gst_element_set_state (source1, GST_STATE_NULL);
  gst_object_unref (source1);

  GST_DEBUG ("Resetted pipeline to READY");

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

  composition = GST_BIN (gst_element_factory_make ("gnlcomposition",
          "composition"));
  source1 = gst_element_factory_make ("gnlsource", "source1");
  source2 = gst_element_factory_make ("gnlsource", "source2");

  gst_bin_add (composition, source1);
  fail_if (gst_bin_remove (composition, source2));
  fail_unless (gst_bin_remove (composition, source1));

  gst_object_unref (composition);
  gst_object_unref (source2);
}

GST_END_TEST;

static GstPadProbeReturn
pad_block (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstPad *ghost;
  GstBin *bin;

  bin = GST_BIN (user_data);

  GST_DEBUG_OBJECT (pad, "probe type:0x%x", GST_PAD_PROBE_INFO_TYPE (info));

  ghost = gst_ghost_pad_new ("src", pad);
  gst_pad_set_active (ghost, TRUE);

  gst_element_add_pad (GST_ELEMENT (bin), ghost);

  return GST_PAD_PROBE_REMOVE;
}

static void
no_more_pads_test_cb (GObject * object, TestClosure * c)
{
  gboolean ret;

  GST_WARNING ("NO MORE PADS");
  gst_bin_add (GST_BIN (c->composition), c->source3);
  g_signal_emit_by_name (c->composition, "commit", TRUE, &ret);
}

GST_START_TEST (test_no_more_pads_race)
{
  gboolean ret;
  GstElement *source1, *source2, *source3;
  GstBin *bin;
  GstElement *videotestsrc1, *videotestsrc2;
  GstElement *operation;
  GstElement *composition;
  GstElement *videomixer, *fakesink;
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *message;
  GstPad *pad;
  TestClosure closure;

  /* We create a composition with an operation and three sources. The operation
   * contains a videomixer instance and the three sources are videotestsrc's.
   *
   * One of the sources, source2, contains videotestsrc inside a bin. Initially
   * the bin doesn't have a source pad. We do this to exercise the dynamic src
   * pad code path in gnlcomposition. We block on the videotestsrc srcpad and in
   * the pad block callback we ghost the pad and add the ghost to the parent
   * bin. This makes gnlsource emit no-more-pads, which is used by
   * gnlcomposition to link the source2:src pad to videomixer.
   *
   * We start with the composition containing operation and source1. We preroll
   * and then add source2. Source2 will do what described above and emit
   * no-more-pads. We connect to that no-more-pads and from there we add source3 to
   * the composition. Adding a new source will make gnlcomposition deactivate
   * the old stack and activate a new one. The new one contains operation,
   * source1, source2 and source3. Source2 was active in the old stack as well and
   * gnlcomposition is *still waiting* for no-more-pads to be emitted on it
   * (since the no-more-pads emission is now blocked in our test's no-more-pads
   * callback, calling gst_bin_add). In short, here, we're simulating a race between
   * no-more-pads and someone modifying the composition.
   *
   * Activating the new stack, gnlcomposition calls compare_relink_single_node,
   * which finds an existing source pad for source2 this time since we have
   * already blocked and ghosted. It takes another code path that assumes that
   * source2 doesn't have dynamic pads and *BOOM*.
   */

  pipeline = GST_ELEMENT (gst_pipeline_new (NULL));
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  composition = gst_element_factory_make ("gnlcomposition", "composition");
  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);
  g_object_set (fakesink, "sync", TRUE, NULL);

  /* operation */
  operation = gst_element_factory_make ("gnloperation", "operation");
  videomixer = gst_element_factory_make ("videomixer", "videomixer");
  fail_unless (videomixer != NULL);
  gst_bin_add (GST_BIN (operation), videomixer);
  g_object_set (operation, "start", (guint64) 0 * GST_SECOND,
      "duration", (guint64) 10 * GST_SECOND,
      "inpoint", (guint64) 0 * GST_SECOND, "priority", 10, NULL);
  gst_bin_add (GST_BIN (composition), operation);

  /* source 1 */
  source1 = gst_element_factory_make ("gnlsource", "source1");
  videotestsrc1 = gst_element_factory_make ("videotestsrc", "videotestsrc1");
  gst_bin_add (GST_BIN (source1), videotestsrc1);
  g_object_set (source1, "start", (guint64) 0 * GST_SECOND, "duration",
      (guint64) 5 * GST_SECOND, "inpoint", (guint64) 0 * GST_SECOND, "priority",
      20, NULL);

  /* source2 */
  source2 = gst_element_factory_make ("gnlsource", "source2");
  bin = GST_BIN (gst_bin_new (NULL));
  videotestsrc2 = gst_element_factory_make ("videotestsrc", "videotestsrc2");
  pad = gst_element_get_static_pad (videotestsrc2, "src");
  blockprobeid =
      gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) pad_block, bin, NULL);
  gst_bin_add (bin, videotestsrc2);
  gst_bin_add (GST_BIN (source2), GST_ELEMENT (bin));
  g_object_set (source2, "start", (guint64) 0 * GST_SECOND, "duration",
      (guint64) 5 * GST_SECOND, "inpoint", (guint64) 0 * GST_SECOND, "priority",
      20, NULL);

  /* source3 */
  source3 = gst_element_factory_make ("gnlsource", "source3");
  videotestsrc2 = gst_element_factory_make ("videotestsrc", "videotestsrc3");
  gst_bin_add (GST_BIN (source3), videotestsrc2);
  g_object_set (source3, "start", (guint64) 0 * GST_SECOND, "duration",
      (guint64) 5 * GST_SECOND, "inpoint", (guint64) 0 * GST_SECOND, "priority",
      20, NULL);

  closure.composition = composition;
  closure.source3 = source3;
  g_object_connect (source2, "signal::no-more-pads",
      no_more_pads_test_cb, &closure, NULL);

  gst_bin_add (GST_BIN (composition), source1);
  g_signal_emit_by_name (composition, "commit", TRUE, &ret);
  g_object_connect (composition, "signal::pad-added",
      on_composition_pad_added_cb, fakesink, NULL);
  g_object_connect (composition, "signal::pad-removed",
      on_composition_pad_removed_cb, NULL, NULL);

  GST_DEBUG ("Adding composition to pipeline");

  gst_bin_add_many (GST_BIN (pipeline), composition, fakesink, NULL);

  GST_DEBUG ("Setting pipeline to PAUSED");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED)
      == GST_STATE_CHANGE_FAILURE);

  message = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    fail_error_message (message);
  }
  gst_message_unref (message);

  GST_DEBUG ("Adding second source");

  /* FIXME: maybe slow down the videotestsrc steaming thread */
  gst_bin_add (GST_BIN (composition), source2);
  g_signal_emit_by_name (composition, "commit", TRUE, &ret);

  message =
      gst_bus_timed_pop_filtered (bus, GST_SECOND / 10, GST_MESSAGE_ERROR);
  if (message) {
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      fail_error_message (message);
    } else {
      fail_if (TRUE);
    }

    gst_message_unref (message);
  }

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_simple_adder)
{
  GstBus *bus;
  GstMessage *message;
  GstElement *pipeline;
  GstElement *gnl_adder;
  GstElement *composition;
  GstElement *adder, *fakesink;
  GstClockTime start_playing_time;
  GstElement *gnlsource1, *gnlsource2;
  GstElement *audiotestsrc1, *audiotestsrc2;

  gboolean carry_on = TRUE, ret;
  GstClockTime total_time = 10 * GST_SECOND;

  pipeline = GST_ELEMENT (gst_pipeline_new (NULL));
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  composition = gst_element_factory_make ("gnlcomposition", "composition");
  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_object_set (fakesink, "sync", TRUE, NULL);

  /* gnl_adder */
  gnl_adder = gst_element_factory_make ("gnloperation", "gnl_adder");
  adder = gst_element_factory_make ("adder", "adder");
  fail_unless (adder != NULL);
  gst_bin_add (GST_BIN (gnl_adder), adder);
  g_object_set (gnl_adder, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time, "inpoint", (guint64) 0 * GST_SECOND,
      "priority", 0, NULL);
  gst_bin_add (GST_BIN (composition), gnl_adder);

  /* source 1 */
  gnlsource1 = gst_element_factory_make ("gnlsource", "gnlsource1");
  audiotestsrc1 = gst_element_factory_make ("audiotestsrc", "audiotestsrc1");
  gst_bin_add (GST_BIN (gnlsource1), audiotestsrc1);
  g_object_set (gnlsource1, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time / 2, "inpoint", (guint64) 0, "priority", 1, NULL);
  fail_unless (gst_bin_add (GST_BIN (composition), gnlsource1));

  /* gnlsource2 */
  gnlsource2 = gst_element_factory_make ("gnlsource", "gnlsource2");
  audiotestsrc2 = gst_element_factory_make ("audiotestsrc", "audiotestsrc2");
  gst_bin_add (GST_BIN (gnlsource2), GST_ELEMENT (audiotestsrc2));
  g_object_set (gnlsource2, "start", (guint64) 0 * GST_SECOND,
      "duration", total_time, "inpoint", (guint64) 0 * GST_SECOND, "priority",
      2, NULL);
  fail_unless (gst_bin_add (GST_BIN (composition), gnlsource2));

  /* Connecting signals */
  g_object_connect (composition, "signal::pad-added",
      on_composition_pad_added_cb, fakesink, NULL);
  g_object_connect (composition, "signal::pad-removed",
      on_composition_pad_removed_cb, NULL, NULL);


  GST_DEBUG ("Adding composition to pipeline");

  gst_bin_add_many (GST_BIN (pipeline), composition, fakesink, NULL);

  GST_DEBUG ("Setting pipeline to PAUSED");

  g_signal_emit_by_name (composition, "commit", TRUE, &ret);
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING)
      == GST_STATE_CHANGE_FAILURE);

  message = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR)
    fail_error_message (message);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gnl-simple-adder-test-play");

  /* Now play the 10 second composition */
  start_playing_time = gst_util_get_timestamp ();
  while (carry_on) {

    if (GST_CLOCK_DIFF (start_playing_time, gst_util_get_timestamp ()) >
        total_time + GST_SECOND) {
      GST_ERROR ("No EOS found after %" GST_TIME_FORMAT " sec",
          GST_TIME_ARGS ((total_time / GST_SECOND) + 1));
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gnl-simple-adder-test-fail");

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
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnlcomposition");
  TCase *tc_chain = tcase_create ("gnlcomposition");

  suite_add_tcase (s, tc_chain);

  g_cond_init (&pad_added_cond);
  g_mutex_init (&pad_added_lock);
  tcase_add_test (tc_chain, test_change_object_start_stop_in_current_stack);
  tcase_add_test (tc_chain, test_remove_invalid_object);
  if (gst_registry_check_feature_version (gst_registry_get (), "videomixer", 0,
          11, 0)) {
    tcase_add_test (tc_chain, test_no_more_pads_race);
  } else {
    GST_WARNING ("videomixer element not available, skipping 1 test");
  }

  if (gst_registry_check_feature_version (gst_registry_get (), "adder", 1,
          0, 0)) {
    tcase_add_test (tc_chain, test_simple_adder);
  } else {
    GST_WARNING ("adder element not available, skipping 1 test");
  }

  return s;
}

GST_CHECK_MAIN (gnonlin)
