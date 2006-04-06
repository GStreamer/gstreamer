/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * cleanup.c: Unit test for cleanup of pipelines
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
  GError *error = NULL;

  pipeline = gst_parse_launch (pipe_descr, &error);
  if (error != NULL) {
    fail_if (error != NULL, "Error parsing pipeline %s: %s", pipe_descr,
        error->message);
    g_error_free (error);
  }
  fail_unless (pipeline != NULL, "Failed to create pipeline %s", pipe_descr);
  return pipeline;
}

static void
expected_fail_pipe (const gchar * pipe_descr)
{
  GstElement *pipeline;
  GError *error = NULL;

#ifndef GST_DISABLE_GST_DEBUG
  gst_debug_set_default_threshold (GST_LEVEL_NONE);
#endif

  pipeline = gst_parse_launch (pipe_descr, &error);
  fail_unless (error != NULL, "Expected failure pipeline %s: succeeded!");
  g_error_free (error);

  /* We get a pipeline back even when parsing has failed, sometimes! */
  if (pipeline)
    gst_object_unref (pipeline);
}

static void
check_pipeline_runs (GstElement * p)
{
  GstStateChangeReturn ret;

  /* Check that the pipeline changes state to PAUSED and back to NULL */
  ret = gst_element_set_state (p, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_ASYNC)
    ret = gst_element_get_state (p, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret != GST_STATE_CHANGE_FAILURE,
      "Could not set pipeline to paused");

  ret = gst_element_set_state (p, GST_STATE_NULL);
  if (ret == GST_STATE_CHANGE_ASYNC)
    ret = gst_element_get_state (p, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret != GST_STATE_CHANGE_FAILURE,
      "Could not set pipeline to null");
}

static const gchar *test_lines[] = {
  "filesrc location=music.mp3 ! identity ! fakesink",
  "filesrc location=music.ogg ! tee ! identity ! identity ! fakesink",
  "filesrc location=http://domain.com/music.mp3 ! identity ! fakesink",
  "filesrc location=movie.avi ! tee name=demuxer ! ( queue ! identity ! fakesink ) ( demuxer. ! queue ! identity ! fakesink )",
  "fakesrc ! video/x-raw-yuv ! fakesink",
  "fakesrc !   video/raw,  format=(fourcc)YUY2; video/raw, format=(fourcc)YV12 ! fakesink",
  "fakesrc ! audio/x-raw-int, width=[16,  32], depth={16, 24, 32}, signed=TRUE ! fakesink",
  NULL
};

GST_START_TEST (test_launch_lines)
{
  GstElement *pipeline;
  const gchar **s;

  for (s = test_lines; *s != NULL; s++) {
    pipeline = setup_pipeline (*s);
    gst_object_unref (pipeline);
  }
}

GST_END_TEST;

#define PIPELINE1  "fakesrc"
#define PIPELINE2  "fakesrc name=donald num-buffers= 27 silent =TruE sizetype = 3 data=   Subbuffer\\ data"
#define PIPELINE3  "fakesrc identity fakesink"
#define PIPELINE4  "fakesrc num-buffers=4 .src ! identity !.sink identity .src ! .sink fakesink"
#define PIPELINE5  "fakesrc num-buffers=4 name=src identity name=id1 identity name = id2 fakesink name =sink src. ! id1. id1.! id2.sink id2.src!sink.sink"
#define PIPELINE6  "pipeline.(name=\"john\" fakesrc num-buffers=4 ( bin. ( ! queue ! identity !( queue ! fakesink )) ))"
#define PIPELINE7  "fakesrc num-buffers=4 ! tee name=tee .src%d! queue ! fakesink tee.src%d ! queue ! fakesink queue name =\"foo\" ! fakesink tee.src%d ! foo."
/* aggregator is borked
 * #define PIPELINE8  "fakesrc num-buffers=4 ! tee name=tee1 .src0,src1 ! .sink0, sink1 aggregator ! fakesink"
 * */
#define PIPELINE8  "fakesrc num-buffers=4 ! fakesink"
#define PIPELINE9  "fakesrc num-buffers=4 ! test. fakesink name=test"
#define PIPELINE10 "( fakesrc num-buffers=\"4\" ! ) identity ! fakesink"
#define PIPELINE11 "fakesink name = sink identity name=id ( fakesrc num-buffers=\"4\" ! id. ) id. ! sink."
#define PIPELINE12 "fakesrc num-buffers=4 name=\"a=b\"  a=b. ! fakesink"
#define PIPELINE13 "file:///tmp/test.file ! fakesink"

GST_START_TEST (test_launch_lines2)
{
  GstElement *cur;
  gint i;
  gboolean b;
  gchar *s = NULL;

  /**
   * checks:
   * - specifying an element works :)
   * - if only 1 element is requested, no bin is returned, but the element
   */
  cur = setup_pipeline (PIPELINE1);
  fail_unless (G_OBJECT_TYPE (cur) == g_type_from_name ("GstFakeSrc"),
      "parse_launch did not produce a fakesrc");
  gst_object_unref (cur);

  /**
   * checks:
   * - properties works
   * - string, int, boolean and enums can be properly set
   * - first test of escaping strings
   */
  cur = setup_pipeline (PIPELINE2);
  g_object_get (G_OBJECT (cur), "name", &s, "num-buffers", &i,
      "silent", &b, NULL);
  fail_if (s == NULL, "name was NULL");
  fail_unless (strcmp (s, "donald") == 0, "fakesrc name was not 'donald'");
  fail_unless (i == 27, "num-buffers was not 27");
  fail_unless (b == TRUE, "silent was not TRUE");
  g_free (s);

  g_object_get (G_OBJECT (cur), "sizetype", &i, NULL);
  fail_unless (i == 3, "sizetype != 3");

  g_object_get (G_OBJECT (cur), "data", &i, NULL);
  fail_unless (i == 2, "data != 2");
  gst_object_unref (cur);

  /**
   * checks:
   * - specifying multiple elements without links works
   * - if multiple toplevel elements exist, a pipeline is returned
   */
  cur = setup_pipeline (PIPELINE3);
  fail_unless (GST_BIN_NUMCHILDREN (cur) == 3,
      "Pipeline does not contain 3 children");
  gst_object_unref (cur);

  /**
   * checks:
   * - test default link "!"
   * - test if specifying pads on links works
   */
  cur = setup_pipeline (PIPELINE4);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - test if appending the links works, too
   * - check if the pipeline constructed works the same as the one before (how?)
   */
  cur = setup_pipeline (PIPELINE5);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - test various types of bins
   * - test if linking across bins works
   * - test if escaping strings works
   */
  cur = setup_pipeline (PIPELINE6);
  fail_unless (GST_IS_PIPELINE (cur), "Parse did not produce a pipeline");
  g_object_get (G_OBJECT (cur), "name", &s, NULL);
  fail_if (s == NULL, "name was NULL");
  fail_unless (strcmp (s, "john") == 0, "Name was not 'john'");
  g_free (s);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - test request pads
   */
  cur = setup_pipeline (PIPELINE7);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - multiple pads on 1 link
   */
  cur = setup_pipeline (PIPELINE8);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - failed in grammar.y cvs version 1.17
   */
  cur = setup_pipeline (PIPELINE9);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - failed in grammar.y cvs version 1.17
   */
  cur = setup_pipeline (PIPELINE10);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:
   * - failed in grammar.y cvs version 1.18
   */
  cur = setup_pipeline (PIPELINE11);
  check_pipeline_runs (cur);
  gst_object_unref (cur);

  /**
   * checks:    
   * - fails because a=b. is not a valid element reference in parse.l 
   */
  expected_fail_pipe (PIPELINE12);

  /**
   * checks:
   * - URI detection works
   */
  cur = setup_pipeline (PIPELINE13);
  gst_object_unref (cur);
}

GST_END_TEST;

Suite *
parse_suite (void)
{
  Suite *s = suite_create ("Parse Launch syntax");
  TCase *tc_chain = tcase_create ("parselaunch");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_launch_lines);
  tcase_add_test (tc_chain, test_launch_lines2);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = parse_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
