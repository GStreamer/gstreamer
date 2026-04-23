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
#include <gst/video/video.h>
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

#define VTDEC_COMPARE_FRAMES 8

typedef struct
{
  guint buffers;
  gboolean eos;
  gboolean error;
  gboolean timeout;
  gchar *error_message;
  GPtrArray *sample_buffers;
  GstCaps *caps;
} PipelineResult;

typedef struct
{
  PipelineResult *result;
  GMainLoop *loop;
  GstElement *pipeline;
  guint timeout_id;
  guint max_samples;
} PipelineContext;

typedef struct
{
  gboolean enabled;
  gboolean had_value;
  gchar *old_value;
} EnvOverride;

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

static GstFlowReturn
on_new_sample (GstAppSink * appsink, gpointer user_data)
{
  PipelineContext *ctx = user_data;
  GstSample *sample = gst_app_sink_pull_sample (appsink);
  GstBuffer *buffer;
  GstCaps *caps;

  if (sample == NULL)
    return GST_FLOW_ERROR;

  buffer = gst_sample_get_buffer (sample);
  caps = gst_sample_get_caps (sample);

  if (ctx->result->caps == NULL && caps != NULL)
    ctx->result->caps = gst_caps_ref (caps);
  if (buffer != NULL)
    g_ptr_array_add (ctx->result->sample_buffers, gst_buffer_ref (buffer));

  if (ctx->max_samples > 0 && ctx->result->sample_buffers->len >=
      ctx->max_samples)
    g_main_loop_quit (ctx->loop);

  gst_sample_unref (sample);

  return GST_FLOW_OK;
}

static void
pipeline_result_clear (PipelineResult * result)
{
  g_clear_pointer (&result->error_message, g_free);
  g_clear_pointer (&result->sample_buffers, g_ptr_array_unref);
  gst_clear_caps (&result->caps);
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
    if (err && ctx->result->error_message == NULL && err->message)
      ctx->result->error_message = g_strdup (err->message);
    if (err != NULL)
      GST_WARNING ("Pipeline error: %s", err->message);
    if (debug != NULL)
      GST_WARNING ("Pipeline error details: %s", debug);
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

  ctx->timeout_id = 0;
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
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static gboolean
require_elements_or_skip (const gchar * const *elements, gsize n_elements)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;
  gsize i;

  for (i = 0; i < n_elements; i++) {
    if (element_available (elements[i]))
      continue;
    if (strict)
      fail_unless (FALSE, "Missing required element: %s", elements[i]);
    GST_INFO ("Skipping test, missing required element: %s", elements[i]);
    return FALSE;
  }

  return TRUE;
}

static void
env_override_enable (EnvOverride * override, const gchar * name,
    const gchar * value)
{
  const gchar *current;

  current = g_getenv (name);
  if (current != NULL)
    return;

  override->enabled = TRUE;
  override->had_value = FALSE;
  override->old_value = NULL;

  fail_unless (g_setenv (name, value, TRUE));
}

static void
env_override_restore (EnvOverride * override, const gchar * name)
{
  if (!override->enabled)
    return;

  if (override->had_value)
    fail_unless (g_setenv (name, override->old_value, TRUE));
  else
    g_unsetenv (name);

  g_clear_pointer (&override->old_value, g_free);
  override->enabled = FALSE;
  override->had_value = FALSE;
}

static PipelineResult
run_pipeline (const gchar * pipeline_desc, gboolean collect_samples)
{
  PipelineResult result = { 0, FALSE, FALSE, FALSE, NULL, NULL, NULL };
  PipelineContext ctx = { &result, NULL, NULL, 0, 0 };
  GstElement *pipeline;
  GstElement *sink;
  GstBus *bus;
  guint bus_watch_id;
  guint timeout_id;

  pipeline = gst_parse_launch (pipeline_desc, NULL);
  fail_unless (pipeline != NULL);

  if (collect_samples)
    result.sample_buffers = g_ptr_array_new_with_free_func ((GDestroyNotify)
        gst_buffer_unref);

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (sink != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);

  ctx.loop = g_main_loop_new (NULL, FALSE);
  ctx.pipeline = pipeline;
  if (collect_samples)
    ctx.max_samples = VTDEC_COMPARE_FRAMES;

  if (collect_samples) {
    g_object_set (sink, "emit-signals", TRUE, NULL);
    g_signal_connect (sink, "new-sample", G_CALLBACK (on_new_sample), &ctx);
  } else {
    g_signal_connect (sink, "handoff", G_CALLBACK (on_handoff), &ctx);
  }

  bus_watch_id = gst_bus_add_watch (bus, on_bus_message, &ctx);
  timeout_id = g_timeout_add_seconds (10, on_timeout, &ctx);
  ctx.timeout_id = timeout_id;

  g_idle_add (start_pipeline, &ctx);
  g_main_loop_run (ctx.loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  if (ctx.timeout_id != 0)
    g_source_remove (ctx.timeout_id);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (ctx.loop);
  gst_object_unref (sink);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return result;
}

static void
run_vtdec_pipeline_test (const gchar * const *elements, gsize n_elements,
    const gchar * pipeline_tail, gboolean disable_mvk_argument_buffers)
{
  gchar *filepath;
  gchar *resolved_path;
  gchar *pipeline_desc;
  PipelineResult result;
  EnvOverride env_override = { 0, };

  if (!require_elements_or_skip (elements, n_elements))
    return;

  filepath = g_build_filename (GST_TEST_FILES_PATH, "test.ts", NULL);
  if (!g_file_test (filepath, G_FILE_TEST_EXISTS)) {
    GST_INFO ("Skipping test, missing file: %s", filepath);
    g_free (filepath);
    return;
  }

  resolved_path = g_canonicalize_filename (filepath, NULL);
  g_free (filepath);

  pipeline_desc = g_strdup_printf ("filesrc location=\"%s\" ! "
      "tsdemux ! h264parse ! vtdec ! %s", resolved_path, pipeline_tail);
  g_free (resolved_path);

  if (disable_mvk_argument_buffers) {
    /* Temporary workaround for MoltenVK issue #2705. Remove this once the
     * bundled MoltenVK includes the upstream fix. Respect a user-provided
     * MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS value so the current MoltenVK
     * behavior can be checked explicitly. */
    env_override_enable (&env_override,
        "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0");
  }

  result = run_pipeline (pipeline_desc, FALSE);

  env_override_restore (&env_override, "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS");
  g_free (pipeline_desc);

  fail_unless (!result.timeout, "Pipeline timed out");
  fail_unless (!result.error, "Pipeline error: %s",
      result.error_message ? result.error_message : "unknown");
  fail_unless (result.eos, "Pipeline EOS missing");
  fail_unless (result.buffers > 0, "Expected buffers at sink");
  pipeline_result_clear (&result);
}

static PipelineResult
run_vtdec_sample_collection_pipeline (const gchar * pipeline_tail,
    gboolean disable_mvk_argument_buffers)
{
  gchar *filepath;
  gchar *resolved_path;
  gchar *pipeline_desc;
  PipelineResult result;
  EnvOverride env_override = { 0, };

  filepath = g_build_filename (GST_TEST_FILES_PATH, "test.ts", NULL);
  fail_unless (g_file_test (filepath, G_FILE_TEST_EXISTS),
      "Missing test file: %s", filepath);

  resolved_path = g_canonicalize_filename (filepath, NULL);
  g_free (filepath);

  pipeline_desc = g_strdup_printf ("filesrc location=\"%s\" ! "
      "tsdemux ! h264parse ! vtdec ! %s", resolved_path, pipeline_tail);
  g_free (resolved_path);

  if (disable_mvk_argument_buffers) {
    env_override_enable (&env_override,
        "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS", "0");
  }

  result = run_pipeline (pipeline_desc, TRUE);

  env_override_restore (&env_override, "MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS");
  g_free (pipeline_desc);

  fail_unless (!result.timeout, "Pipeline timed out");
  fail_unless (!result.error, "Pipeline error: %s",
      result.error_message ? result.error_message : "unknown");
  fail_unless (result.eos || result.sample_buffers->len == VTDEC_COMPARE_FRAMES,
      "Pipeline EOS missing before collecting %u samples",
      VTDEC_COMPARE_FRAMES);
  fail_unless (result.caps != NULL, "Expected output caps");
  fail_unless (result.sample_buffers != NULL && result.sample_buffers->len > 0,
      "Expected samples at appsink");

  return result;
}

static void
compare_video_buffers (GstBuffer * expected, GstBuffer * actual,
    const GstVideoInfo * info)
{
  GstVideoFrame expected_frame;
  GstVideoFrame actual_frame;

  fail_unless (gst_video_frame_map (&expected_frame, info, expected,
          GST_MAP_READ));
  fail_unless (gst_video_frame_map (&actual_frame, info, actual, GST_MAP_READ));

  for (guint plane = 0; plane < GST_VIDEO_INFO_N_PLANES (info); plane++) {
    gint comp[GST_VIDEO_MAX_COMPONENTS];
    const guint8 *expected_row;
    const guint8 *actual_row;
    guint row_size;
    guint height;

    gst_video_format_info_component (info->finfo, plane, comp);
    row_size = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]) *
        GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
    if (row_size == 0) {
      row_size = MIN (GST_VIDEO_INFO_COMP_PSTRIDE (&expected_frame.info, plane),
          GST_VIDEO_INFO_COMP_PSTRIDE (&actual_frame.info, plane));
    }
    height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

    expected_row = GST_VIDEO_FRAME_PLANE_DATA (&expected_frame, plane);
    actual_row = GST_VIDEO_FRAME_PLANE_DATA (&actual_frame, plane);

    for (guint row = 0; row < height; row++) {
      fail_unless (memcmp (expected_row, actual_row, row_size) == 0,
          "Frame data differs at plane %u row %u", plane, row);

      expected_row += GST_VIDEO_FRAME_PLANE_STRIDE (&expected_frame, plane);
      actual_row += GST_VIDEO_FRAME_PLANE_STRIDE (&actual_frame, plane);
    }
  }

  gst_video_frame_unmap (&actual_frame);
  gst_video_frame_unmap (&expected_frame);
}

static void
compare_pipeline_results (PipelineResult * expected, PipelineResult * actual)
{
  GstVideoInfo info;

  fail_unless (gst_video_info_from_caps (&info, expected->caps));
  fail_unless (gst_caps_can_intersect (expected->caps, actual->caps),
      "Output caps differ: expected %" GST_PTR_FORMAT ", actual %"
      GST_PTR_FORMAT, expected->caps, actual->caps);
  fail_unless (expected->sample_buffers->len == actual->sample_buffers->len,
      "Expected %u buffers, got %u", expected->sample_buffers->len,
      actual->sample_buffers->len);

  for (guint i = 0; i < expected->sample_buffers->len; i++) {
    GstBuffer *expected_buffer =
        g_ptr_array_index (expected->sample_buffers, i);
    GstBuffer *actual_buffer = g_ptr_array_index (actual->sample_buffers, i);

    compare_video_buffers (expected_buffer, actual_buffer, &info);
  }
}

GST_START_TEST (test_vtdec_system_memory_output)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "fakesink"
  };

  run_vtdec_pipeline_test (required, G_N_ELEMENTS (required),
      "video/x-raw,format=NV12 ! "
      "fakesink name=sink signal-handoffs=true sync=false", FALSE);
}

GST_END_TEST;

GST_START_TEST (test_vtdec_vulkan_image_download_matches_system_memory)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "vulkandownload", "appsink"
  };
  PipelineResult system_result;
  PipelineResult vulkan_result;

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  system_result =
      run_vtdec_sample_collection_pipeline ("video/x-raw,format=NV12 ! "
      "appsink name=sink sync=false", FALSE);
  vulkan_result =
      run_vtdec_sample_collection_pipeline
      ("video/x-raw(memory:VulkanImage),format=NV12 ! " "vulkandownload ! "
      "video/x-raw,format=NV12 ! " "appsink name=sink sync=false", TRUE);

  compare_pipeline_results (&system_result, &vulkan_result);

  pipeline_result_clear (&vulkan_result);
  pipeline_result_clear (&system_result);
}

GST_END_TEST;

GST_START_TEST (test_vtdec_gl_download_matches_system_memory)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "gldownload", "appsink"
  };
  PipelineResult system_result;
  PipelineResult gl_result;

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  system_result =
      run_vtdec_sample_collection_pipeline ("video/x-raw,format=NV12 ! "
      "appsink name=sink sync=false", FALSE);
  gl_result =
      run_vtdec_sample_collection_pipeline
      ("video/x-raw(memory:GLMemory),format=NV12 ! " "gldownload ! "
      "video/x-raw,format=NV12 ! " "appsink name=sink sync=false", FALSE);

  compare_pipeline_results (&system_result, &gl_result);

  pipeline_result_clear (&gl_result);
  pipeline_result_clear (&system_result);
}

GST_END_TEST;

GST_START_TEST (test_vtdec_gl_memory_output)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "glcolorconvert",
    "gldownload", "fakesink"
  };

  run_vtdec_pipeline_test (required, G_N_ELEMENTS (required),
      "video/x-raw(memory:GLMemory),format=NV12 ! "
      "glcolorconvert ! "
      "video/x-raw(memory:GLMemory),format=RGBA ! "
      "gldownload ! "
      "video/x-raw,format=RGBA ! "
      "fakesink name=sink signal-handoffs=true sync=false", FALSE);
}

GST_END_TEST;

GST_START_TEST (test_vtdec_vulkan_image_output)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "vulkancolorconvert",
    "vulkandownload", "fakesink"
  };

  run_vtdec_pipeline_test (required, G_N_ELEMENTS (required),
      "video/x-raw(memory:VulkanImage),format=NV12 ! "
      "vulkancolorconvert ! "
      "video/x-raw(memory:VulkanImage),format=BGRA ! "
      "vulkandownload ! "
      "video/x-raw,format=BGRA ! "
      "fakesink name=sink signal-handoffs=true sync=false", TRUE);
}

GST_END_TEST;

static Suite *
vtdec_output_paths_suite (void)
{
  Suite *s = suite_create ("vtdec-output-paths");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_vtdec_system_memory_output);
  tcase_add_test (tc_chain, test_vtdec_gl_memory_output);
  tcase_add_test (tc_chain, test_vtdec_vulkan_image_output);
  tcase_add_test (tc_chain, test_vtdec_gl_download_matches_system_memory);
  tcase_add_test (tc_chain,
      test_vtdec_vulkan_image_download_matches_system_memory);

  return s;
}

static int
run_tests (int argc, char **argv, gpointer user_data)
{
  Suite *s;
  SRunner *sr;

  (void) argc;
  (void) argv;
  (void) user_data;

  s = vtdec_output_paths_suite ();
  sr = srunner_create (s);
  /* VideoToolbox / GL / Vulkan integration is not fork-safe enough for the
   * default check mode on Apple platforms. Run in-process like the other
   * applemedia pipeline tests. */
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
