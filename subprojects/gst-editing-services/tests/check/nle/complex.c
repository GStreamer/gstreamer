#include "common.h"

static void
fill_pipeline_and_check (GstElement * comp, GList * segments,
    gint expected_error_domain)
{
  GstElement *pipeline, *sink;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;
  GList *listcopy = copy_segment_list (segments);

  pipeline = gst_pipeline_new ("test_pipeline");
  sink = gst_element_factory_make_or_warn ("fakevideosink", "sink");
  fail_if (sink == NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), comp, sink, NULL);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = sink;

  /* Expected segments */
  collect->expected_segments = segments;

  gst_element_link (comp, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");

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
        {
          GError *error = NULL;

          gst_message_parse_error (message, &error, NULL);
          if (comp == GST_ELEMENT (GST_MESSAGE_SRC (message)) &&
              expected_error_domain == error->domain) {
            GST_DEBUG ("Expected Error Message from %s : %s",
                GST_OBJECT_NAME (GST_MESSAGE_SRC (message)), error->message);
            carry_on = FALSE;
          } else {
            fail_unless (FALSE, "Error Message from %s : %s",
                GST_OBJECT_NAME (GST_MESSAGE_SRC (message)), error->message);
          }
          g_clear_error (&error);
        }
          break;
        default:
          break;
      }
      gst_mini_object_unref (GST_MINI_OBJECT (message));
    }
  }

  GST_DEBUG ("Setting pipeline to READY");


  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_READY) == GST_STATE_CHANGE_FAILURE);


  fail_if (collect->expected_segments != NULL);

  GST_DEBUG ("Resetted pipeline to READY");

  if (collect->seen_segments)
    g_list_free (collect->seen_segments);

  collect->seen_segments = NULL;
  collect->expected_segments = listcopy;
  collect->gotsegment = FALSE;
  collect->expected_base = 0;

  if (expected_error_domain)
    goto done;

  GST_DEBUG ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  carry_on = TRUE;

  GST_DEBUG ("Let's poll the bus");

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

done:
  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  gst_object_unref (GST_OBJECT (sinkpad));
  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  collect_free (collect);
}

GST_START_TEST (test_one_space_another)
{
  GstElement *comp, *source1, *source2;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   * [-source1--]            [-source2--]                         | 1
   * */

  /*
     Source 1
     Start : 0s
     Duration : 1s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 0, 1 * GST_SECOND, 2, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
     Source 2
     Start : 2s
     Duration : 1s
     Priority : 1
   */
  source2 = videotest_nle_src ("source2", 2 * GST_SECOND, 1 * GST_SECOND, 3, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 2 * GST_SECOND, 3 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  GST_ERROR ("doing one commit");
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  commit_and_wait (comp, &ret);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  /* Remove first source */

  gst_object_ref (source1);
  nle_composition_remove (GST_BIN (comp), source1);
  check_start_stop_duration (comp, 2 * GST_SECOND, 3 * GST_SECOND,
      1 * GST_SECOND);
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Re-add first source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  gst_object_unref (source1);
  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));

  fill_pipeline_and_check (comp, segments, GST_STREAM_ERROR);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_default_another)
{
  gboolean ret = FALSE;
  GstElement *comp, *source1, *source2, *source3, *defaultsrc;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   *             [-source1--]            [-source2--][-source3-]  | 1
   * [--------------------------defaultsource------------------]  | MAXUINT32
   * */


  /*
     defaultsrc source
     Start : 0s
     Duration : 5s
     Priority : 2
   */

  defaultsrc =
      videotest_nle_src ("defaultsrc", 0, 5 * GST_SECOND, 2, G_MAXUINT32);
  g_object_set (defaultsrc, "expandable", TRUE, NULL);
  fail_if (defaultsrc == NULL);
  check_start_stop_duration (defaultsrc, 0, 5 * GST_SECOND, 5 * GST_SECOND);

  /*
     Source 1
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 1 * GST_SECOND, 1 * GST_SECOND, 3, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 2
     Start : 3s
     Duration : 1s
     Priority : 1
   */
  source2 = videotest_nle_src ("source2", 3 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 3 * GST_SECOND, 4 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 3
     Start : 4s
     Duration : 1s
     Priority : 1
   */
  source3 = videotest_nle_src ("source3", 4 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source3 == NULL);
  check_start_stop_duration (source3, 4 * GST_SECOND, 5 * GST_SECOND,
      1 * GST_SECOND);


  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* defaultsrc source */
  nle_composition_add (GST_BIN (comp), defaultsrc);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (defaultsrc, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (defaultsrc, "defaultsrc", 1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);
  /* Third source */
  nle_composition_add (GST_BIN (comp), source3);
  commit_and_wait (comp, &ret);
  fail_unless (ret);
  check_start_stop_duration (comp, 0, 5 * GST_SECOND, 5 * GST_SECOND);
  check_start_stop_duration (defaultsrc, 0, 5 * GST_SECOND, 5 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source3, "source3", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          3 * GST_SECOND, 4 * GST_SECOND, 3 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 5 * GST_SECOND, 4 * GST_SECOND));

  fill_pipeline_and_check (comp, segments, GST_STREAM_ERROR);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_expandable_another)
{
  GstElement *comp, *source1, *source2, *source3, *defaultsrc;
  GList *segments = NULL;
  gboolean ret = FALSE;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   *             [ source1  ]            [ source2  ][ source3 ]  | 1
   * [--------------------- defaultsrc ------------------------]  | 1000 EXPANDABLE
   * */

  /*
     defaultsrc source
     Start : 0s
     Duration : 5s
     Priority : 1000
   */

  defaultsrc = videotest_nle_src ("defaultsrc", 0, 5 * GST_SECOND, 2, 1000);
  g_object_set (defaultsrc, "expandable", TRUE, NULL);
  fail_if (defaultsrc == NULL);
  check_start_stop_duration (defaultsrc, 0, 5 * GST_SECOND, 5 * GST_SECOND);

  /*
     Source 1
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 1 * GST_SECOND, 1 * GST_SECOND, 3, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 2
     Start : 3s
     Duration : 1s
     Priority : 1
   */
  source2 = videotest_nle_src ("source2", 3 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 3 * GST_SECOND, 4 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 3
     Start : 4s
     Duration : 1s
     Priority : 1
   */
  source3 = videotest_nle_src ("source3", 4 * GST_SECOND, 1 * GST_SECOND, 2, 1);
  fail_if (source3 == NULL);
  check_start_stop_duration (source3, 4 * GST_SECOND, 5 * GST_SECOND,
      1 * GST_SECOND);


  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* defaultsrc source */

  nle_composition_add (GST_BIN (comp), defaultsrc);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (defaultsrc, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (defaultsrc, "defaultsrc", 1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 4 * GST_SECOND, 4 * GST_SECOND);
  check_start_stop_duration (defaultsrc, 0, 4 * GST_SECOND, 4 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);


  /* Third source */

  nle_composition_add (GST_BIN (comp), source3);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 5 * GST_SECOND, 5 * GST_SECOND);
  check_start_stop_duration (defaultsrc, 0, 5 * GST_SECOND, 5 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source3, "source3", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          3 * GST_SECOND, 4 * GST_SECOND, 3 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 5 * GST_SECOND, 4 * GST_SECOND));

  fill_pipeline_and_check (comp, segments, 0);

  ges_deinit ();
}

GST_END_TEST;



GST_START_TEST (test_renegotiation)
{
  gboolean ret;
  GstElement *pipeline;
  GstElement *comp, *sink, *source1, *source2, *source3;
  GstElement *audioconvert;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE;
  GstPad *sinkpad;
  GstCaps *caps;

  ges_init ();

  pipeline = gst_pipeline_new ("test_pipeline");
  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /*
     Source 1
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source1 =
      audiotest_bin_src ("source1", 0 * GST_SECOND, 1 * GST_SECOND, 1, FALSE);
  check_start_stop_duration (source1, 0 * GST_SECOND, 1 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source2 =
      audiotest_bin_src ("source2", 1 * GST_SECOND, 1 * GST_SECOND, 1, TRUE);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  /*
     Source 3
     Start : 2s
     Duration : 1s
     Priority : 1
   */
  source3 =
      audiotest_bin_src ("source3", 2 * GST_SECOND, 1 * GST_SECOND, 1, FALSE);
  check_start_stop_duration (source3, 2 * GST_SECOND, 3 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);


  /* Third source */

  nle_composition_add (GST_BIN (comp), source3);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source3, "source3", 1);


  sink = gst_element_factory_make_or_warn ("fakeaudiosink", "sink");
  g_object_set (sink, "sync", FALSE, NULL);
  audioconvert = gst_element_factory_make_or_warn ("audioconvert", "aconv");

  gst_bin_add_many (GST_BIN (pipeline), comp, audioconvert, sink, NULL);
  caps = gst_caps_from_string ("audio/x-raw,format=(string)S16LE");
  fail_unless (gst_element_link_filtered (audioconvert, sink, caps));
  gst_caps_unref (caps);

  /* Shared data */
  collect = g_new0 (CollectStructure, 1);
  collect->comp = comp;
  collect->sink = audioconvert;

  /* Expected segments */
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));

  gst_element_link (comp, audioconvert);

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
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  collect->expected_segments = g_list_append (collect->expected_segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));
  collect->gotsegment = FALSE;
  collect->expected_base = 0;


  GST_DEBUG ("Setting pipeline to PLAYING again");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  carry_on = TRUE;

  GST_DEBUG ("Let's poll the bus");

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

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  gst_object_unref (GST_OBJECT (sinkpad));
  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  collect_free (collect);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_one_bin_space_another)
{
  GstElement *comp, *source1, *source2;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

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
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /*
     Source 2
     Start : 2s
     Duration : 1s
     Priority : 1
   */
  source2 =
      videotest_in_bin_nle_src ("source2", 2 * GST_SECOND, 1 * GST_SECOND, 2,
      1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 2 * GST_SECOND, 3 * GST_SECOND,
      1 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  /* Remove second source */

  gst_object_ref (source1);
  nle_composition_remove (GST_BIN (comp), source1);
  check_start_stop_duration (comp, 2 * GST_SECOND, 3 * GST_SECOND,
      1 * GST_SECOND);

  /* Re-add second source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  gst_object_unref (source1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));

  fill_pipeline_and_check (comp, segments, GST_STREAM_ERROR);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_one_above_another)
{
  GstElement *comp, *source1, *source2;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);
  fail_if (comp == NULL);

  /*
     Source 1
     Start : 0s
     Duration : 2s
     Priority : 2
   */
  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 3, 2);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /*
     Source 2
     Start : 2s
     Duration : 2s
     Priority : 1
   */
  source2 = videotest_nle_src ("source2", 1 * GST_SECOND, 2 * GST_SECOND, 2, 1);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

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

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));

  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 3 * GST_SECOND, 1 * GST_SECOND));

  fill_pipeline_and_check (comp, segments, 0);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin-complex");
  TCase *tc_chain = tcase_create ("complex");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_one_space_another);
  tcase_add_test (tc_chain, test_one_default_another);
  tcase_add_test (tc_chain, test_one_expandable_another);
  tcase_add_test (tc_chain, test_renegotiation);
  tcase_add_test (tc_chain, test_one_bin_space_another);
  tcase_add_test (tc_chain, test_one_above_another);
  return s;
}

GST_CHECK_MAIN (gnonlin)
