#include "common.h"

static void
fill_pipeline_and_check (GstElement * comp, GList * segments)
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
          fail_error_message (message);
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
  collect->expected_base = 0;
  collect->expected_segments = listcopy;
  collect->gotsegment = FALSE;

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
}

GST_START_TEST (test_simple_operation)
{
  gboolean ret = FALSE;
  GstElement *comp, *oper, *source;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   *             [-- oper --]                                     | 0
   * [------------- source -------------]                         | 1
   * */

  /*
     source
     Start : 0s
     Duration : 3s
     Priority : 1
   */

  source = videotest_nle_src ("source", 0, 3 * GST_SECOND, 2, 1);
  fail_if (source == NULL);

  /*
     operation
     Start : 1s
     Duration : 1s
     Priority : 0
   */

  oper = new_operation ("oper", "identity", 1 * GST_SECOND, 1 * GST_SECOND, 0);
  fail_if (oper == NULL);

  /* Add source */
  ASSERT_OBJECT_REFCOUNT (source, "source", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  nle_composition_add (GST_BIN (comp), source);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source, "source", 1);

  /* Add operaton */

  nle_composition_add (GST_BIN (comp), oper);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* remove source */

  gst_object_ref (source);
  nle_composition_remove (GST_BIN (comp), source);
  check_start_stop_duration (comp, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source, "source", 1);

  /* re-add source */
  nle_composition_add (GST_BIN (comp), source);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);
  gst_object_unref (source);

  ASSERT_OBJECT_REFCOUNT (source, "source", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 3 * GST_SECOND, 2 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_pyramid_operations)
{
  GstElement *comp, *oper1, *oper2, *source;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /*
     source
     Start : 0s
     Duration : 10s
     Priority : 2
   */

  source = videotest_nle_src ("source", 0, 10 * GST_SECOND, 2, 2);

  /*
     operation1
     Start : 4s
     Duration : 2s
     Priority : 1
   */

  oper1 =
      new_operation ("oper1", "identity", 4 * GST_SECOND, 2 * GST_SECOND, 1);

  /*
     operation2
     Start : 2s
     Duration : 6s
     Priority : 0
   */

  oper2 =
      new_operation ("oper2", "identity", 2 * GST_SECOND, 6 * GST_SECOND, 0);

  /* Add source */
  ASSERT_OBJECT_REFCOUNT (source, "source", 1);
  ASSERT_OBJECT_REFCOUNT (oper1, "oper1", 1);
  ASSERT_OBJECT_REFCOUNT (oper2, "oper2", 1);

  nle_composition_add (GST_BIN (comp), source);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source, 0, 10 * GST_SECOND, 10 * GST_SECOND);
  check_start_stop_duration (comp, 0, 10 * GST_SECOND, 10 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source, "source", 1);

  /* Add operation 1 */

  nle_composition_add (GST_BIN (comp), oper1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (oper1, 4 * GST_SECOND, 6 * GST_SECOND,
      2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 10 * GST_SECOND, 10 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper1, "oper1", 1);

  /* Add operation 2 */

  nle_composition_add (GST_BIN (comp), oper2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (oper2, 2 * GST_SECOND, 8 * GST_SECOND,
      6 * GST_SECOND);
  check_start_stop_duration (comp, 0, 10 * GST_SECOND, 10 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper1, "oper2", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 4 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 6 * GST_SECOND, 4 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          6 * GST_SECOND, 8 * GST_SECOND, 6 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          8 * GST_SECOND, 10 * GST_SECOND, 8 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_pyramid_operations2)
{
  gboolean ret;
  GstElement *comp, *oper, *source1, *source2, *def;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /*
     source1
     Start : 0s
     Duration : 2s
     Priority : 2
   */

  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 2, 2);

  /*
     operation
     Start : 1s
     Duration : 4s
     Priority : 1
   */

  oper = new_operation ("oper", "identity", 1 * GST_SECOND, 4 * GST_SECOND, 1);

  /*
     source2
     Start : 4s
     Duration : 2s
     Priority : 2
   */

  source2 = videotest_nle_src ("source2", 4 * GST_SECOND, 2 * GST_SECOND, 2, 2);

  /*
     def (default source)
     Priority = G_MAXUINT32
   */
  def =
      videotest_nle_src ("default", 0 * GST_SECOND, 0 * GST_SECOND, 2,
      G_MAXUINT32);
  g_object_set (def, "expandable", TRUE, NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);
  ASSERT_OBJECT_REFCOUNT (def, "default", 1);

  /* Add source 1 */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /* Add source 2 */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);

  /* Add operation */

  nle_composition_add (GST_BIN (comp), oper);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);

  /* Add default */

  nle_composition_add (GST_BIN (comp), def);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);


  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          1 * GST_SECOND, 2 * GST_SECOND, 1 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 4 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 5 * GST_SECOND, 4 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          5 * GST_SECOND, 6 * GST_SECOND, 5 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_pyramid_operations_expandable)
{
  GstElement *comp, *oper, *source1, *source2, *def;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /*
     source1
     Start : 0s
     Duration : 2s
     Priority : 2
   */

  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 2, 2);

  /*
     operation (expandable)
     Start : XX
     Duration : XX
     Priority : 1
   */

  oper = new_operation ("oper", "identity", 1 * GST_SECOND, 4 * GST_SECOND, 1);
  g_object_set (oper, "expandable", TRUE, NULL);

  /*
     source2
     Start : 4s
     Duration : 2s
     Priority : 2
   */

  source2 = videotest_nle_src ("source2", 4 * GST_SECOND, 2 * GST_SECOND, 2, 2);

  /*
     def (default source)
     Priority = G_MAXUINT32
   */
  def =
      videotest_nle_src ("default", 0 * GST_SECOND, 0 * GST_SECOND, 2,
      G_MAXUINT32);
  g_object_set (def, "expandable", TRUE, NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);
  ASSERT_OBJECT_REFCOUNT (def, "default", 1);

  /* Add source 1 */
  nle_composition_add (GST_BIN (comp), source1);
  /* Add source 2 */
  nle_composition_add (GST_BIN (comp), source2);
  /* Add operation */
  nle_composition_add (GST_BIN (comp), oper);
  /* Add default */
  nle_composition_add (GST_BIN (comp), def);

  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (oper, 0 * GST_SECOND, 6 * GST_SECOND,
      6 * GST_SECOND);
  check_start_stop_duration (source2, 4 * GST_SECOND, 6 * GST_SECOND,
      2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          2 * GST_SECOND, 4 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 6 * GST_SECOND, 4 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_complex_operations)
{
  GstElement *comp, *oper, *source1, *source2;
  gboolean ret = FALSE;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /* TOPOLOGY
   *
   * 0           1           2           3           4     5    6 | Priority
   * ----------------------------------------------------------------------------
   *                         [    -oper-            ]             | 1
   *                         [    -source2-                   -]  | 2
   * [                    -source1-                -]             | 3
   * */

  /*
     source1
     Start : 0s
     Duration : 4s
     Priority : 3
   */

  source1 = videotest_in_bin_nle_src ("source1", 0, 4 * GST_SECOND, 2, 3);
  fail_if (source1 == NULL);

  /*
     source2
     Start : 2s
     Duration : 4s
     Priority : 2
   */

  source2 =
      videotest_in_bin_nle_src ("source2", 2 * GST_SECOND, 4 * GST_SECOND, 2,
      2);
  fail_if (source2 == NULL);

  /*
     operation
     Start : 2s
     Duration : 2s
     Priority : 1
   */

  oper =
      new_operation ("oper", "compositor", 2 * GST_SECOND, 2 * GST_SECOND, 1);
  fail_if (oper == NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Add source1 */
  nle_composition_add (GST_BIN (comp), source1);
  check_start_stop_duration (comp, 0, 0, 0);
  /* If the composition already processed the source, the refcount
   * might be 2 */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (source1, "source1", 1, 2);

  /* Add source2 */
  nle_composition_add (GST_BIN (comp), source2);
  check_start_stop_duration (comp, 0, 0, 0);
  /* If the composition already processed the source, the refcount
   * might be 2 */
  ASSERT_OBJECT_REFCOUNT_BETWEEN (source2, "source2", 1, 2);

  /* Add operaton */
  nle_composition_add (GST_BIN (comp), oper);
  check_start_stop_duration (comp, 0, 0, 0);

  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          0 * GST_SECOND, 2 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          4 * GST_SECOND, 6 * GST_SECOND, 4 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;


GST_START_TEST (test_complex_operations_bis)
{
  GstElement *comp, *oper, *source1, *source2;
  gboolean ret;
  GList *segments = NULL;

  ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  gst_element_set_state (comp, GST_STATE_READY);

  /* TOPOLOGY
   *
   * 0           1           2           3           4     ..   6 | Priority
   * ----------------------------------------------------------------------------
   * [ ......................[------ oper ----------]..........]  | 1 EXPANDABLE
   * [--------------------- source1 ----------------]             | 2
   *                         [------------ source2 ------------]  | 3
   * */


  /*
     source1
     Start : 0s
     Duration : 4s
     Priority : 2
   */

  source1 = videotest_in_bin_nle_src ("source1", 0, 4 * GST_SECOND, 3, 2);
  fail_if (source1 == NULL);

  /*
     source2
     Start : 2s
     Duration : 4s
     Priority : 3
   */

  source2 =
      videotest_in_bin_nle_src ("source2", 2 * GST_SECOND, 4 * GST_SECOND, 2,
      3);
  fail_if (source2 == NULL);

  /*
     operation
     Start : 2s
     Duration : 2s
     Priority : 1
     EXPANDABLE
   */

  oper =
      new_operation ("oper", "compositor", 2 * GST_SECOND, 2 * GST_SECOND, 1);
  fail_if (oper == NULL);
  g_object_set (oper, "expandable", TRUE, NULL);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);
  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Add source1 */
  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 4 * GST_SECOND, 4 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Add source2 */
  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  /* Add operaton */

  nle_composition_add (GST_BIN (comp), oper);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);
  /* Since it's expandable, it should have changed to full length */
  check_start_stop_duration (oper, 0 * GST_SECOND, 6 * GST_SECOND,
      6 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          0 * GST_SECOND, 2 * GST_SECOND, 2 * GST_SECOND));
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME,
          0 * GST_SECOND, 2 * GST_SECOND, 4 * GST_SECOND));

  fill_pipeline_and_check (comp, segments);

  ges_deinit ();
}

GST_END_TEST;



static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("nleoperation");
  TCase *tc_chain = tcase_create ("nleoperation");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_simple_operation);
  tcase_add_test (tc_chain, test_pyramid_operations);
  tcase_add_test (tc_chain, test_pyramid_operations2);
  tcase_add_test (tc_chain, test_pyramid_operations_expandable);
  if (gst_registry_check_feature_version (gst_registry_get (), "compositor", 0,
          11, 0)) {
    tcase_add_test (tc_chain, test_complex_operations);
    tcase_add_test (tc_chain, test_complex_operations_bis);
  } else
    GST_WARNING ("compositor element not available, skipping 1 test");

  return s;
}

GST_CHECK_MAIN (gnonlin)
