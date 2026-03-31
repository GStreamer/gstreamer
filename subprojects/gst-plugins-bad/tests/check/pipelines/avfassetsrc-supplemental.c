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
#include <VideoToolbox/VideoToolbox.h>
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

#if TARGET_OS_OSX || TARGET_OS_IOS || TARGET_OS_TV || TARGET_OS_VISION
#define HAVE_SUPPLEMENTAL
#if (TARGET_OS_OSX && __MAC_OS_X_VERSION_MAX_ALLOWED >= 110000) || (TARGET_OS_IOS && __IPHONE_OS_VERSION_MAX_ALLOWED >= 260200) || (TARGET_OS_TV && __TV_OS_VERSION_MAX_ALLOWED >= 260200) || (TARGET_OS_VISION && __VISION_OS_VERSION_MAX_ALLOWED >= 260200)
#define HAVE_SUPPLEMENTAL_DEFINITION
#else
#include <dlfcn.h>
#endif
#endif

typedef struct
{
  guint buffers;
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

static void
on_handoff (GstElement * fakesink, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  PipelineContext *ctx = user_data;
  (void) fakesink;
  (void) buffer;
  (void) pad;

  ctx->result->buffers++;
}

static gboolean
on_bus_message (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  PipelineContext *ctx = user_data;
  GstMessageType msg_type = GST_MESSAGE_TYPE (msg);
  (void) bus;

  if (msg_type == GST_MESSAGE_WARNING) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_warning (msg, &err, &debug);
    if (err != NULL)
      GST_WARNING ("Pipeline warning: %s", err->message);
    if (debug != NULL)
      GST_WARNING ("Pipeline warning details: %s", debug);
    g_clear_error (&err);
    g_free (debug);
  }

  if (msg_type == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    if (err) {
      GST_WARNING ("Pipeline error: %s", err->message);
      if (ctx->result->error_message == NULL && err->message) {
        ctx->result->error_message = g_strdup (err->message);
      }
    }
    if (debug)
      GST_WARNING ("Pipeline debug details: %s", debug);
    g_clear_error (&err);
    g_free (debug);
    ctx->result->error = TRUE;
    g_main_loop_quit (ctx->loop);
  } else if (msg_type == GST_MESSAGE_EOS) {
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
  GstStateChangeReturn ret;

  ret = gst_element_set_state (ctx->pipeline, GST_STATE_PAUSED);
  GST_LOG ("PAUSED state request returned: %d", ret);

  ret = gst_element_set_state (ctx->pipeline, GST_STATE_PLAYING);
  GST_LOG ("PLAYING state request returned: %d", ret);

  return G_SOURCE_REMOVE;
}

static gboolean
pipeline_error_is_cannot_decode (const PipelineResult * result)
{
  if (result->error_message == NULL)
    return FALSE;

  if (g_strrstr (result->error_message, "Cannot Decode") != NULL)
    return TRUE;
  if (g_strrstr (result->error_message,
          "AVFoundationErrorDomain Code=-11821") != NULL)
    return TRUE;
  if (g_strrstr (result->error_message,
          "NSOSStatusErrorDomain Code=-12911") != NULL)
    return TRUE;

  return FALSE;
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

static PipelineResult
run_pipeline_for_uri (const gchar * uri)
{
  PipelineResult result = { 0, FALSE, FALSE, FALSE, NULL };
  PipelineContext ctx = { &result, NULL, NULL };
  GstElement *pipeline;
  GstElement *sink;
  GstBus *bus;
  gchar *pipeline_desc;
  guint bus_watch_id;
  guint timeout_id;

  pipeline_desc = g_strdup_printf ("avfassetsrc uri=%s name=src "
      "src.video ! queue ! fakesink name=sink signal-handoffs=true "
      "sync=false", uri);
  pipeline = gst_parse_launch (pipeline_desc, NULL);
  g_free (pipeline_desc);
  fail_unless (pipeline != NULL);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  ctx.loop = g_main_loop_new (NULL, FALSE);
  ctx.pipeline = pipeline;

  g_signal_connect (sink, "handoff", G_CALLBACK (on_handoff), &ctx);

  bus_watch_id = gst_bus_add_watch (bus, on_bus_message, &ctx);

  timeout_id = g_timeout_add_seconds (5, on_timeout, &ctx);

  g_idle_add (start_pipeline, &ctx);
  g_main_loop_run (ctx.loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_source_remove (timeout_id);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (ctx.loop);
  gst_object_unref (sink);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return result;
}

typedef void (*VTRegisterSupplementalVideoDecoderIfAvailableFunc)
  (CMVideoCodecType codecType);

/*
 * Keep in sync with gst_vtutil_register_supplemental_decoder in sys/applemedia/vtutil.c
 */
static gboolean
register_supplemental_decoder (CMVideoCodecType codec_type)
{
#ifdef HAVE_SUPPLEMENTAL
#ifdef HAVE_SUPPLEMENTAL_DEFINITION
  if (__builtin_available (macOS 11.0, iOS 26.2, tvOS 26.2, visionOS 26.2, *)) {
    VTRegisterSupplementalVideoDecoderIfAvailable (codec_type);
    return TRUE;
  }
#else
  VTRegisterSupplementalVideoDecoderIfAvailableFunc func;

  func = (VTRegisterSupplementalVideoDecoderIfAvailableFunc)
      dlsym (RTLD_DEFAULT, "VTRegisterSupplementalVideoDecoderIfAvailable");
  if (func != NULL) {
    func (codec_type);
    return TRUE;
  }
#endif
#endif

  return FALSE;
}

static void
run_supplemental_codec_test (const gchar * filename,
    CMVideoCodecType codec_type)
{
  gchar *filepath;
  gchar *resolved_path;
  gchar *uri;
  gboolean pre_supported = FALSE;
  gboolean post_supported = FALSE;
  PipelineResult result;

  if (!element_available ("avfassetsrc")
      || !element_available ("fakesink")) {
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
  if (uri == NULL) {
    GST_INFO ("Skipping test, failed to build file URI");
    return;
  }

  pre_supported = VTIsHardwareDecodeSupported (codec_type);
  result = run_pipeline_for_uri (uri);
  post_supported = VTIsHardwareDecodeSupported (codec_type);

  if (result.buffers > 0) {
    g_free (result.error_message);
    g_free (uri);
    return;
  }

  if (result.error && pipeline_error_is_cannot_decode (&result)) {
    g_free (result.error_message);
    g_free (uri);
    return;
  }

  if (result.error && (pre_supported || post_supported)) {
    fail_unless (FALSE, "Codec supported but pipeline failed");
  }

  if (!pre_supported && !post_supported) {
    gboolean registration_ok = register_supplemental_decoder (codec_type);
    gboolean post_registration_supported =
        VTIsHardwareDecodeSupported (codec_type);
    if (registration_ok && post_registration_supported) {
      fail_unless (FALSE, "Codec became supported after manual registration");
    }
  }
  g_free (result.error_message);
  g_free (uri);
}

GST_START_TEST (test_avfassetsrc_vp9_supplemental)
{
  run_supplemental_codec_test ("vp9_only.mp4", kCMVideoCodecType_VP9);
}

GST_END_TEST;

GST_START_TEST (test_avfassetsrc_av1_supplemental)
{
  run_supplemental_codec_test ("av1_only.mp4", kCMVideoCodecType_AV1);
}

GST_END_TEST;

static Suite *
avfassetsrc_supplemental_suite (void)
{
  Suite *s = suite_create ("avfassetsrc-supplemental");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_avfassetsrc_vp9_supplemental);
  tcase_add_test (tc_chain, test_avfassetsrc_av1_supplemental);

  return s;
}

static int
run_tests ()
{
  /* AVFoundation/CoreFoundation APIs are not fork-safe. The default
   * check fork mode triggers a crash when these APIs are used, so we
   * must run the tests in-process. */
  Suite *s = avfassetsrc_supplemental_suite ();
  return gst_check_run_suite_nofork (s, "avfassetsrc_supplemental", __FILE__);
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
