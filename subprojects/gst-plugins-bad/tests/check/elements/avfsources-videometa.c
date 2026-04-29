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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/video/video.h>
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

typedef struct
{
  GstBuffer *buffer;
  GstCaps *caps;
  gboolean got_buffer;
  gboolean skipped;
  gboolean error;
  gboolean timeout;
  gchar *error_message;
  gchar *error_debug;
  GMainLoop *loop;
} PipelineObservation;

typedef enum
{
  AVFVIDEOSRC_MODE_CAMERA,
  AVFVIDEOSRC_MODE_SCREEN
} AvfVideoSrcMode;

typedef struct
{
  gboolean request_video_meta;
  gboolean got_first_buffer;
  gboolean first_buffer_ok;
  gboolean got_second_buffer;
  gboolean second_buffer_ok;
  guint allocation_queries;
  guint allocation_queries_after_request;
  gboolean saw_requested_allocation;
  gboolean skipped;
  gboolean error;
  gboolean timeout;
  gchar *error_message;
  gchar *error_debug;
  GMainLoop *loop;
} ReconfigureObservation;

static void
on_handoff (GstElement * sink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  PipelineObservation *obs = user_data;

  (void) sink;

  if (!obs->got_buffer) {
    obs->buffer = gst_buffer_ref (buffer);
    obs->caps = gst_pad_get_current_caps (pad);
  }
  obs->got_buffer = TRUE;
  g_main_loop_quit (obs->loop);
}

static gboolean
on_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  PipelineObservation *obs = user_data;

  (void) bus;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (msg, &err, &debug);
      if (err != NULL && obs->error_message == NULL && err->message != NULL)
        obs->error_message = g_strdup (err->message);
      if (debug != NULL && obs->error_debug == NULL)
        obs->error_debug = g_strdup (debug);
      g_clear_error (&err);
      g_free (debug);
      obs->error = TRUE;
      g_main_loop_quit (obs->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (obs->loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static gboolean
on_timeout (gpointer user_data)
{
  PipelineObservation *obs = user_data;

  obs->timeout = TRUE;
  g_main_loop_quit (obs->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
on_reconfigure_timeout (gpointer user_data)
{
  ReconfigureObservation *obs = user_data;

  obs->timeout = TRUE;
  g_main_loop_quit (obs->loop);
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

static gboolean
on_reconfigure_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  ReconfigureObservation *obs = user_data;

  (void) bus;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *debug = NULL;

      gst_message_parse_error (msg, &err, &debug);
      if (err != NULL && obs->error_message == NULL && err->message != NULL)
        obs->error_message = g_strdup (err->message);
      if (debug != NULL && obs->error_debug == NULL)
        obs->error_debug = g_strdup (debug);
      g_clear_error (&err);
      g_free (debug);
      obs->error = TRUE;
      g_main_loop_quit (obs->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (obs->loop);
      break;
    default:
      break;
  }

  return TRUE;
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

static PipelineObservation
run_pipeline (const gchar * pipeline_desc)
{
  PipelineObservation obs = { 0, };
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

  obs.loop = g_main_loop_new (NULL, FALSE);
  g_signal_connect (sink, "handoff", G_CALLBACK (on_handoff), &obs);

  bus_watch_id = gst_bus_add_watch (bus, on_bus_message, &obs);
  /* Live AVFoundation sources can stall waiting for device access or
   * permissions, so each pipeline run needs a bounded local timeout. */
  timeout_id = g_timeout_add_seconds (5, on_timeout, &obs);

  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    obs.error = TRUE;
    if (obs.error_message == NULL)
      obs.error_message = g_strdup ("failed to enter PLAYING");
  } else {
    g_main_loop_run (obs.loop);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (!obs.timeout)
    g_source_remove (timeout_id);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (obs.loop);
  obs.loop = NULL;

  gst_object_unref (bus);
  gst_object_unref (sink);
  gst_object_unref (pipeline);

  return obs;
}

static gboolean
buffer_has_video_meta (GstBuffer * buffer)
{
  return gst_buffer_get_video_meta (buffer) != NULL;
}

static gboolean
buffer_video_meta_has_no_padding (GstBuffer * buffer, GstCaps * caps)
{
  GstVideoMeta *meta = gst_buffer_get_video_meta (buffer);
  GstVideoInfo info;

  if (meta == NULL)
    return TRUE;

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (meta->n_planes != GST_VIDEO_INFO_N_PLANES (&info))
    return FALSE;

  for (guint i = 0; i < meta->n_planes; i++) {
    if (meta->stride[i] != GST_VIDEO_INFO_PLANE_STRIDE (&info, i))
      return FALSE;
    if (meta->offset[i] != GST_VIDEO_INFO_PLANE_OFFSET (&info, i))
      return FALSE;
  }

  return TRUE;
}

static gboolean
reconfigure_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  ReconfigureObservation *obs =
      g_object_get_data (G_OBJECT (pad), "reconfigure-observation");
  gboolean ret;

  if (GST_QUERY_TYPE (query) == GST_QUERY_ALLOCATION) {
    if (obs != NULL) {
      obs->allocation_queries++;

      if (obs->request_video_meta) {
        obs->allocation_queries_after_request++;
        obs->saw_requested_allocation = TRUE;
        gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
      }
    }

    gst_pad_query_default (pad, parent, query);
    return TRUE;
  }

  ret = gst_pad_query_default (pad, parent, query);
  return ret;
}

static GstPadProbeReturn
reconfigure_sink_probe (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  ReconfigureObservation *obs = user_data;
  GstBuffer *buffer = gst_pad_probe_info_get_buffer (info);
  GstCaps *caps;

  if (buffer == NULL)
    return GST_PAD_PROBE_OK;

  caps = gst_pad_get_current_caps (pad);
  fail_unless (caps != NULL);

  if (!obs->got_first_buffer) {
    obs->first_buffer_ok = buffer_video_meta_has_no_padding (buffer, caps);
    obs->got_first_buffer = TRUE;
    obs->request_video_meta = TRUE;
    fail_unless (gst_pad_push_event (pad, gst_event_new_reconfigure ()));
  } else if (!obs->got_second_buffer && obs->saw_requested_allocation) {
    obs->second_buffer_ok = buffer_has_video_meta (buffer);
    obs->got_second_buffer = TRUE;
    g_main_loop_quit (obs->loop);
  }

  gst_caps_unref (caps);

  return GST_PAD_PROBE_OK;
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
make_avfassetsrc_pipeline_desc (gboolean request_video_meta)
{
  gchar *uri;
  gchar *desc;

  if (!element_available ("avfassetsrc") || !element_available ("fakesink")) {
    GST_INFO ("Skipping avfassetsrc test, required elements missing");
    return NULL;
  }

  if (request_video_meta && !element_available ("fakevideosink")) {
    GST_INFO
        ("Skipping avfassetsrc requested-meta test, fakevideosink missing");
    return NULL;
  }

  uri = make_avfassetsrc_uri ();
  if (uri == NULL)
    return NULL;

  /* fakevideosink advertises GstVideoMeta support while fakesink does not, so
   * switching between them lets us verify that avfassetsrc honors the request. */
  desc = g_strdup_printf ("avfassetsrc name=src uri=%s "
      "src.video ! %s name=sink signal-handoffs=true sync=false async=false",
      uri, request_video_meta ? "fakevideosink" : "fakesink");
  g_free (uri);

  return desc;
}

static ReconfigureObservation
run_avfassetsrc_reconfigure_pipeline (void)
{
  ReconfigureObservation obs = { 0, };
  gchar *uri;
  gchar *pipeline_desc;
  GstElement *pipeline;
  GstElement *sink;
  GstPad *sinkpad;
  GstBus *bus;
  guint bus_watch_id;
  guint timeout_id;

  uri = make_avfassetsrc_uri ();
  if (uri == NULL) {
    obs.skipped = TRUE;
    return obs;
  }

  pipeline_desc = g_strdup_printf ("avfassetsrc name=src uri=%s "
      "src.video ! fakesink name=sink sync=false async=false", uri);
  g_free (uri);

  pipeline = gst_parse_launch (pipeline_desc, NULL);
  g_free (pipeline_desc);
  fail_unless (pipeline != NULL);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (sinkpad != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  obs.loop = g_main_loop_new (NULL, FALSE);

  g_object_set_data (G_OBJECT (sinkpad), "reconfigure-observation", &obs);
  gst_pad_set_query_function (sinkpad, reconfigure_sink_query);
  gst_pad_add_probe (sinkpad, GST_PAD_PROBE_TYPE_BUFFER, reconfigure_sink_probe,
      &obs, NULL);

  bus_watch_id = gst_bus_add_watch (bus, on_reconfigure_bus_message, &obs);
  timeout_id = g_timeout_add_seconds (5, on_reconfigure_timeout, &obs);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  g_main_loop_run (obs.loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (!obs.timeout)
    g_source_remove (timeout_id);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (obs.loop);
  obs.loop = NULL;

  gst_object_unref (bus);
  gst_object_unref (sinkpad);
  gst_object_unref (sink);
  gst_object_unref (pipeline);

  return obs;
}

static gchar *
make_avfvideosrc_pipeline_desc (AvfVideoSrcMode mode,
    gboolean request_video_meta)
{
  const gchar *mode_name =
      mode == AVFVIDEOSRC_MODE_SCREEN ? "screen" : "camera";

  if (!element_available ("avfvideosrc")) {
    GST_INFO ("Skipping avfvideosrc %s test, required elements missing",
        mode_name);
    return NULL;
  }

  if (request_video_meta && !element_available ("fakevideosink")) {
    GST_INFO
        ("Skipping avfvideosrc %s requested-meta test, fakevideosink missing",
        mode_name);
    return NULL;
  }

  if (!avfvideosrc_mode_available (mode)) {
    GST_INFO ("Skipping avfvideosrc %s test, no default source available",
        mode_name);
    return NULL;
  }

  /* fakevideosink requests GstVideoMeta and fakesink does not, which gives us
   * a simple way to check that avfvideosrc follows downstream negotiation. */
  return
      g_strdup_printf ("avfvideosrc name=src %snum-buffers=1 "
      "! video/x-raw,framerate=30/1 "
      "! %s name=sink signal-handoffs=true sync=false async=false",
      mode == AVFVIDEOSRC_MODE_SCREEN ? "capture-screen=true " : "",
      request_video_meta ? "fakevideosink" : "fakesink");
}

static void
observation_clear (PipelineObservation * obs)
{
  if (obs->buffer != NULL) {
    gst_buffer_unref (obs->buffer);
    obs->buffer = NULL;
  }
  if (obs->caps != NULL) {
    gst_caps_unref (obs->caps);
    obs->caps = NULL;
  }
  g_clear_pointer (&obs->error_message, g_free);
  g_clear_pointer (&obs->error_debug, g_free);
}

static void
assert_successful_observation (const PipelineObservation * obs,
    const gchar * label)
{
  fail_unless (!obs->timeout, "%s: timed out", label);
  fail_unless (!obs->error, "%s: error: %s debug: %s", label,
      GST_STR_NULL (obs->error_message), GST_STR_NULL (obs->error_debug));
  fail_unless (obs->got_buffer, "%s: expected at least one buffer", label);
}

static void
assert_requested_video_meta (const PipelineObservation * obs,
    const gchar * label)
{
  assert_successful_observation (obs, label);
  fail_unless (buffer_has_video_meta (obs->buffer),
      "%s: expected GstVideoMeta", label);
}

static void
assert_not_requested_video_meta (const PipelineObservation * obs,
    const gchar * label)
{
  assert_successful_observation (obs, label);
  fail_unless (buffer_video_meta_has_no_padding (obs->buffer, obs->caps),
      "%s: unexpected padded GstVideoMeta without downstream request", label);
}

static void
reconfigure_observation_clear (ReconfigureObservation * obs)
{
  g_clear_pointer (&obs->error_message, g_free);
  g_clear_pointer (&obs->error_debug, g_free);
}

static void
assert_successful_reconfigure_observation (const ReconfigureObservation * obs,
    const gchar * label)
{
  fail_unless (!obs->timeout, "%s: timed out", label);
  fail_unless (!obs->error, "%s: error: %s debug: %s", label,
      GST_STR_NULL (obs->error_message), GST_STR_NULL (obs->error_debug));
  fail_unless (obs->got_first_buffer, "%s: expected first buffer", label);
  fail_unless (obs->allocation_queries > 0,
      "%s: expected at least one ALLOCATION query", label);
  fail_unless (obs->allocation_queries_after_request > 0,
      "%s: expected a post-reconfigure ALLOCATION query", label);
  fail_unless (obs->got_second_buffer, "%s: expected second buffer", label);
}

GST_START_TEST (test_avfassetsrc_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc = make_avfassetsrc_pipeline_desc (TRUE);

  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_requested_video_meta (&obs, "avfassetsrc requested-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

GST_START_TEST (test_avfassetsrc_not_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc = make_avfassetsrc_pipeline_desc (FALSE);

  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_not_requested_video_meta (&obs, "avfassetsrc no-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

GST_START_TEST (test_avfassetsrc_reconfigure_video_meta)
{
  ReconfigureObservation obs;

  if (!element_available ("avfassetsrc") || !element_available ("fakesink")) {
    GST_INFO
        ("Skipping avfassetsrc reconfigure test, required elements missing");
    return;
  }

  obs = run_avfassetsrc_reconfigure_pipeline ();
  if (obs.skipped)
    return;
  assert_successful_reconfigure_observation (&obs,
      "avfassetsrc reconfigure-video-meta");
  fail_unless (obs.first_buffer_ok,
      "avfassetsrc reconfigure-video-meta: unexpected padded GstVideoMeta "
      "before downstream request");
  fail_unless (obs.second_buffer_ok,
      "avfassetsrc reconfigure-video-meta: expected GstVideoMeta after "
      "reconfigure");

  reconfigure_observation_clear (&obs);
}

GST_END_TEST;

GST_START_TEST (test_avfvideosrc_camera_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc =
      make_avfvideosrc_pipeline_desc (AVFVIDEOSRC_MODE_CAMERA, TRUE);

  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_requested_video_meta (&obs, "avfvideosrc camera requested-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

GST_START_TEST (test_avfvideosrc_camera_not_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc =
      make_avfvideosrc_pipeline_desc (AVFVIDEOSRC_MODE_CAMERA, FALSE);

  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_not_requested_video_meta (&obs, "avfvideosrc camera no-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

GST_START_TEST (test_avfvideosrc_screen_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc;

#if !TARGET_OS_OSX
  GST_INFO
      ("Skipping avfvideosrc screen test, screen capture path is macOS only");
  return;
#endif

  pipeline_desc =
      make_avfvideosrc_pipeline_desc (AVFVIDEOSRC_MODE_SCREEN, TRUE);
  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_requested_video_meta (&obs, "avfvideosrc screen requested-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

GST_START_TEST (test_avfvideosrc_screen_not_requested_video_meta)
{
  PipelineObservation obs;
  gchar *pipeline_desc;

#if !TARGET_OS_OSX
  GST_INFO
      ("Skipping avfvideosrc screen test, screen capture path is macOS only");
  return;
#endif

  pipeline_desc =
      make_avfvideosrc_pipeline_desc (AVFVIDEOSRC_MODE_SCREEN, FALSE);
  if (pipeline_desc == NULL)
    return;

  obs = run_pipeline (pipeline_desc);
  assert_not_requested_video_meta (&obs, "avfvideosrc screen no-meta");

  observation_clear (&obs);
  g_free (pipeline_desc);
}

GST_END_TEST;

/* These tests protect the zero-copy path for avf sources on the system memory
 * output path. AVFoundation often produces buffers with line padding, and the
 * only way to preserve zero-copy in that case is for the source to keep the
 * original buffer and describe its padded layout through GstVideoMeta when
 * downstream advertises support for it.
 */
static Suite *
avfsources_videometa_suite (void)
{
  Suite *s = suite_create ("avfsources-videometa");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  /* Keep a broader safety net around the whole case as teardown can hang when
     camera/screen capture permissions or platform services misbehave. */
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_avfassetsrc_requested_video_meta);
  tcase_add_test (tc, test_avfassetsrc_not_requested_video_meta);
  tcase_add_test (tc, test_avfassetsrc_reconfigure_video_meta);
  /* avfvideosrc exercises permission-sensitive capture paths, so keep these
   * checks out of CI for now and leave CI focused on avfassetsrc coverage.
   */
  if (!running_in_ci ()) {
    tcase_add_test (tc, test_avfvideosrc_camera_requested_video_meta);
    tcase_add_test (tc, test_avfvideosrc_camera_not_requested_video_meta);
    tcase_add_test (tc, test_avfvideosrc_screen_requested_video_meta);
    tcase_add_test (tc, test_avfvideosrc_screen_not_requested_video_meta);
  }

  return s;
}

static int
run_tests (void)
{
  Suite *s = avfsources_videometa_suite ();

  return gst_check_run_suite_nofork (s, "avfsources_videometa", __FILE__);
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
