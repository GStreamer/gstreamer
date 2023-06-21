#include "common.h"

GST_START_TEST (test_simple_videotestsrc)
{
  GstElement *pipeline;
  GstElement *nlesource, *sink;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;

  ges_init ();

  pipeline = gst_pipeline_new ("test_pipeline");

  /*
     Source 1
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  nlesource =
      videotest_nle_src ("source1", 1 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (nlesource == NULL);
  check_start_stop_duration (nlesource, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), nlesource, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = nlesource;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));

  gst_element_link (nlesource, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_if (sinkpad == NULL);
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (pipeline);

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (nlesource, "nlesource", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    GST_LOG ("poll");
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

  GST_DEBUG ("Setting pipeline to NULL");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  gst_object_unref (GST_OBJECT (sinkpad));

  GST_DEBUG ("Resetted pipeline to NULL");

  gst_object_unref (pipeline);
  gst_object_unref (bus);

  g_free (collect);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_videotestsrc_in_bin)
{
  GstElement *pipeline;
  GstElement *nlesource, *sink;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;

  ges_init ();

  pipeline = gst_pipeline_new ("test_pipeline");

  /*
     Source 1
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  nlesource = videotest_in_bin_nle_src ("source1", 0, 1 * GST_SECOND, 2, 1);
  /* Handle systems which don't have alpha available */
  if (nlesource == NULL)
    return;

  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), nlesource, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = nlesource;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));

  gst_element_link (nlesource, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_if (sinkpad == NULL);
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (pipeline);

  GST_DEBUG ("Setting pipeline to PLAYING");
  ASSERT_OBJECT_REFCOUNT (nlesource, "nlesource", 1);

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  GST_DEBUG ("Let's poll the bus");

  while (carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    GST_LOG ("poll");
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

  GST_DEBUG ("Setting pipeline to NULL");

  gst_object_unref (GST_OBJECT (sinkpad));

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to NULL");

  gst_object_unref (pipeline);
  gst_object_unref (bus);

  g_free (collect);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("nlesource");
  TCase *tc_chain = tcase_create ("nlesource");

  suite_add_tcase (s, tc_chain);

  if (0)
    tcase_add_test (tc_chain, test_simple_videotestsrc);
  tcase_add_test (tc_chain, test_videotestsrc_in_bin);

  return s;
}

GST_CHECK_MAIN (gnonlin)
