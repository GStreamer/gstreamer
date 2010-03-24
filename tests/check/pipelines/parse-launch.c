/* GStreamer gst_parse_launch unit tests
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2008> Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef HAVE_VALGRIND_H
# include <valgrind/valgrind.h>
# include <valgrind/memcheck.h>
#endif

#include <gst/check/gstcheck.h>

#define GST_TYPE_PARSE_TEST_ELEMENT (gst_parse_test_element_get_type())
static GType gst_parse_test_element_get_type (void);

static GstElement *
setup_pipeline (const gchar * pipe_descr)
{
  GstElement *pipeline;
  GError *error = NULL;

  pipeline = gst_parse_launch (pipe_descr, &error);

  GST_DEBUG ("created %s", pipe_descr);

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
  fail_unless (pipeline == NULL || error != NULL,
      "Expected failure pipeline %s: succeeded!", pipe_descr);
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
  "filesrc location=music.mp3 ! identity silent=true ! fakesink silent=true",
  "filesrc location=music.ogg ! tee ! identity silent=true ! identity silent=true ! fakesink silent=true",
  "filesrc location=http://domain.com/music.mp3 ! identity silent=true ! fakesink silent=true",
  "filesrc location=movie.avi ! tee name=demuxer ! ( queue ! identity silent=true ! fakesink silent=true ) ( demuxer. ! queue ! identity silent=true ! fakesink silent=true )",
  "fakesrc ! video/x-raw-yuv ! fakesink silent=true",
  "fakesrc !   video/raw,  format=(fourcc)YUY2; video/raw, format=(fourcc)YV12 ! fakesink silent=true",
  "fakesrc ! audio/x-raw-int, width=[16,  32], depth={16, 24, 32}, signed=TRUE ! fakesink silent=true",
  "fakesrc ! identity silent=true ! identity silent=true ! identity silent=true ! fakesink silent=true",
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
#define PIPELINE3  "fakesrc identity silent=true fakesink silent=true"
#define PIPELINE4  "fakesrc num-buffers=4 .src ! identity silent=true !.sink identity silent=true .src ! .sink fakesink silent=true"
#define PIPELINE5  "fakesrc num-buffers=4 name=src identity silent=true name=id1 identity silent=true name = id2 fakesink silent=true name =sink src. ! id1. id1.! id2.sink id2.src!sink.sink"
#define PIPELINE6  "pipeline.(name=\"john\" fakesrc num-buffers=4 ( bin. ( ! queue ! identity silent=true !( queue ! fakesink silent=true )) ))"
#define PIPELINE7  "fakesrc num-buffers=4 ! tee name=tee .src%d! queue ! fakesink silent=true tee.src%d ! queue ! fakesink silent=true queue name =\"foo\" ! fakesink silent=true tee.src%d ! foo."
/* aggregator is borked
 * #define PIPELINE8  "fakesrc num-buffers=4 ! tee name=tee1 .src0,src1 ! .sink0, sink1 aggregator ! fakesink silent=true"
 * */
#define PIPELINE8  "fakesrc num-buffers=4 ! fakesink silent=true"
#define PIPELINE9  "fakesrc num-buffers=4 ! test. fakesink silent=true name=test"
#define PIPELINE10 "( fakesrc num-buffers=\"4\" ! ) identity silent=true ! fakesink silent=true"
#define PIPELINE11 "fakesink silent=true name = sink identity silent=true name=id ( fakesrc num-buffers=\"4\" ! id. ) id. ! sink."
#define PIPELINE12 "file:///tmp/test.file ! fakesink silent=true"
#define PIPELINE13 "fakesrc ! file:///tmp/test.file"

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
   * - URI detection works
   */
  cur = setup_pipeline (PIPELINE12);
  gst_object_unref (cur);

  /** * checks:
   * - URI sink detection works
   */
  cur = setup_pipeline (PIPELINE13);
  gst_object_unref (cur);

  /* Checks handling of a assignment followed by error inside a bin. 
   * This should warn, but ignore the error and carry on */
  cur = setup_pipeline ("( filesrc blocksize=4 location=/dev/null @ )");
  gst_object_unref (cur);
}

GST_END_TEST;

static const gchar *expected_failures[] = {
  /* checks: fails because a=b. is not a valid element reference in parse.l */
  "fakesrc num-buffers=4 name=\"a=b\"  a=b. ! fakesink silent=true",
  /* checks: Error branch for a non-deserialisable property value */
  "filesrc blocksize=absdff",
  /* checks: That broken caps which don't parse can't create a pipeline */
  "fakesrc ! video/raw,format=(antwerp)monkeys ! fakesink silent=true",
  /* checks: Empty pipeline is invalid */
  "",
  /* checks: Link without sink element failes */
  "fakesrc ! ",
  /* checks: Link without src element failes */
  " ! fakesink silent=true",
  /* checks: Source URI for which no element exists is a failure */
  "borky://fdaffd ! fakesink silent=true",
  /* checks: Sink URI for which no element exists is a failure */
  "fakesrc ! borky://fdaffd",
  /* checks: Referencing non-existent source element by name can't link */
  "fakesrc name=src fakesink silent=true name=sink noexiste. ! sink.",
  /* checks: Referencing non-existent sink element by name can't link */
  "fakesrc name=src fakesink silent=true name=sink src. ! noexiste.",
  /* checks: Can't link 2 elements that only have sink pads */
  "fakesink silent=true ! fakesink silent=true",
  /* checks multi-chain link without src element fails. */
  "! identity silent=true ! identity silent=true ! fakesink silent=true",
  /* Empty bin not allowed */
  "bin.( )",
  /* bin with non-existent element counts as empty, and not allowed */
  "bin.( non_existent_element )",
  /* END: */
  NULL
};

GST_START_TEST (expected_to_fail_pipes)
{
  const gchar **s;

  for (s = expected_failures; *s != NULL; s++) {
    expected_fail_pipe (*s);
  }
}

GST_END_TEST;

static const gchar *leaking_failures[] = {
  /* checks: Invalid pipeline syntax fails */
  "fakesrc ! identity silent=true ! sgsdfagfd @ gfdgfdsgfsgSF",
  /* checks: Attempting to link to a non-existent pad on an element 
   * created via URI handler should fail */
  "fakesrc ! .foo file:///dev/null",
  /* checks: That requesting an element which doesn't exist doesn't work */
  "error-does-not-exist-src",
  NULL
};

GST_START_TEST (leaking_fail_pipes)
{
  const gchar **s;

  for (s = leaking_failures; *s != NULL; s++) {
    /* Uncomment if you want to try fixing the leaks */
#if 0
    g_print ("Trying pipe: %s\n", *s);
    expected_fail_pipe (*s);
#endif
#ifdef HAVE_VALGRIND_H
    VALGRIND_DO_LEAK_CHECK;
#endif
  }
}

GST_END_TEST;

/* Helper function to test delayed linking support in parse_launch by creating
 * a test element based on bin, which contains a fakesrc and a sometimes 
 * pad-template, and trying to link to a fakesink. When the bin transitions
 * to paused it adds a pad, which should get linked to the fakesink */
static void
run_delayed_test (const gchar * pipe_str, const gchar * peer,
    gboolean expect_link)
{
  GstElement *pipe, *src, *sink;
  GstPad *srcpad, *sinkpad, *peerpad = NULL;

  pipe = setup_pipeline (pipe_str);

  src = gst_bin_get_by_name (GST_BIN (pipe), "src");
  fail_if (src == NULL, "Test source element was not created");

  sink = gst_bin_get_by_name (GST_BIN (pipe), "sink");
  fail_if (sink == NULL, "Test sink element was not created");

  /* The src should not yet have a src pad */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad == NULL, "Source element already has a source pad");

  /* Set the state to PAUSED and wait until the src at least reaches that
   * state */
  fail_if (gst_element_set_state (pipe, GST_STATE_PAUSED) ==
      GST_STATE_CHANGE_FAILURE);

  fail_if (gst_element_get_state (src, NULL, NULL, GST_CLOCK_TIME_NONE) ==
      GST_STATE_CHANGE_FAILURE);

  /* Now, the source element should have a src pad, and if "peer" was passed, 
   * then the src pad should have gotten linked to the 'sink' pad of that 
   * peer */
  srcpad = gst_element_get_static_pad (src, "src");
  fail_if (srcpad == NULL, "Source element did not create source pad");

  peerpad = gst_pad_get_peer (srcpad);

  if (expect_link == TRUE) {
    fail_if (peerpad == NULL, "Source element pad did not get linked");
  } else {
    fail_if (peerpad != NULL,
        "Source element pad got linked but should not have");
  }
  if (peerpad != NULL)
    gst_object_unref (peerpad);

  if (peer != NULL) {
    GstElement *peer_elem = gst_bin_get_by_name (GST_BIN (pipe), peer);

    fail_if (peer_elem == NULL, "Could not retrieve peer %s", peer);

    sinkpad = gst_element_get_static_pad (peer_elem, "sink");
    fail_if (sinkpad == NULL, "Peer element did not have a 'sink' pad");

    fail_unless (peerpad == sinkpad,
        "Source src pad got connected to the wrong peer");
    gst_object_unref (sinkpad);
  }

  gst_object_unref (srcpad);

  gst_object_unref (src);
  gst_object_unref (sink);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
}

GST_START_TEST (delayed_link)
{
  fail_unless (gst_element_register (NULL, "parsetestelement",
          GST_RANK_NONE, GST_TYPE_PARSE_TEST_ELEMENT));

  /* This tests the delayed linking support in parse_launch by creating
   * a test element based on bin, which contains a fakesrc and a sometimes 
   * pad-template, and trying to link to a fakesink. When the bin transitions
   * to paused it adds a pad, which should get linked to the fakesink */
  run_delayed_test
      ("parsetestelement name=src ! fakesink silent=true name=sink", "sink",
      TRUE);

  /* Test, but this time specifying both pad names */
  run_delayed_test ("parsetestelement name=src .src ! "
      ".sink fakesink silent=true name=sink", "sink", TRUE);

  /* Now try with a caps filter, but not testing that
   * the peerpad == sinkpad, because the peer will actually
   * be a capsfilter */
  run_delayed_test ("parsetestelement name=src ! application/x-test-caps ! "
      "fakesink silent=true name=sink", NULL, TRUE);

  /* Now try with mutually exclusive caps filters that 
   * will prevent linking, but only once gets around to happening -
   * ie, the pipeline should create ok but fail to change state */
  run_delayed_test ("parsetestelement name=src ! application/x-test-caps ! "
      "identity silent=true ! application/x-other-caps ! "
      "fakesink silent=true name=sink silent=true", NULL, FALSE);
}

GST_END_TEST;

typedef struct _GstParseTestElement
{
  GstBin parent;

  GstElement *fakesrc;
} GstParseTestElement;

typedef struct _GstParseTestElementClass
{
  GstBinClass parent;
} GstParseTestElementClass;

static GstStaticPadTemplate test_element_pad_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC,
    GST_PAD_SOMETIMES, GST_STATIC_CAPS ("application/x-test-caps"));

GST_BOILERPLATE (GstParseTestElement, gst_parse_test_element, GstBin,
    GST_TYPE_BIN);

static GstStateChangeReturn
gst_parse_test_element_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_parse_test_element_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Test element for parse launch tests", "Source",
      "Test element for parse launch tests in core",
      "GStreamer Devel <gstreamer-devel@lists.sf.net>");
}

static void
gst_parse_test_element_class_init (GstParseTestElementClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&test_element_pad_template));

  gstelement_class->change_state = gst_parse_test_element_change_state;
}

static void
gst_parse_test_element_init (GstParseTestElement * src,
    GstParseTestElementClass * klass)
{
  /* Create a fakesrc and add it to ourselves */
  src->fakesrc = gst_element_factory_make ("fakesrc", NULL);
  if (src->fakesrc)
    gst_bin_add (GST_BIN (src), src->fakesrc);
}

static GstStateChangeReturn
gst_parse_test_element_change_state (GstElement * element,
    GstStateChange transition)
{
  GstParseTestElement *src = (GstParseTestElement *) element;

  if (transition == GST_STATE_CHANGE_READY_TO_PAUSED) {
    /* Add our pad */
    GstPad *pad;
    GstPad *ghost;

    if (src->fakesrc == NULL)
      return GST_STATE_CHANGE_FAILURE;

    pad = gst_element_get_static_pad (src->fakesrc, "src");
    if (pad == NULL)
      return GST_STATE_CHANGE_FAILURE;

    ghost = gst_ghost_pad_new ("src", pad);
    fail_if (ghost == NULL, "Failed to create ghost pad");
    /* activate and add */
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (GST_ELEMENT (src), ghost);
    gst_object_unref (pad);
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

GST_START_TEST (test_missing_elements)
{
  GstParseContext *ctx;
  GstElement *element;
  GError *err = NULL;
  gchar **arr;

  /* avoid misleading 'no such element' error debug messages when using cvs */
  if (!g_getenv ("GST_DEBUG"))
    gst_debug_set_default_threshold (GST_LEVEL_NONE);

  /* one missing element */
  ctx = gst_parse_context_new ();
  element = gst_parse_launch_full ("fakesrc ! coffeesink", ctx,
      GST_PARSE_FLAG_FATAL_ERRORS, &err);
  fail_unless (err != NULL, "expected error");
  fail_unless_equals_int (err->code, GST_PARSE_ERROR_NO_SUCH_ELEMENT);
  fail_unless (element == NULL, "expected NULL return with FATAL_ERRORS");
  arr = gst_parse_context_get_missing_elements (ctx);
  fail_unless (arr != NULL, "expected missing elements");
  fail_unless_equals_string (arr[0], "coffeesink");
  fail_unless (arr[1] == NULL);
  g_strfreev (arr);
  gst_parse_context_free (ctx);
  g_error_free (err);
  err = NULL;

  /* multiple missing elements */
  ctx = gst_parse_context_new ();
  element = gst_parse_launch_full ("fakesrc ! bogusenc ! identity ! goomux ! "
      "fakesink", ctx, GST_PARSE_FLAG_FATAL_ERRORS, &err);
  fail_unless (err != NULL, "expected error");
  fail_unless_equals_int (err->code, GST_PARSE_ERROR_NO_SUCH_ELEMENT);
  fail_unless (element == NULL, "expected NULL return with FATAL_ERRORS");
  arr = gst_parse_context_get_missing_elements (ctx);
  fail_unless (arr != NULL, "expected missing elements");
  fail_unless_equals_string (arr[0], "bogusenc");
  fail_unless_equals_string (arr[1], "goomux");
  fail_unless (arr[2] == NULL);
  g_strfreev (arr);
  gst_parse_context_free (ctx);
  g_error_free (err);
  err = NULL;

  /* multiple missing elements, different link pattern */
  ctx = gst_parse_context_new ();
  element = gst_parse_launch_full ("fakesrc ! bogusenc ! mux.sink "
      "blahsrc ! goomux name=mux ! fakesink   fakesrc ! goosink", ctx,
      GST_PARSE_FLAG_FATAL_ERRORS, &err);
  fail_unless (err != NULL, "expected error");
  fail_unless_equals_int (err->code, GST_PARSE_ERROR_NO_SUCH_ELEMENT);
  fail_unless (element == NULL, "expected NULL return with FATAL_ERRORS");
  arr = gst_parse_context_get_missing_elements (ctx);
  fail_unless (arr != NULL, "expected missing elements");
  fail_unless_equals_string (arr[0], "bogusenc");
  fail_unless_equals_string (arr[1], "blahsrc");
  fail_unless_equals_string (arr[2], "goomux");
  fail_unless_equals_string (arr[3], "goosink");
  fail_unless (arr[4] == NULL);
  g_strfreev (arr);
  gst_parse_context_free (ctx);
  g_error_free (err);
  err = NULL;
}

GST_END_TEST;

GST_START_TEST (test_flags)
{
  GstElement *element;
  GError *err = NULL;

  /* avoid misleading 'no such element' error debug messages when using cvs */
  if (!g_getenv ("GST_DEBUG"))
    gst_debug_set_default_threshold (GST_LEVEL_NONE);

  /* default behaviour is to return any already constructed bins/elements */
  element = gst_parse_launch_full ("fakesrc ! coffeesink", NULL, 0, &err);
  fail_unless (err != NULL, "expected error");
  fail_unless_equals_int (err->code, GST_PARSE_ERROR_NO_SUCH_ELEMENT);
  fail_unless (element != NULL, "expected partial pipeline/element");
  g_error_free (err);
  err = NULL;
  gst_object_unref (element);

  /* test GST_PARSE_FLAG_FATAL_ERRORS */
  element = gst_parse_launch_full ("fakesrc ! coffeesink", NULL,
      GST_PARSE_FLAG_FATAL_ERRORS, &err);
  fail_unless (err != NULL, "expected error");
  fail_unless_equals_int (err->code, GST_PARSE_ERROR_NO_SUCH_ELEMENT);
  fail_unless (element == NULL, "expected NULL return with FATAL_ERRORS");
  g_error_free (err);
  err = NULL;
}

GST_END_TEST;

static Suite *
parse_suite (void)
{
  Suite *s = suite_create ("Parse Launch syntax");
  TCase *tc_chain = tcase_create ("parselaunch");

  /* time out after 20s, not the default 3 */
  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_launch_lines);
  tcase_add_test (tc_chain, test_launch_lines2);
  tcase_add_test (tc_chain, expected_to_fail_pipes);
  tcase_add_test (tc_chain, leaking_fail_pipes);
  tcase_add_test (tc_chain, delayed_link);
  tcase_add_test (tc_chain, test_flags);
  tcase_add_test (tc_chain, test_missing_elements);
  return s;
}

GST_CHECK_MAIN (parse);
