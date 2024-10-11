#include "common.h"

static void
test_simplest_full (void)
{
  gboolean ret = FALSE;
  GstElement *pipeline;
  GstElement *comp, *sink, *source1;
  CollectStructure *collect;
  GstBus *bus;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /*
     Source 1
     Start : 0s
     Duration : 1s
     Media start : 5s
     Media Duartion : 1s
     Priority : 1
   */
  source1 =
      videotest_nle_src_full ("source1", 0, 1 * GST_SECOND, 5 * GST_SECOND, 3,
      1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  fail_unless (ret);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));

  gst_element_link (comp, sink);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_ERROR ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  poll_the_bus (bus);

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_ERROR ("Resetted pipeline to READY");

  if (collect->seen_segments)
    g_list_free (collect->seen_segments);

  collect->seen_segments = NULL;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));
  collect->expected_base = 0;
  collect->gotsegment = FALSE;

  GST_ERROR ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus AGAIN");

  poll_the_bus (bus);

  fail_if (collect->expected_segments != NULL);

  gst_object_unref (GST_OBJECT (sinkpad));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_check_objects_destroyed_on_unref (pipeline, comp, source1, NULL);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);

  collect_free (collect);
}

static void
test_time_duration_full (void)
{
  gboolean ret = FALSE;
  GstElement *comp, *source1, *source2;

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /*
     Source 1
     Start : 0s
     Duration : 1s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 0, 1 * GST_SECOND, 3, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source2 = videotest_nle_src ("source2", 1 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  fail_unless (ret == TRUE);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Second source */

  ret = FALSE;
  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  fail_unless (ret == TRUE);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  /* Remove first source */

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  gst_object_ref (source1);
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 2);
  GST_ERROR_OBJECT (source1, "Num refs : %i", ((GObject *) source1)->ref_count);
  nle_composition_remove (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  GST_ERROR_OBJECT (source1, "Num refs : %i", ((GObject *) source1)->ref_count);
  check_start_stop_duration (comp, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Re-add first source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  gst_object_unref (source1);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  gst_element_set_state (comp, GST_STATE_NULL);
  gst_object_unref (comp);
}

static void
test_one_after_other_full (void)
{
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;

  gboolean ret = FALSE;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /*
     Source 1
     Start : 0s
     Duration : 1s
     Media start : 5s
     Priority : 1
   */
  source1 =
      videotest_nle_src_full ("source1", 0, 1 * GST_SECOND, 5 * GST_SECOND, 3,
      1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 1s
     Media start : 2s
     Priority : 1
   */
  source2 = videotest_nle_src_full ("source2", 1 * GST_SECOND, 1 * GST_SECOND,
      2 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */
  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Second source */
  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  fail_unless (ret);
  check_start_stop_duration (source1, 0 * GST_SECOND, 1 * GST_SECOND,
      1 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 1 * GST_SECOND));

  gst_element_link (comp, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
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
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");

  if (collect->seen_segments)
    g_list_free (collect->seen_segments);

  collect->seen_segments = NULL;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 1 * GST_SECOND));
  collect->gotsegment = FALSE;
  collect->expected_base = 0;


  GST_DEBUG ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  carry_on = TRUE;

  GST_DEBUG ("Let's poll the bus AGAIN");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          carry_on = FALSE;
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* We shouldn't see any segement messages, since we didn't do a segment seek */
          GST_WARNING ("Saw a Segment start/stop");
          fail_if (TRUE);
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    } else {
      GST_DEBUG ("bus_poll responded, but there wasn't any message...");
    }
  }

  fail_if (collect->expected_segments != NULL);

  gst_object_unref (GST_OBJECT (sinkpad));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  collect_free (collect);
}

static void
test_one_under_another_full (void)
{
  gboolean ret = FALSE;
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   * [-      source1       -]                                     | 1
   *             [-      source2       -]                         | 2
   * */

  /*
     Source 1
     Start : 0s
     Duration : 2s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 18, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 2s
     Priority : 2
   */
  source2 = videotest_nle_src ("source2", 1 * GST_SECOND, 2 * GST_SECOND, 0, 2);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);

  /* Add two sources */

  nle_composition_add (GST_BIN (comp), source1);
  nle_composition_add (GST_BIN (comp), source2);
  check_start_stop_duration (comp, 0, 0 * GST_SECOND, 0 * GST_SECOND);
  /* Now commiting changes */
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);

  /* Remove second source */

  gst_object_ref (source1);
  nle_composition_remove (GST_BIN (comp), source1);
  check_start_stop_duration (comp, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);

  /* Re-add second source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  gst_object_unref (source1);

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, GST_SECOND, 0));

  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, GST_SECOND, 2 * GST_SECOND,
          GST_SECOND));

  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));

  gst_element_link (comp, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          carry_on = FALSE;
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* check if the segment is the correct one (0s-4s) */
          carry_on = FALSE;
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_message_unref (message);
    }
  }

  fail_if (collect->expected_segments != NULL);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
  gst_object_unref (GST_OBJECT (sinkpad));
  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  collect_free (collect);
}

static void
test_one_bin_after_other_full (void)
{
  gboolean ret = FALSE;
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /*
     Source 1
     Start : 0s
     Duration : 1s
     Priority : 1
   */
  source1 = videotest_in_bin_nle_src ("source1", 0, 1 * GST_SECOND, 3, 1);
  if (source1 == NULL) {
    gst_object_unref (pipeline);
    gst_object_unref (comp);
    return;
  }
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source2 =
      videotest_in_bin_nle_src ("source2", 1 * GST_SECOND, 1 * GST_SECOND, 2,
      1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  fail_unless (ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  /* Remove first source */

  gst_object_ref (source1);
  nle_composition_remove (GST_BIN (comp), source1);
  check_start_stop_duration (comp, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Re-add first source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  gst_object_unref (source1);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));

  gst_element_link (comp, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
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
          fail_if (FALSE);
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");

  if (collect->seen_segments)
    g_list_free (collect->seen_segments);

  collect->seen_segments = NULL;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  collect->gotsegment = FALSE;
  collect->expected_base = 0;

  GST_DEBUG ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  carry_on = TRUE;
  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
          /* we should check if we really finished here */
          carry_on = FALSE;
          break;
        case GST_MESSAGE_SEGMENT_START:
        case GST_MESSAGE_SEGMENT_DONE:
          /* We shouldn't see any segement messages, since we didn't do a segment seek */
          GST_WARNING ("Saw a Segment start/stop");
          fail_if (FALSE);
          break;
        case GST_MESSAGE_ERROR:
          fail_error_message (message);
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  gst_object_unref (GST_OBJECT (sinkpad));

  fail_if (collect->expected_segments != NULL);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  collect_free (collect);
}

GST_START_TEST (test_simplest)
{
  ges_init ();
  test_simplest_full ();
  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_time_duration)
{
  ges_init ();
  test_time_duration_full ();
  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_after_other)
{
  ges_init ();
  test_one_after_other_full ();
  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_under_another)
{
  ges_init ();
  test_one_under_another_full ();
  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_bin_after_other)
{
  ges_init ();
  test_one_bin_after_other_full ();
  ges_deinit ();
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin-simple");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_time_duration);
  tcase_add_test (tc_chain, test_simplest);
  tcase_add_test (tc_chain, test_one_after_other);
  tcase_add_test (tc_chain, test_one_under_another);
  tcase_add_test (tc_chain, test_one_bin_after_other);
  return s;
}

GST_CHECK_MAIN (gnonlin)
