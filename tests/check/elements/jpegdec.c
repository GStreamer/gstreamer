/* GStreamer
 *
 * unit test for jpegenc
 *
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

#include <unistd.h>

#include <gio/gio.h>
#include <gst/check/gstcheck.h>
#include <gst/app/gstappsink.h>
#include <gst/pbutils/gstdiscoverer.h>

/* Verify jpegdec is working when explictly requested by a pipeline. */
GST_START_TEST (test_jpegdec_explicit)
{
  GError *error = NULL;
  GstElement *pipeline;
  GstElement *source;
  GstElement *sink;
  GstSample *sample;

  /* construct a pipeline that explicitly uses jpegdec */
  pipeline = gst_parse_launch
      ("filesrc name=source ! jpegdec ! appsink name=sink", &error);
  fail_unless (GST_IS_PIPELINE (pipeline));
  fail_unless (error == NULL, "%s", (error ? error->message : ""));

  source = gst_bin_get_by_name (GST_BIN (pipeline), "source");
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  fail_unless (GST_IS_ELEMENT (source));
  fail_unless (GST_IS_APP_SINK (sink));

  /* point that pipeline to our test image */
  {
    char *filename = g_build_filename (GST_TEST_FILES_PATH, "image.jpg", NULL);
    g_object_set (G_OBJECT (source), "location", filename, NULL);
    g_free (filename);
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));

  fail_unless (GST_IS_SAMPLE (sample));
  fail_unless (gst_app_sink_is_eos (GST_APP_SINK (sink)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* do some basic checks to verify image decoding */
  {
    GstCaps *decoded;
    GstCaps *expected;

    decoded = gst_sample_get_caps (sample);
    expected = gst_caps_from_string ("video/x-raw, width=120, height=160");

    fail_unless (gst_caps_is_always_compatible (decoded, expected));

    gst_caps_unref (expected);
  }

  gst_sample_unref (sample);
  gst_object_unref (sink);
  gst_object_unref (source);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* Verify JPEG discovery is working. Right now jpegdec would be used,
 * but I have no idea how to actually verify this. */
GST_START_TEST (test_jpegdec_discover)
{
  GstDiscoverer *disco;
  GError *error = NULL;
  char *uri;
  GstDiscovererInfo *info;
  GstDiscovererVideoInfo *video;

  disco = gst_discoverer_new (5 * GST_SECOND, &error);

  fail_unless (GST_IS_DISCOVERER (disco));
  fail_unless (error == NULL, "%s", (error ? error->message : ""));

  {
    GFile *testdir = g_file_new_for_path (GST_TEST_FILES_PATH);
    GFile *testfile = g_file_resolve_relative_path (testdir, "image.jpg");
    uri = g_file_get_uri (testfile);
    g_object_unref (testfile);
    g_object_unref (testdir);
  }

  info = gst_discoverer_discover_uri (disco, uri, &error);
  fail_unless (GST_IS_DISCOVERER_INFO (info));
  fail_unless (error == NULL, "%s: %s", uri, (error ? error->message : ""));

  fail_unless_equals_string (gst_discoverer_info_get_uri (info), uri);
  fail_unless_equals_int (gst_discoverer_info_get_result (info),
      GST_DISCOVERER_OK);

  video =
      GST_DISCOVERER_VIDEO_INFO (gst_discoverer_info_get_stream_info (info));
  fail_unless (video != NULL);

  fail_unless (gst_discoverer_video_info_is_image (video));
  fail_unless_equals_int (gst_discoverer_video_info_get_width (video), 120);
  fail_unless_equals_int (gst_discoverer_video_info_get_height (video), 160);

  gst_discoverer_info_unref (info);
  g_free (uri);
  g_object_unref (disco);
}

GST_END_TEST;

static Suite *
jpegdec_suite (void)
{
  Suite *s = suite_create ("jpegdec");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_jpegdec_explicit);
  tcase_add_test (tc_chain, test_jpegdec_discover);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = jpegdec_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
