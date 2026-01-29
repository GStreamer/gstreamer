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
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

typedef struct
{
  guint buffers;
  gboolean eos;
} TrackResult;

typedef struct
{
  TrackResult audio;
  TrackResult video;
  gboolean eos;
  gboolean error;
  gboolean timeout;
  gchar *error_message;
} PipelineResult;

typedef struct
{
  PipelineResult *result;
  GMainLoop *loop;
  GstElement *pipeline;
} PipelineContext;

typedef struct
{
  TrackResult *track;
  GstElement *sink;
  GstPad *pad;
  gulong probe_id;
} SinkWatch;

static void
on_handoff (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  TrackResult *track = user_data;

  (void) fakesink;
  (void) buffer;
  (void) pad;

  track->buffers++;
}

static GstPadProbeReturn
on_pad_event (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  TrackResult *track = user_data;

  (void) pad;

  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    if (GST_EVENT_TYPE (event) == GST_EVENT_EOS)
      track->eos = TRUE;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
on_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  PipelineContext *ctx = user_data;

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    if (err && ctx->result->error_message == NULL && err->message)
      ctx->result->error_message = g_strdup (err->message);
    g_clear_error (&err);
    g_free (debug);
    ctx->result->error = TRUE;
    g_main_loop_quit (ctx->loop);
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS) {
    ctx->result->eos = TRUE;
    g_main_loop_quit (ctx->loop);
  }

  return TRUE;
}

static gboolean
on_timeout (gpointer user_data)
{
  PipelineContext *ctx = user_data;

  ctx->result->timeout = TRUE;
  g_main_loop_quit (ctx->loop);
  return G_SOURCE_REMOVE;
}

static gboolean
start_pipeline (gpointer user_data)
{
  PipelineContext *ctx = user_data;

  gst_element_set_state (ctx->pipeline, GST_STATE_PAUSED);
  gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);

  return G_SOURCE_REMOVE;
}

static gboolean
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static void
sink_watch_init (SinkWatch * watch, TrackResult * track, GstElement * pipeline,
    const gchar * name)
{
  watch->track = track;
  watch->sink = gst_bin_get_by_name (GST_BIN (pipeline), name);
  fail_unless (watch->sink != NULL);

  g_signal_connect (watch->sink, "handoff", G_CALLBACK (on_handoff), track);

  watch->pad = gst_element_get_static_pad (watch->sink, "sink");
  fail_unless (watch->pad != NULL);
  watch->probe_id = gst_pad_add_probe (watch->pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, on_pad_event, track, NULL);
}

static void
sink_watch_clear (SinkWatch * watch)
{
  if (watch->pad != NULL) {
    if (watch->probe_id != 0)
      gst_pad_remove_probe (watch->pad, watch->probe_id);
    gst_object_unref (watch->pad);
    watch->pad = NULL;
  }
  if (watch->sink != NULL) {
    gst_object_unref (watch->sink);
    watch->sink = NULL;
  }
}

static PipelineResult
run_pipeline_for_uri (const gchar * uri, gboolean link_audio,
    gboolean link_video)
{
  PipelineResult result = { 0, };
  PipelineContext ctx = { &result, NULL, NULL };
  GstElement *pipeline;
  GstBus *bus;
  GString *pipeline_desc;
  guint bus_watch_id;
  guint timeout_id;
  SinkWatch audio_watch = { 0, };
  SinkWatch video_watch = { 0, };

  pipeline_desc = g_string_new ("avfassetsrc uri=");
  g_string_append (pipeline_desc, uri);
  g_string_append (pipeline_desc, " name=src ");
  if (link_audio) {
    g_string_append (pipeline_desc,
        "src.audio ! queue ! fakesink name=asink signal-handoffs=true "
        "sync=false ");
  }
  if (link_video) {
    g_string_append (pipeline_desc,
        "src.video ! queue ! fakesink name=vsink signal-handoffs=true "
        "sync=false ");
  }

  pipeline = gst_parse_launch (pipeline_desc->str, NULL);
  g_string_free (pipeline_desc, TRUE);
  fail_unless (pipeline != NULL);

  if (link_audio)
    sink_watch_init (&audio_watch, &result.audio, pipeline, "asink");
  if (link_video)
    sink_watch_init (&video_watch, &result.video, pipeline, "vsink");

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  ctx.loop = g_main_loop_new (NULL, FALSE);
  ctx.pipeline = pipeline;

  bus_watch_id = gst_bus_add_watch (bus, on_bus_message, &ctx);
  timeout_id = g_timeout_add_seconds (5, on_timeout, &ctx);

  g_idle_add (start_pipeline, &ctx);
  g_main_loop_run (ctx.loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_source_remove (timeout_id);
  g_source_remove (bus_watch_id);

  sink_watch_clear (&audio_watch);
  sink_watch_clear (&video_watch);

  g_main_loop_unref (ctx.loop);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return result;
}

static void
assert_supported_track (const TrackResult * track, const gchar * label)
{
  fail_unless (track->buffers > 0, "%s: expected buffers", label);
  fail_unless (track->eos, "%s: expected EOS", label);
}

static void
assert_unsupported_track (const TrackResult * track, const gchar * label)
{
  fail_unless (track->buffers == 0, "%s: expected no buffers", label);
  fail_unless (track->eos, "%s: expected EOS", label);
}

static void
run_partial_support_test (const gchar * filename, gboolean audio_supported,
    gboolean video_supported, gboolean link_audio, gboolean link_video)
{
  gchar *filepath;
  gchar *resolved_path;
  gchar *uri;
  PipelineResult result;

  if (!element_available ("avfassetsrc")
      || !element_available ("fakesink")
      || !element_available ("queue")) {
    GST_INFO ("Skipping test, required elements missing");
    return;
  }

  filepath = g_build_filename (GST_TEST_FILES_PATH, filename, NULL);
  if (!g_file_test (filepath, G_FILE_TEST_EXISTS)) {
    GST_INFO ("Skipping test, missing file: %s", filepath);
    g_free (filepath);
    return;
  }

  resolved_path = g_canonicalize_filename (filepath, NULL);
  g_free (filepath);

  uri = g_filename_to_uri (resolved_path, NULL, NULL);
  g_free (resolved_path);
  fail_unless (uri != NULL);

  result = run_pipeline_for_uri (uri, link_audio, link_video);
  fail_unless (!result.timeout, "Pipeline timed out");
  fail_unless (!result.error, "Pipeline error: %s",
      result.error_message ? result.error_message : "unknown");
  fail_unless (result.eos, "Pipeline EOS missing");
  if (link_audio) {
    if (audio_supported)
      assert_supported_track (&result.audio, "audio");
    else
      assert_unsupported_track (&result.audio, "audio");
  }
  if (link_video) {
    if (video_supported)
      assert_supported_track (&result.video, "video");
    else
      assert_unsupported_track (&result.video, "video");
  }
  g_free (result.error_message);
  g_free (uri);
}

GST_START_TEST (test_avfassetsrc_vp8_aac_partial)
{
  /* A mp4 where the audio track is supported but the video track isn't should
     still successfully play the audio track. We know AVAssetReader doesn't
     support VP8 video. */
  run_partial_support_test ("vp8_aac.mp4", TRUE, FALSE, TRUE, TRUE);
  run_partial_support_test ("vp8_aac.mp4", TRUE, FALSE, TRUE, FALSE);
}

GST_END_TEST;

GST_START_TEST (test_avfassetsrc_h264_opus_partial)
{
  /* A mp4 where the video track is supported but the audio track isn't should
     still successfully play the video track. We know AVAssetReader doesn't
     suport opus audio. */
  run_partial_support_test ("h264_opus.mp4", FALSE, TRUE, TRUE, TRUE);
  run_partial_support_test ("h264_opus.mp4", FALSE, TRUE, FALSE, TRUE);
}

GST_END_TEST;

GST_START_TEST (test_avfassetsrc_h264_aac_partial)
{
  /* A mp4 where both the video and audio tracks are support should make the
     individual tracks playable even when not all pads are connected. */
  run_partial_support_test ("h264_aac.mp4", TRUE, TRUE, TRUE, TRUE);
  run_partial_support_test ("h264_aac.mp4", TRUE, TRUE, TRUE, FALSE);
  run_partial_support_test ("h264_aac.mp4", TRUE, TRUE, FALSE, TRUE);
}

GST_END_TEST;

static Suite *
avfassetsrc_partial_suite (void)
{
  Suite *s = suite_create ("avfassetsrc-partial");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_avfassetsrc_vp8_aac_partial);
  tcase_add_test (tc_chain, test_avfassetsrc_h264_opus_partial);
  tcase_add_test (tc_chain, test_avfassetsrc_h264_aac_partial);

  return s;
}

static int
run_tests (int argc, char **argv, gpointer user_data)
{
  Suite *s;
  SRunner *sr;

  (void) user_data;

  s = avfassetsrc_partial_suite ();
  sr = srunner_create (s);
  /* AVFoundation/CoreFoundation APIs are not fork-safe. The default
   * check fork mode triggers a crash when these APIs are used, so we
   * must run the tests in-process. */
  srunner_set_fork_status (sr, CK_NOFORK);
  srunner_run (sr, NULL, NULL, CK_NORMAL);
  srunner_free (sr);
  return 0;
}

int
main (int argc, char **argv)
{
  gst_check_init (&argc, &argv);
#if TARGET_OS_OSX
  return gst_macos_main ((GstMainFunc) run_tests, argc, argv, NULL);
#else
  return run_tests (argc, argv, NULL);
#endif
}
