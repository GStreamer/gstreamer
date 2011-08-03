/* GStreamer unit test for the autodetect elements
 *
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>

GST_START_TEST (test_autovideosink_ghostpad_error_case)
{
  GstStateChangeReturn state_ret;
  GstElement *pipeline, *src, *filter, *sink;
  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", NULL);
  filter = gst_element_factory_make ("capsfilter", NULL);
  sink = gst_element_factory_make ("autovideosink", NULL);

  caps = gst_caps_new_simple ("video/x-raw-yuv", "format", GST_TYPE_FOURCC,
      GST_MAKE_FOURCC ('A', 'C', 'D', 'C'), NULL);

  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add_many (GST_BIN (pipeline), src, filter, sink, NULL);

  fail_unless (gst_element_link (src, filter), "Failed to link src to filter");
  fail_unless (gst_element_link (filter, sink),
      "Failed to link filter to sink");

  /* this should fail, there's no such format */
  state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (state_ret == GST_STATE_CHANGE_FAILURE,
      "pipeline _set_state() to PAUSED succeeded but should have failed");

  /* so, we hit an error and try to shut down the pipeline; this shouldn't
   * deadlock or block anywhere when autovideosink resets the ghostpad
   * targets etc. */
  state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS,
      "State change on pipeline failed");

  /* clean up */
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* disable this for now, too many valgrind suppressions needed for libasound */
#if 0
GST_START_TEST (test_autoaudiosink_ghostpad_error_case)
{
  GstStateChangeReturn state_ret;
  GstElement *pipeline, *src, *filter, *sink;
  GstCaps *caps;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", NULL);
  filter = gst_element_factory_make ("capsfilter", NULL);
  sink = gst_element_factory_make ("autoaudiosink", NULL);

  caps = gst_caps_new_simple ("audio/x-raw-int", "width", G_TYPE_INT, 42, NULL);

  g_object_set (filter, "caps", caps, NULL);
  gst_caps_unref (caps);

  gst_bin_add_many (GST_BIN (pipeline), src, filter, sink, NULL);

  fail_unless (gst_element_link (src, filter));
  fail_unless (gst_element_link (filter, sink));

  /* this should fail, there's no such width (hopefully) */
  state_ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  fail_unless (state_ret == GST_STATE_CHANGE_FAILURE,
      "pipeline _set_state() to PAUSED succeeded but should have failed");

  /* so, we hit an error and try to shut down the pipeline; this shouldn't
   * deadlock or block anywhere when autoaudiosink resets the ghostpad
   * targets etc. */
  state_ret = gst_element_set_state (pipeline, GST_STATE_NULL);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS);

  /* clean up */
  gst_object_unref (pipeline);
}

GST_END_TEST;
#endif

static Suite *
autodetect_suite (void)
{
  guint maj, min, mic, nano;

  Suite *s = suite_create ("autodetect");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  gst_version (&maj, &min, &mic, &nano);

  /* requires fixes from 0.10.10.1, but don't want to add a hard dependency
   * in configure.ac just for this yet */
  if (maj > 0 || min > 10 || mic > 10 || (mic == 10 && nano > 0)) {
    tcase_add_test (tc_chain, test_autovideosink_ghostpad_error_case);
    /* tcase_add_test (tc_chain, test_autoaudiosink_ghostpad_error_case); */
  }

  return s;
}

GST_CHECK_MAIN (autodetect);
