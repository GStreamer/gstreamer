/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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
#include <gst/app/gstappsink.h>
#include <gst/iosurface/gstiosurface.h>
#include <gst/video/video.h>
#include <TargetConditionals.h>

#include "iosurface-output-paths.h"

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

typedef enum
{
  AVFVIDEOSRC_MODE_CAMERA,
  AVFVIDEOSRC_MODE_SCREEN
} AvfVideoSrcMode;

typedef struct
{
  GstBuffer *buffer;
  GstCaps *caps;
  gboolean got_sample;
  gboolean error;
  gboolean timeout;
  gchar *error_message;
  GMainLoop *loop;
} PipelineResult;

static GstFlowReturn
on_new_sample (GstAppSink * appsink, gpointer user_data)
{
  PipelineResult *result = user_data;
  GstSample *sample = gst_app_sink_pull_sample (appsink);
  GstBuffer *buffer;
  GstCaps *caps;

  if (sample == NULL)
    return GST_FLOW_ERROR;

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  if (buffer != NULL && result->buffer == NULL)
    result->buffer = gst_buffer_ref (buffer);
  if (caps != NULL && result->caps == NULL)
    result->caps = gst_caps_ref (caps);
  result->got_sample = TRUE;

  gst_sample_unref (sample);
  g_main_loop_quit (result->loop);

  return GST_FLOW_OK;
}

static gboolean
on_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  PipelineResult *result = user_data;

  (void) bus;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (msg, &err, &debug);
      if (err != NULL && result->error_message == NULL && err->message != NULL)
        result->error_message = g_strdup (err->message);
      g_clear_error (&err);
      g_free (debug);
      result->error = TRUE;
      g_main_loop_quit (result->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (result->loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
on_timeout (gpointer user_data)
{
  PipelineResult *result = user_data;

  result->timeout = TRUE;
  g_main_loop_quit (result->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory != NULL) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static gboolean
running_in_ci (void)
{
  return g_getenv ("CI") != NULL;
}

static PipelineResult
run_pipeline (const gchar * pipeline_desc)
{
  PipelineResult result = { 0, };
  GstElement *pipeline = gst_parse_launch (pipeline_desc, NULL);
  GstElement *sink;
  GstBus *bus;
  GstStateChangeReturn ret;
  guint bus_watch_id;
  guint timeout_id;

  fail_unless (pipeline != NULL);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  result.loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (sink, "new-sample", G_CALLBACK (on_new_sample), &result);

  bus_watch_id = gst_bus_add_watch (bus, on_bus_message, &result);
  timeout_id = g_timeout_add_seconds (5, on_timeout, &result);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    result.error = TRUE;
    if (result.error_message == NULL)
      result.error_message = g_strdup ("failed to enter PLAYING");
  } else {
    g_main_loop_run (result.loop);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (!result.timeout)
    g_source_remove (timeout_id);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (result.loop);
  result.loop = NULL;

  gst_object_unref (bus);
  gst_object_unref (sink);
  gst_object_unref (pipeline);

  return result;
}

static void
pipeline_result_clear (PipelineResult * result)
{
  gst_clear_buffer (&result->buffer);
  gst_clear_caps (&result->caps);
  g_clear_pointer (&result->error_message, g_free);
}

static void
assert_iosurface_result (PipelineResult * result, const gchar * label)
{
  GstCapsFeatures *features;

  fail_unless (!result->timeout, "%s: timed out", label);
  fail_unless (!result->error, "%s: error: %s", label,
      GST_STR_NULL (result->error_message));
  fail_unless (result->got_sample, "%s: expected an output sample", label);
  fail_unless (result->caps != NULL, "%s: expected output caps", label);
  fail_unless (result->buffer != NULL, "%s: expected output buffer", label);

  features = gst_caps_get_features (result->caps, 0);
  fail_unless (features != NULL &&
      gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_IOSURFACE),
      "%s: expected IOSurface caps, got %" GST_PTR_FORMAT, label, result->caps);

  assert_iosurface_buffer_matches_caps (result->buffer, result->caps, label);
}

static gchar *
make_avfassetsrc_uri (void)
{
  gchar *filepath =
      g_build_filename (GST_TEST_FILES_PATH, "h264_aac.mp4", NULL);
  gchar *resolved_path;
  gchar *uri;

  if (!g_file_test (filepath, G_FILE_TEST_EXISTS)) {
    GST_INFO ("Skipping test, missing file: %s", filepath);
    g_free (filepath);
    return NULL;
  }

  resolved_path = g_canonicalize_filename (filepath, NULL);
  g_free (filepath);
  uri = gst_filename_to_uri (resolved_path, NULL);
  g_free (resolved_path);
  fail_unless (uri != NULL);

  return uri;
}

static gchar *
make_avfassetsrc_pipeline_desc (const gchar * caps)
{
  gchar *uri;
  gchar *pipeline_desc;

  if (!element_available ("avfassetsrc") || !element_available ("appsink")) {
    GST_INFO ("Skipping avfassetsrc test, required elements missing");
    return NULL;
  }

  uri = make_avfassetsrc_uri ();
  if (uri == NULL)
    return NULL;

  pipeline_desc = g_strdup_printf ("avfassetsrc name=src uri=%s "
      "src.video ! %s ! appsink name=sink emit-signals=true sync=false",
      uri, caps);
  g_free (uri);

  return pipeline_desc;
}

static gboolean
avfvideosrc_mode_available (AvfVideoSrcMode mode)
{
  GstElement *src = gst_element_factory_make ("avfvideosrc", NULL);
  GstStateChangeReturn ret;
  gboolean available;

  if (src == NULL)
    return FALSE;

  g_object_set (src, "capture-screen", mode == AVFVIDEOSRC_MODE_SCREEN, NULL);

  ret = gst_element_set_state (src, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_ASYNC)
    ret = gst_element_get_state (src, NULL, NULL, 2 * GST_SECOND);

  available = ret == GST_STATE_CHANGE_SUCCESS
      || ret == GST_STATE_CHANGE_NO_PREROLL;

  gst_element_set_state (src, GST_STATE_NULL);
  gst_object_unref (src);

  return available;
}

static gchar *
make_avfvideosrc_pipeline_desc (AvfVideoSrcMode mode)
{
  const gchar *mode_name =
      mode == AVFVIDEOSRC_MODE_SCREEN ? "screen" : "camera";

  if (!element_available ("avfvideosrc") || !element_available ("appsink")) {
    GST_INFO ("Skipping avfvideosrc %s test, required elements missing",
        mode_name);
    return NULL;
  }

  if (!avfvideosrc_mode_available (mode)) {
    GST_INFO ("Skipping avfvideosrc %s test, no default source available",
        mode_name);
    return NULL;
  }

  return g_strdup_printf ("avfvideosrc name=src %snum-buffers=1 "
      "! video/x-raw(memory:IOSurface),framerate=30/1 "
      "! appsink name=sink emit-signals=true sync=false async=false",
      mode == AVFVIDEOSRC_MODE_SCREEN ? "capture-screen=true " : "");
}

GST_START_TEST (test_avfassetsrc_iosurface_memory_output)
{
  PipelineResult result;
  gchar *pipeline_desc = make_avfassetsrc_pipeline_desc
      ("video/x-raw(memory:IOSurface),format=NV12");
  if (pipeline_desc == NULL)
    return;

  result = run_pipeline (pipeline_desc);
  assert_iosurface_result (&result, "avfassetsrc IOSurface");

  pipeline_result_clear (&result);
  g_free (pipeline_desc);
}

GST_END_TEST;

static void
run_avfvideosrc_iosurface_test (AvfVideoSrcMode mode, const gchar * label)
{
  PipelineResult result;
  gchar *pipeline_desc = make_avfvideosrc_pipeline_desc (mode);
  if (pipeline_desc == NULL)
    return;

  result = run_pipeline (pipeline_desc);
  assert_iosurface_result (&result, label);

  pipeline_result_clear (&result);
  g_free (pipeline_desc);
}

GST_START_TEST (test_avfvideosrc_camera_iosurface_memory_output)
{
  run_avfvideosrc_iosurface_test (AVFVIDEOSRC_MODE_CAMERA,
      "avfvideosrc camera IOSurface");
}

GST_END_TEST;

GST_START_TEST (test_avfvideosrc_screen_iosurface_memory_output)
{
#if !TARGET_OS_OSX
  GST_INFO
      ("Skipping avfvideosrc screen test, screen capture path is macOS only");
  return;
#endif

  run_avfvideosrc_iosurface_test (AVFVIDEOSRC_MODE_SCREEN,
      "avfvideosrc screen IOSurface");
}

GST_END_TEST;

static Suite *
avfsources_output_paths_suite (void)
{
  Suite *s = suite_create ("avfsources-output-paths");
  TCase *tc_asset = tcase_create ("avfassetsrc");
  TCase *tc_video = tcase_create ("avfvideosrc");

  suite_add_tcase (s, tc_asset);
  tcase_set_timeout (tc_asset, 20);
  tcase_add_test (tc_asset, test_avfassetsrc_iosurface_memory_output);

  suite_add_tcase (s, tc_video);
  if (!running_in_ci ()) {
    tcase_set_timeout (tc_video, 20);
    tcase_add_test (tc_video, test_avfvideosrc_camera_iosurface_memory_output);
    tcase_add_test (tc_video, test_avfvideosrc_screen_iosurface_memory_output);
  }

  return s;
}

static int
run_tests (void)
{
  Suite *s = avfsources_output_paths_suite ();

  return gst_check_run_suite_nofork (s, "avfsources_output_paths", __FILE__);
}

int
main (int argc, char **argv)
{
  gst_check_init (&argc, &argv);
#if TARGET_OS_OSX
  return gst_macos_main_simple ((GstMainFuncSimple) run_tests, NULL);
#else
  return run_tests ();
#endif
}
