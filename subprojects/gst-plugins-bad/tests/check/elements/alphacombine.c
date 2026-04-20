/* GStreamer unit tests for the alphacombine element
 *
 * Copyright (C) 2026 Dominique Leuenberger
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

#include <gst/app/gstappsink.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

static const guint test_width = 16;
static const guint test_height = 8;

static GstStaticPadTemplate sink_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate alpha_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate output_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

typedef struct
{
  GstElement *element;
  GstPad *sink_srcpad;
  GstPad *alpha_srcpad;
  GstPad *output_sinkpad;
} AlphaCombineFixture;

typedef struct
{
  GstBufferPool *pool;
  guint size;
  guint min_buffers;
  guint max_buffers;
} AllocationQueryData;

static gboolean
allocation_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  AllocationQueryData *data = g_object_get_data (G_OBJECT (pad),
      "allocation-query-data");

  if (GST_QUERY_TYPE (query) == GST_QUERY_ALLOCATION && data) {
    gst_query_add_allocation_pool (query, data->pool, data->size,
        data->min_buffers, data->max_buffers);
    return TRUE;
  }

  return gst_pad_query_default (pad, parent, query);
}

static AlphaCombineFixture *
setup_alphacombine (void)
{
  AlphaCombineFixture *fixture = g_new0 (AlphaCombineFixture, 1);

  fixture->element = gst_check_setup_element ("alphacombine");
  fail_unless (fixture->element != NULL);

  fixture->sink_srcpad = gst_check_setup_src_pad_by_name (fixture->element,
      &sink_src_template, "sink");
  fixture->alpha_srcpad = gst_check_setup_src_pad_by_name (fixture->element,
      &alpha_src_template, "alpha");
  fixture->output_sinkpad = gst_check_setup_sink_pad (fixture->element,
      &output_sink_template);

  gst_pad_set_active (fixture->sink_srcpad, TRUE);
  gst_pad_set_active (fixture->alpha_srcpad, TRUE);
  gst_pad_set_active (fixture->output_sinkpad, TRUE);

  fail_unless_equals_int (gst_element_set_state (fixture->element,
          GST_STATE_PLAYING), GST_STATE_CHANGE_SUCCESS);

  return fixture;
}

static void
cleanup_alphacombine (AlphaCombineFixture * fixture)
{
  fail_unless_equals_int (gst_element_set_state (fixture->element,
          GST_STATE_NULL), GST_STATE_CHANGE_SUCCESS);

  gst_pad_set_active (fixture->sink_srcpad, FALSE);
  gst_pad_set_active (fixture->alpha_srcpad, FALSE);
  gst_pad_set_active (fixture->output_sinkpad, FALSE);

  gst_check_teardown_pad_by_name (fixture->element, "sink");
  gst_check_teardown_pad_by_name (fixture->element, "alpha");
  gst_check_teardown_sink_pad (fixture->element);
  gst_check_teardown_element (fixture->element);

  g_free (fixture);
}

static GstCaps *
make_caps_with_range (GstVideoFormat format, GstVideoColorRange range)
{
  GstVideoInfo info;

  gst_video_info_set_format (&info, format, test_width, test_height);
  info.colorimetry.range = range;
  return gst_video_info_to_caps (&info);
}

static GstCaps *
make_caps (GstVideoFormat format)
{
  return make_caps_with_range (format, GST_VIDEO_COLOR_RANGE_0_255);
}

static void
set_input_caps (AlphaCombineFixture * fixture, GstCaps * sink_caps,
    GstCaps * alpha_caps)
{
  gst_check_setup_events_with_stream_id (fixture->sink_srcpad,
      fixture->element, sink_caps, GST_FORMAT_TIME, "sink");
  gst_check_setup_events_with_stream_id (fixture->alpha_srcpad,
      fixture->element, alpha_caps, GST_FORMAT_TIME, "alpha");
}

static GstBuffer *
make_video_buffer (GstVideoFormat format, guint plane_0_memory_prefix)
{
  GstVideoInfo info;
  GstBuffer *buffer;
  gsize offsets[GST_VIDEO_MAX_PLANES] = { 0, };
  gint strides[GST_VIDEO_MAX_PLANES] = { 0, };
  guint n_planes, i;

  gst_video_info_set_format (&info, format, test_width, test_height);
  n_planes = GST_VIDEO_INFO_N_PLANES (&info);

  buffer = gst_buffer_new ();
  for (i = 0; i < n_planes; i++) {
    GstMemory *memory;
    gsize plane_size;
    guint plane_height = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (info.finfo, i,
        GST_VIDEO_INFO_HEIGHT (&info));

    /* Add optional byte padding before plane 0 to validate plane offsets. */
    offsets[i] =
        gst_buffer_get_size (buffer) + (i == 0 ? plane_0_memory_prefix : 0);
    strides[i] = GST_VIDEO_INFO_PLANE_STRIDE (&info, i);
    plane_size = strides[i] * plane_height;

    memory = gst_allocator_alloc (NULL,
        plane_size + (i == 0 ? plane_0_memory_prefix : 0), NULL);
    gst_buffer_append_memory (buffer, memory);
  }

  fail_unless (gst_buffer_add_video_meta_full (buffer, 0, format, test_width,
          test_height, n_planes, offsets, strides) != NULL);

  return buffer;
}

static guint
n_planes_for_format (GstVideoFormat format)
{
  const GstVideoFormatInfo *info = gst_video_format_get_info (format);

  fail_unless (info != NULL);
  return GST_VIDEO_FORMAT_INFO_N_PLANES (info);
}

static GstBuffer *
pop_output_buffer (void)
{
  GstBuffer *buffer;

  fail_unless_equals_int (g_list_length (buffers), 1);
  buffer = GST_BUFFER_CAST (buffers->data);
  buffers = g_list_remove (buffers, buffer);

  return buffer;
}

static guint
n_output_buffers (void)
{
  /* gst_check_setup_sink_pad() stores captured buffers in this check global. */
  return g_list_length (buffers);
}

static gboolean
count_parent_buffer_meta (GstBuffer * buffer, GstMeta ** meta,
    gpointer user_data)
{
  guint *count = user_data;

  if ((*meta)->info->api == GST_PARENT_BUFFER_META_API_TYPE)
    (*count)++;

  return TRUE;
}

static void
assert_parent_buffer_meta_count (GstBuffer * buffer, guint expected)
{
  guint count = 0;

  gst_buffer_foreach_meta (buffer, count_parent_buffer_meta, &count);
  fail_unless_equals_int (count, expected);
}

static void
assert_output_caps (AlphaCombineFixture * fixture, GstVideoFormat format)
{
  GstCaps *caps;
  GstVideoInfo info;

  caps = gst_pad_get_current_caps (fixture->output_sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&info, caps));
  fail_unless_equals_int (GST_VIDEO_INFO_FORMAT (&info), format);
  fail_unless_equals_int (GST_VIDEO_INFO_WIDTH (&info), test_width);
  fail_unless_equals_int (GST_VIDEO_INFO_HEIGHT (&info), test_height);
  gst_caps_unref (caps);
}

static void
assert_output_buffer (GstBuffer * output, GstBuffer * sink_buffer,
    guint alpha_plane_memory_prefix, GstVideoFormat sink_format,
    GstVideoFormat src_format)
{
  GstVideoMeta *meta;
  guint alpha_plane_idx;

  meta = gst_buffer_get_video_meta (output);
  fail_unless (meta != NULL);

  alpha_plane_idx = n_planes_for_format (sink_format);
  fail_unless_equals_int (meta->format, src_format);
  fail_unless_equals_int (meta->width, test_width);
  fail_unless_equals_int (meta->height, test_height);
  fail_unless_equals_int (meta->n_planes, alpha_plane_idx + 1);
  fail_unless_equals_int64 (meta->offset[alpha_plane_idx],
      gst_buffer_get_size (sink_buffer) + alpha_plane_memory_prefix);
  fail_unless_equals_int (gst_buffer_n_memory (output),
      gst_buffer_n_memory (sink_buffer) + 1);
  assert_parent_buffer_meta_count (output, 2);
}

static void
run_combine_test (GstVideoFormat sink_format, GstVideoFormat alpha_format,
    GstVideoFormat expected_format, guint alpha_plane_memory_prefix)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (sink_format);
  GstCaps *alpha_caps = make_caps (alpha_format);
  GstBuffer *sink_buffer = make_video_buffer (sink_format, 0);
  GstBuffer *alpha_buffer =
      make_video_buffer (alpha_format, alpha_plane_memory_prefix);
  GstBuffer *output;

  set_input_caps (fixture, sink_caps, alpha_caps);

  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad,
          gst_buffer_ref (alpha_buffer)), GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad,
          gst_buffer_ref (sink_buffer)), GST_FLOW_OK);

  assert_output_caps (fixture, expected_format);
  output = pop_output_buffer ();
  assert_output_buffer (output, sink_buffer, alpha_plane_memory_prefix,
      sink_format, expected_format);
  gst_buffer_unref (output);

  gst_buffer_unref (sink_buffer);
  gst_buffer_unref (alpha_buffer);
  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_START_TEST (test_i420_i420_to_a420)
{
  run_combine_test (GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_FORMAT_A420, 0);
}

GST_END_TEST;

GST_START_TEST (test_i420_gray8_to_a420)
{
  run_combine_test (GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_A420, 3);
}

GST_END_TEST;

GST_START_TEST (test_nv12_nv12_to_av12)
{
  run_combine_test (GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_NV12,
      GST_VIDEO_FORMAT_AV12, 0);
}

GST_END_TEST;

GST_START_TEST (test_nv12_gray8_to_av12)
{
  run_combine_test (GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_AV12, 0);
}

GST_END_TEST;

GST_START_TEST (test_i420_10le_i420_10le_to_a420_10le)
{
  run_combine_test (GST_VIDEO_FORMAT_I420_10LE, GST_VIDEO_FORMAT_I420_10LE,
      GST_VIDEO_FORMAT_A420_10LE, 0);
}

GST_END_TEST;

GST_START_TEST (test_unsupported_format_pair)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (GST_VIDEO_FORMAT_I420_10LE);
  GstCaps *alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY8);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420_10LE, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_GRAY8, 0);

  set_input_caps (fixture, sink_caps, alpha_caps);

  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad, alpha_buffer),
      GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
      GST_FLOW_NOT_NEGOTIATED);
  fail_unless_equals_int (n_output_buffers (), 0);

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_color_range_mismatch)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps_with_range (GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_RANGE_16_235);
  GstCaps *alpha_caps = make_caps_with_range (GST_VIDEO_FORMAT_I420,
      GST_VIDEO_COLOR_RANGE_0_255);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);

  set_input_caps (fixture, sink_caps, alpha_caps);

  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad, alpha_buffer),
      GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
      GST_FLOW_NOT_NEGOTIATED);
  fail_unless_equals_int (n_output_buffers (), 0);

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_gap_reuses_previous_alpha_buffer)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (GST_VIDEO_FORMAT_I420);
  GstCaps *alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY8);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_GRAY8, 0);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstBuffer *output;

  set_input_caps (fixture, sink_caps, alpha_caps);
  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad,
          gst_buffer_ref (alpha_buffer)), GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
      GST_FLOW_OK);
  output = pop_output_buffer ();
  gst_buffer_unref (output);

  fail_unless (gst_pad_push_event (fixture->alpha_srcpad,
          gst_event_new_gap (GST_SECOND, GST_SECOND)));
  sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad,
          gst_buffer_ref (sink_buffer)), GST_FLOW_OK);
  output = pop_output_buffer ();
  assert_output_buffer (output, sink_buffer, 0, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_FORMAT_A420);
  gst_buffer_unref (output);
  gst_buffer_unref (sink_buffer);

  gst_buffer_unref (alpha_buffer);
  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_initial_gap_fails)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (GST_VIDEO_FORMAT_I420);
  GstCaps *alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY8);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);

  set_input_caps (fixture, sink_caps, alpha_caps);

  fail_unless (gst_pad_push_event (fixture->alpha_srcpad,
          gst_event_new_gap (0, GST_SECOND)));
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
      GST_FLOW_ERROR);
  fail_unless_equals_int (n_output_buffers (), 0);

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_allocation_query_strips_pool)
{
  AllocationQueryData data;
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstQuery *query = gst_query_new_allocation (NULL, FALSE);
  GstBufferPool *pool = NULL;
  guint size = 0, min_buffers = 0, max_buffers = 0;

  data.pool = gst_buffer_pool_new ();
  data.size = 4096;
  data.min_buffers = 2;
  data.max_buffers = 7;

  g_object_set_data (G_OBJECT (fixture->output_sinkpad),
      "allocation-query-data", &data);
  gst_pad_set_query_function (fixture->output_sinkpad, allocation_query_func);

  fail_unless (gst_pad_peer_query (fixture->sink_srcpad, query));
  fail_unless_equals_int (gst_query_get_n_allocation_pools (query), 1);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size,
      &min_buffers, &max_buffers);
  fail_unless (pool == NULL);
  fail_unless_equals_int (size, data.size);
  fail_unless_equals_int (min_buffers, data.min_buffers);
  fail_unless_equals_int (max_buffers, data.max_buffers);

  gst_query_unref (query);
  gst_object_unref (data.pool);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

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

static GstSample *
pull_encoded_sample (const gchar * launchline)
{
  GError *error = NULL;
  GstElement *pipeline = gst_parse_launch (launchline, &error);
  GstElement *appsink;
  GstStateChangeReturn state_ret;
  GstSample *sample;
  GstBus *bus;
  GstMessage *msg;

  fail_unless (pipeline != NULL, "Failed to parse pipeline: %s",
      error ? error->message : "unknown error");
  g_clear_error (&error);

  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_unless (appsink != NULL);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  fail_unless (state_ret == GST_STATE_CHANGE_SUCCESS ||
      state_ret == GST_STATE_CHANGE_ASYNC);

  sample = gst_app_sink_try_pull_sample (GST_APP_SINK (appsink),
      10 * GST_SECOND);
  fail_unless (sample != NULL);

  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);
  if (msg) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    fail ("Encoding pipeline failed: %s (%s)", err->message,
        debug ? debug : "no debug");
    g_clear_error (&err);
    g_free (debug);
    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (appsink);
  gst_object_unref (pipeline);

  return sample;
}

static void
run_alpha_decode_bin_smoke (const gchar * element_name,
    const gchar * encoded_caps, const gchar * launchline)
{
  GstSample *main_sample = pull_encoded_sample (launchline);
  GstSample *alpha_sample = pull_encoded_sample (launchline);
  GstBuffer *main_buffer =
      gst_buffer_copy (gst_sample_get_buffer (main_sample));
  GstBuffer *alpha_buffer =
      gst_buffer_copy (gst_sample_get_buffer (alpha_sample));
  GstCaps *caps = gst_caps_from_string (encoded_caps);
  GstHarness *h = gst_harness_new (element_name);
  GstBuffer *out_buffer;
  GstVideoInfo out_info;

  fail_unless (main_buffer != NULL);
  fail_unless (alpha_buffer != NULL);
  fail_unless (gst_buffer_add_video_codec_alpha_meta (main_buffer,
          alpha_buffer) != NULL);

  fail_unless (caps != NULL);
  gst_harness_set_src_caps (h, caps);
  out_buffer = gst_harness_push_and_pull (h, main_buffer);
  fail_unless (out_buffer != NULL);

  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&out_info, caps));
  fail_unless (GST_VIDEO_FORMAT_INFO_HAS_ALPHA (out_info.finfo));
  gst_caps_unref (caps);
  gst_buffer_unref (out_buffer);
  gst_harness_teardown (h);

  gst_sample_unref (main_sample);
  gst_sample_unref (alpha_sample);
}

GST_START_TEST (test_vp8alphadecodebin_smoke)
{
  const gchar *launchline =
      "videotestsrc num-buffers=1 pattern=smpte ! "
      "video/x-raw,format=I420,width=16,height=16,framerate=1/1 ! "
      "vp8enc deadline=1 ! appsink name=sink sync=false";
  const gchar *required[] = { "vp8enc", "vp8dec" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  run_alpha_decode_bin_smoke ("vp8alphadecodebin",
      "video/x-vp8,codec-alpha=(boolean)true", launchline);
}

GST_END_TEST;

GST_START_TEST (test_vp9alphadecodebin_smoke)
{
  const gchar *launchline =
      "videotestsrc num-buffers=1 pattern=smpte ! "
      "video/x-raw,format=I420,width=16,height=16,framerate=1/1 ! "
      "vp9enc deadline=1 cpu-used=8 ! vp9parse ! "
      "video/x-vp9,alignment=(string)super-frame ! "
      "appsink name=sink sync=false";
  const gchar *required[] = { "vp9enc", "vp9dec", "vp9parse" };

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  run_alpha_decode_bin_smoke ("vp9alphadecodebin",
      "video/x-vp9,codec-alpha=(boolean)true,alignment=(string)super-frame",
      launchline);
}

GST_END_TEST;

static Suite *
alphacombine_suite (void)
{
  Suite *s = suite_create ("alphacombine");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 20);
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_i420_i420_to_a420);
  tcase_add_test (tc_chain, test_i420_gray8_to_a420);
  tcase_add_test (tc_chain, test_nv12_nv12_to_av12);
  tcase_add_test (tc_chain, test_nv12_gray8_to_av12);
  tcase_add_test (tc_chain, test_i420_10le_i420_10le_to_a420_10le);
  tcase_add_test (tc_chain, test_unsupported_format_pair);
  tcase_add_test (tc_chain, test_color_range_mismatch);
  tcase_add_test (tc_chain, test_gap_reuses_previous_alpha_buffer);
  tcase_add_test (tc_chain, test_initial_gap_fails);
  tcase_add_test (tc_chain, test_allocation_query_strips_pool);
  tcase_add_test (tc_chain, test_vp8alphadecodebin_smoke);
  tcase_add_test (tc_chain, test_vp9alphadecodebin_smoke);

  return s;
}

GST_CHECK_MAIN (alphacombine);
