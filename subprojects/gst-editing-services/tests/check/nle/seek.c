#include "common.h"
static const gchar *compositor_element = NULL;

static void late_ges_init (void);

typedef struct _SeekInfo
{
  GstClockTime position;        /* Seek value and segment position */
  GstClockTime start;           /* Segment start */
  GstClockTime stop;            /* Segment stop */
  gboolean expect_failure;      /* Whether we expect the seek to fail or not */
} SeekInfo;

static SeekInfo *
new_seek_info (GstClockTime position, GstClockTime start, GstClockTime stop,
    gboolean expect_failure)
{
  SeekInfo *info = g_new0 (SeekInfo, 1);

  info->position = position;
  info->start = start;
  info->stop = stop;
  info->expect_failure = expect_failure;

  return info;
}

static void
fill_pipeline_and_check (GstElement * comp, GList * segments, GList * seeks)
{
  GstElement *pipeline, *sink;
  CollectStructure *collect;
  GstBus *bus;
  GstMessage *message;
  gboolean carry_on = TRUE, expected_failure;
  GstPad *sinkpad;
  GList *ltofree = seeks;

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
  collect->keep_expected_segments = TRUE;

  gst_element_link (comp, sink);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
      (GstPadProbeCallback) sinkpad_probe, collect, NULL);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  GST_DEBUG ("Setting pipeline to PLAYING");

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE);

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
          break;
        case GST_MESSAGE_ASYNC_DONE:
          GST_DEBUG ("prerolling done");

          if (seeks == NULL) {
            carry_on = FALSE;
            g_list_free_full (collect->expected_segments, g_free);
            collect->expected_segments = NULL;
            GST_DEBUG ("Done seeking");
            break;
          }

          g_list_free_full (collect->expected_segments, g_free);
          collect->expected_segments = NULL;
          expected_failure = TRUE;
          while (expected_failure && carry_on) {
            SeekInfo *sinfo = (SeekInfo *) seeks->data;

            seeks = seeks->next;

            if (!sinfo->expect_failure) {
              collect->gotsegment = FALSE;
              collect->expected_base = 0;
              collect->expected_segments =
                  g_list_append (collect->expected_segments, segment_new (1.0,
                      GST_FORMAT_TIME, sinfo->start, sinfo->stop,
                      sinfo->position));

              expected_failure = FALSE;
            }

            GST_DEBUG ("Seeking to %" GST_TIME_FORMAT ", Expecting (%"
                GST_TIME_FORMAT " %" GST_TIME_FORMAT ")",
                GST_TIME_ARGS (sinfo->position), GST_TIME_ARGS (sinfo->start),
                GST_TIME_ARGS (sinfo->stop));

            fail_unless_equals_int (gst_element_seek_simple (pipeline,
                    GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, sinfo->position),
                !sinfo->expect_failure);

            if (!sinfo->expect_failure) {
              g_free (sinfo);
              break;
            }

            if (seeks == NULL)
              carry_on = FALSE;
            g_free (sinfo);
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

  fail_if (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_NULL) == GST_STATE_CHANGE_FAILURE);

  ASSERT_OBJECT_REFCOUNT_BETWEEN (pipeline, "main pipeline", 1, 2);
  gst_object_unref (pipeline);
  ASSERT_OBJECT_REFCOUNT_BETWEEN (bus, "main bus", 1, 2);
  gst_object_unref (bus);

  g_list_free (ltofree);
  g_free (collect);
}

static void
test_simplest_full (void)
{
  gboolean ret;
  GstElement *comp, *source1;
  GList *segments = NULL;
  GList *seeks = NULL;

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
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

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (comp, 0, 1 * GST_SECOND, 1 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));

  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 5.5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0 * GST_SECOND, 5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND - 1, 6 * GST_SECOND - 1,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND, 6 * GST_SECOND,
          6 * GST_SECOND, TRUE));
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 5.5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0 * GST_SECOND, 5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND - 1, 6 * GST_SECOND - 1,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND, 6 * GST_SECOND,
          6 * GST_SECOND, TRUE));

  fill_pipeline_and_check (comp, segments, seeks);
}

static void
test_one_after_other_full (void)
{
  gboolean ret;
  GstElement *comp, *source1, *source2;
  GList *segments = NULL, *seeks = NULL;

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   * [5 source1 ][2 source2 ]                                     | 1
   *
   * */

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

  /* Add sources */
  nle_composition_add (GST_BIN (comp), source1);
  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);


  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 5 * GST_SECOND, 6 * GST_SECOND, 0));

  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 5.5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0 * GST_SECOND, 5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND - 1, 6 * GST_SECOND - 1,
          6 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND, 2 * GST_SECOND,
          3 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2 * GST_SECOND - 1,
          3 * GST_SECOND - 1, 3 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2 * GST_SECOND, 3 * GST_SECOND,
          3 * GST_SECOND, TRUE));


  fill_pipeline_and_check (comp, segments, seeks);
}

static void
test_one_under_another_full (void)
{
  gboolean ret;
  GstElement *comp, *source1, *source2;
  GList *segments = NULL, *seeks = NULL;

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
  fail_if (comp == NULL);

  /* TOPOLOGY
   *
   * 0           1           2           3           4          5 | Priority
   * ----------------------------------------------------------------------------
   * [       source1        ]                                     | 1
   *             [        source2       ]                         | 2
   *
   * */

  /*
     Source 1
     Start : 0s
     Duration : 2s
     Priority : 1
   */
  source1 = videotest_nle_src ("source1", 0, 2 * GST_SECOND, 3, 1);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  /*
     Source 2
     Start : 1s
     Duration : 2s
     Priority : 2
   */
  source2 = videotest_nle_src ("source2", 1 * GST_SECOND, 2 * GST_SECOND, 2, 2);
  fail_if (source2 == NULL);
  check_start_stop_duration (source2, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);

  /* Add two sources */

  nle_composition_add (GST_BIN (comp), source1);
  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 2 * GST_SECOND, 2 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 3 * GST_SECOND,
      2 * GST_SECOND);
  check_start_stop_duration (comp, 0, 3 * GST_SECOND, 3 * GST_SECOND);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, GST_SECOND, 0));


  /* Hit source1 */
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0.5 * GST_SECOND,
          1 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0 * GST_SECOND, 0 * GST_SECOND,
          1 * GST_SECOND, FALSE));
  /* Hit source1 over source2 */
  seeks =
      g_list_append (seeks, new_seek_info (1 * GST_SECOND, 1 * GST_SECOND,
          2 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (1.5 * GST_SECOND, 1.5 * GST_SECOND,
          2 * GST_SECOND, FALSE));
  /* Hit source2 */
  seeks =
      g_list_append (seeks, new_seek_info (2 * GST_SECOND, 2 * GST_SECOND,
          3 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 2.5 * GST_SECOND,
          3 * GST_SECOND, FALSE));

  fill_pipeline_and_check (comp, segments, seeks);
}

static void
test_one_bin_after_other_full (void)
{
  gboolean ret = FALSE;
  GstElement *comp, *source1, *source2;
  GList *segments = NULL, *seeks = NULL;

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");
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
     Start : 1s
     Duration : 1s
     Priority : 1
   */
  source2 =
      videotest_in_bin_nle_src ("source2", 1 * GST_SECOND, 1 * GST_SECOND, 2,
      1);
  fail_if (source2 == NULL);

  /* Add one source */

  nle_composition_add (GST_BIN (comp), source1);

  /* Second source */

  nle_composition_add (GST_BIN (comp), source2);
  commit_and_wait (comp, &ret);
  check_start_stop_duration (source1, 0, 1 * GST_SECOND, 1 * GST_SECOND);
  check_start_stop_duration (source2, 1 * GST_SECOND, 2 * GST_SECOND,
      1 * GST_SECOND);
  check_start_stop_duration (comp, 0, 2 * GST_SECOND, 2 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (source1, "source1", 1);
  ASSERT_OBJECT_REFCOUNT (source2, "source2", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 1 * GST_SECOND, 0));

  /* Hit source1 */
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0.5 * GST_SECOND,
          GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0 * GST_SECOND, 0 * GST_SECOND,
          GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND - 1, GST_SECOND - 1,
          GST_SECOND, FALSE));
  /* Hit source2 */
  seeks =
      g_list_append (seeks, new_seek_info (1.5 * GST_SECOND, 1.5 * GST_SECOND,
          2 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (GST_SECOND, GST_SECOND,
          2 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2 * GST_SECOND - 1,
          2 * GST_SECOND - 1, 2 * GST_SECOND, FALSE));
  /* Should fail */
  seeks =
      g_list_append (seeks, new_seek_info (2 * GST_SECOND, GST_SECOND,
          GST_SECOND, TRUE));

  fill_pipeline_and_check (comp, segments, seeks);
}


GST_START_TEST (test_complex_operations)
{
  gboolean ret = FALSE;
  GstElement *comp, *oper, *source1, *source2;
  GList *segments = NULL, *seeks = NULL;

  late_ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");

  /* TOPOLOGY
   *
   * 0           1           2           3           4     ..   6 | Priority
   * ----------------------------------------------------------------------------
   *                         [------ oper ----------]             | 1
   * [--------------------- source1 ----------------]             | 2
   *                         [------------ source2       ------]  | 3
   * */

  /*
     source1
     Start : 0s
     Duration : 4s
     Priority : 3
   */

  source1 = videotest_in_bin_nle_src ("source1", 0, 4 * GST_SECOND, 2, 3);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 4 * GST_SECOND, 4 * GST_SECOND);

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
  check_start_stop_duration (source2, 2 * GST_SECOND, 6 * GST_SECOND,
      4 * GST_SECOND);

  /*
     operation
     Start : 2s
     Duration : 2s
     Priority : 1
   */

  oper =
      new_operation ("oper", compositor_element, 2 * GST_SECOND, 2 * GST_SECOND,
      1);
  fail_if (oper == NULL);
  check_start_stop_duration (oper, 2 * GST_SECOND, 4 * GST_SECOND,
      2 * GST_SECOND);

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

  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));

  /* Seeks */
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0.5 * GST_SECOND,
          2 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (4.5 * GST_SECOND, 4.5 * GST_SECOND,
          6 * GST_SECOND, FALSE));
  /* and backwards */
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0.5 * GST_SECOND,
          2 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (4.5 * GST_SECOND, 4.5 * GST_SECOND,
          6 * GST_SECOND, FALSE));

  fill_pipeline_and_check (comp, segments, seeks);
}

GST_END_TEST;


GST_START_TEST (test_complex_operations_bis)
{
  gboolean ret = FALSE;
  GstElement *comp, *oper, *source1, *source2;
  GList *segments = NULL, *seeks = NULL;

  late_ges_init ();

  comp =
      gst_element_factory_make_or_warn ("nlecomposition", "test_composition");

  /* TOPOLOGY
   *
   * 0           1           2           3           4     ..   6 | Priority
   * ----------------------------------------------------------------------------
   * [ ......................[------ oper ----------]..........]  | 1 EXPANDABLE
   * [--------------------- source1 ----------------]             | 2
   *                         [------------ source2       ------]  | 3
   * */


  /*
     source1
     Start : 0s
     Duration : 4s
     Priority : 2
   */

  source1 = videotest_in_bin_nle_src ("source1", 0, 4 * GST_SECOND, 3, 2);
  fail_if (source1 == NULL);
  check_start_stop_duration (source1, 0, 4 * GST_SECOND, 4 * GST_SECOND);

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
  check_start_stop_duration (source2, 2 * GST_SECOND, 6 * GST_SECOND,
      4 * GST_SECOND);

  /*
     operation
     Start : 2s
     Duration : 2s
     Priority : 1
     EXPANDABLE
   */

  oper =
      new_operation ("oper", compositor_element, 2 * GST_SECOND, 2 * GST_SECOND,
      1);
  fail_if (oper == NULL);
  check_start_stop_duration (oper, 2 * GST_SECOND, 4 * GST_SECOND,
      2 * GST_SECOND);
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
  check_start_stop_duration (source1, 0, 4 * GST_SECOND, 4 * GST_SECOND);
  check_start_stop_duration (source2, 2 * GST_SECOND, 6 * GST_SECOND,
      4 * GST_SECOND);
  check_start_stop_duration (comp, 0, 6 * GST_SECOND, 6 * GST_SECOND);
  check_start_stop_duration (oper, 0 * GST_SECOND, 6 * GST_SECOND,
      6 * GST_SECOND);

  ASSERT_OBJECT_REFCOUNT (oper, "oper", 1);

  /* Expected segments */
  segments = g_list_append (segments,
      segment_new (1.0, GST_FORMAT_TIME, 0, 2 * GST_SECOND, 0));

  /* Seeks */
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (4.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  /* and backwards */
  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (0.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));

  seeks =
      g_list_append (seeks, new_seek_info (2.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));
  seeks =
      g_list_append (seeks, new_seek_info (4.5 * GST_SECOND, 0 * GST_SECOND,
          1.5 * GST_SECOND, FALSE));

  fill_pipeline_and_check (comp, segments, seeks);
}

GST_END_TEST;


GST_START_TEST (test_simplest)
{
  late_ges_init ();

  test_simplest_full ();
}

GST_END_TEST;


GST_START_TEST (test_one_after_other)
{
  late_ges_init ();

  test_one_after_other_full ();
}

GST_END_TEST;


GST_START_TEST (test_one_under_another)
{
  late_ges_init ();

  test_one_under_another_full ();
}

GST_END_TEST;


GST_START_TEST (test_one_bin_after_other)
{
  late_ges_init ();

  test_one_bin_after_other_full ();
}

GST_END_TEST;

static void
late_ges_init ()
{
  /* We need to do this inside the test cases, not during the initialization
   * of the suite, as ges_init() will initialize thread pools, which cannot
   * work properly after a fork. */

  if (atexit (ges_deinit) != 0) {
    GST_ERROR ("failed to set ges_deinit as exit function");
  }

  ges_init ();
}

static Suite *
gnonlin_suite (void)
{
  Suite *s = suite_create ("gnonlin-seek");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  if (gst_registry_check_feature_version (gst_registry_get (), "compositor", 1,
          0, 0)) {
    compositor_element = "compositor";
  } else if (gst_registry_check_feature_version (gst_registry_get (),
          "videomixer", 1, 0, 0)) {
    compositor_element = "videomixer";

  }

  tcase_add_test (tc_chain, test_simplest);
  tcase_add_test (tc_chain, test_one_after_other);
  tcase_add_test (tc_chain, test_one_under_another);
  tcase_add_test (tc_chain, test_one_bin_after_other);

  if (compositor_element) {
    tcase_add_test (tc_chain, test_complex_operations);
    tcase_add_test (tc_chain, test_complex_operations_bis);
  } else {
    GST_WARNING ("No compositor element, can not run operations tests");
  }

  return s;
}

GST_CHECK_MAIN (gnonlin)
