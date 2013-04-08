/* GStreamer WavParse unit tests
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
#include <gst/check/gstcheck.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <unistd.h>

GST_START_TEST (test_empty_file)
{
  GstElement *pipeline;
  GstElement *filesrc;
  GstElement *wavparse;
  GstElement *fakesink;

  pipeline = gst_pipeline_new ("testpipe");
  filesrc = gst_element_factory_make ("filesrc", NULL);
  fail_if (filesrc == NULL);
  wavparse = gst_element_factory_make ("wavparse", NULL);
  fail_if (wavparse == NULL);
  fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_if (fakesink == NULL);

  gst_object_ref_sink (filesrc);
  gst_object_ref_sink (wavparse);
  gst_object_ref_sink (fakesink);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, wavparse, fakesink, NULL);
  g_object_set (filesrc, "location", "/dev/null", NULL);

  fail_unless (gst_element_link_many (filesrc, wavparse, fakesink, NULL));

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (filesrc);
  gst_object_unref (wavparse);
  gst_object_unref (fakesink);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
wavparse_suite (void)
{
  Suite *s = suite_create ("wavparse");
  TCase *tc_chain = tcase_create ("wavparse");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_empty_file);
  return s;
}

GST_CHECK_MAIN (wavparse)
