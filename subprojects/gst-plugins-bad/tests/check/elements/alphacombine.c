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

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>

static const guint test_width = 16;
static const guint test_height = 8;

static GstStaticPadTemplate sink_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate alpha_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate output_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
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
  GstAllocator *allocator;
  GstAllocationParams params;
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
    gst_query_add_allocation_param (query, data->allocator, &data->params);
    return TRUE;
  }

  return gst_pad_query_default (pad, parent, query);
}

static gboolean
caps_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstCaps *caps = g_object_get_data (G_OBJECT (pad), "caps-query-caps");

  if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS && caps) {
    GstCaps *filter, *result;

    gst_query_parse_caps (query, &filter);
    if (filter)
      result = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    else
      result = gst_caps_ref (caps);

    gst_query_set_caps_result (query, result);
    gst_caps_unref (result);

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

static GstCaps *
make_caps_with_feature (GstVideoFormat format, const gchar * feature)
{
  GstCaps *caps = make_caps (format);

  gst_caps_set_features (caps, 0,
      gst_caps_features_new_single_static_str (feature));

  return caps;
}

static GstCaps *
make_caps_with_features (GstVideoFormat format, const gchar * feature1,
    const gchar * feature2)
{
  GstCaps *caps = make_caps (format);

  gst_caps_set_features (caps, 0,
      gst_caps_features_new_static_str (feature1, feature2, NULL));

  return caps;
}

static GstCaps *
make_gl_caps (GstVideoFormat format, const gchar * texture_target)
{
  GstCaps *caps = make_caps_with_feature (format, "memory:GLMemory");

  gst_caps_set_simple (caps, "texture-target", G_TYPE_STRING,
      texture_target, NULL);

  return caps;
}

static GstCaps *
make_vulkan_image_caps (GstVideoFormat format)
{
  return make_caps_with_feature (format, "memory:VulkanImage");
}

static GstCaps *
make_iosurface_caps (GstVideoFormat format)
{
  return make_caps_with_feature (format, "memory:IOSurface");
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
assert_output_caps_features (AlphaCombineFixture * fixture,
    GstVideoFormat format, const GstCapsFeatures * expected_features,
    const gchar * expected_texture_target)
{
  GstCaps *caps;
  GstVideoInfo info;
  const GstCapsFeatures *features;
  const GstStructure *structure;

  caps = gst_pad_get_current_caps (fixture->output_sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_video_info_from_caps (&info, caps));
  fail_unless_equals_int (GST_VIDEO_INFO_FORMAT (&info), format);

  features = gst_caps_get_features (caps, 0);
  fail_unless (gst_caps_features_is_equal (features, expected_features));

  structure = gst_caps_get_structure (caps, 0);
  if (expected_texture_target) {
    const gchar *texture_target =
        gst_structure_get_string (structure, "texture-target");

    fail_unless (texture_target != NULL);
    fail_unless_equals_string (texture_target, expected_texture_target);
  }

  gst_caps_unref (caps);
}

static GstMemory *
find_plane_memory (GstBuffer * buffer, gsize offset)
{
  guint idx = 0, length = 0;
  gsize skip = 0;

  fail_unless (gst_buffer_find_memory (buffer, offset, 1, &idx, &length,
          &skip));

  return gst_buffer_peek_memory (buffer, idx);
}

static void
assert_output_buffer (GstBuffer * output, GstBuffer * sink_buffer,
    GstBuffer * alpha_buffer, guint alpha_plane_memory_prefix,
    GstVideoFormat sink_format, GstVideoFormat src_format)
{
  GstVideoMeta *meta = gst_buffer_get_video_meta (output);
  GstVideoMeta *alpha_meta = gst_buffer_get_video_meta (alpha_buffer);
  guint alpha_plane_idx;
  guint sink_n_mem, output_n_mem;
  GstMemory *expected_alpha_mem;

  fail_unless (meta != NULL);
  fail_unless (alpha_meta != NULL);

  fail_unless_equals_int (meta->format, src_format);
  fail_unless_equals_int (meta->width, test_width);
  fail_unless_equals_int (meta->height, test_height);

  alpha_plane_idx = n_planes_for_format (sink_format);
  fail_unless_equals_int (meta->n_planes, alpha_plane_idx + 1);
  fail_unless_equals_int64 (meta->offset[alpha_plane_idx],
      gst_buffer_get_size (sink_buffer) + alpha_plane_memory_prefix);
  sink_n_mem = gst_buffer_n_memory (sink_buffer);
  output_n_mem = gst_buffer_n_memory (output);
  fail_unless_equals_int (output_n_mem, sink_n_mem + 1);

  for (guint i = 0; i < sink_n_mem; i++)
    fail_unless (gst_buffer_peek_memory (output, i) ==
        gst_buffer_peek_memory (sink_buffer, i));

  expected_alpha_mem =
      find_plane_memory (alpha_buffer, alpha_meta->offset[GST_VIDEO_COMP_Y]);
  fail_unless (gst_buffer_peek_memory (output,
          sink_n_mem) == expected_alpha_mem);

  for (guint i = 0; i < alpha_plane_idx; i++)
    fail_unless (find_plane_memory (output, meta->offset[i]) ==
        find_plane_memory (sink_buffer, meta->offset[i]));

  fail_unless (find_plane_memory (output, meta->offset[alpha_plane_idx]) ==
      expected_alpha_mem);

  assert_parent_buffer_meta_count (output, 2);
}

static void
assert_yv12_planes_reordered_to_a420 (GstBuffer * output,
    GstBuffer * sink_buffer)
{
  GstVideoMeta *out_meta = gst_buffer_get_video_meta (output);
  GstVideoMeta *sink_meta = gst_buffer_get_video_meta (sink_buffer);

  fail_unless (out_meta != NULL);
  fail_unless (sink_meta != NULL);
  fail_unless_equals_int (out_meta->format, GST_VIDEO_FORMAT_A420);
  fail_unless_equals_uint64 (out_meta->offset[1], sink_meta->offset[2]);
  fail_unless_equals_uint64 (out_meta->offset[2], sink_meta->offset[1]);
  fail_unless_equals_int (out_meta->stride[1], sink_meta->stride[2]);
  fail_unless_equals_int (out_meta->stride[2], sink_meta->stride[1]);
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
  assert_output_buffer (output, sink_buffer, alpha_buffer,
      alpha_plane_memory_prefix, sink_format, expected_format);
  gst_buffer_unref (output);

  gst_buffer_unref (sink_buffer);
  gst_buffer_unref (alpha_buffer);
  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_START_TEST (test_yv12_to_a420_reorders_uv_planes)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (GST_VIDEO_FORMAT_YV12);
  GstCaps *alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY8);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_YV12, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_GRAY8, 0);
  GstBuffer *output;

  set_input_caps (fixture, sink_caps, alpha_caps);

  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad, alpha_buffer),
      GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad,
          gst_buffer_ref (sink_buffer)), GST_FLOW_OK);

  assert_output_caps (fixture, GST_VIDEO_FORMAT_A420);
  output = pop_output_buffer ();
  assert_output_buffer (output, sink_buffer, alpha_buffer, 0,
      GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_A420);
  assert_yv12_planes_reordered_to_a420 (output, sink_buffer);
  gst_buffer_unref (output);

  gst_buffer_unref (sink_buffer);
  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_8_bit_planar_formats)
{
  run_combine_test (GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_I420,
      GST_VIDEO_FORMAT_A420, 0);
  run_combine_test (GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_A420, 3);
  run_combine_test (GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_NV12,
      GST_VIDEO_FORMAT_AV12, 0);
  run_combine_test (GST_VIDEO_FORMAT_NV12, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_AV12, 0);
  run_combine_test (GST_VIDEO_FORMAT_YV12, GST_VIDEO_FORMAT_YV12,
      GST_VIDEO_FORMAT_A420, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_Y42B,
      GST_VIDEO_FORMAT_A422, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y42B, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_A422, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_Y444,
      GST_VIDEO_FORMAT_A444, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_A444, 0);
  run_combine_test (GST_VIDEO_FORMAT_GBR, GST_VIDEO_FORMAT_GRAY8,
      GST_VIDEO_FORMAT_GBRA, 0);
}

GST_END_TEST;

GST_START_TEST (test_10_bit_planar_formats)
{
  run_combine_test (GST_VIDEO_FORMAT_I420_10LE, GST_VIDEO_FORMAT_I420_10LE,
      GST_VIDEO_FORMAT_A420_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I420_10LE, GST_VIDEO_FORMAT_GRAY10_LE16,
      GST_VIDEO_FORMAT_A420_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I420_10BE, GST_VIDEO_FORMAT_I420_10BE,
      GST_VIDEO_FORMAT_A420_10BE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I422_10LE, GST_VIDEO_FORMAT_I422_10LE,
      GST_VIDEO_FORMAT_A422_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I422_10LE, GST_VIDEO_FORMAT_GRAY10_LE16,
      GST_VIDEO_FORMAT_A422_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I422_10BE, GST_VIDEO_FORMAT_I422_10BE,
      GST_VIDEO_FORMAT_A422_10BE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_10LE, GST_VIDEO_FORMAT_Y444_10LE,
      GST_VIDEO_FORMAT_A444_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_10LE, GST_VIDEO_FORMAT_GRAY10_LE16,
      GST_VIDEO_FORMAT_A444_10LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_10BE, GST_VIDEO_FORMAT_Y444_10BE,
      GST_VIDEO_FORMAT_A444_10BE, 0);
}

GST_END_TEST;

GST_START_TEST (test_12_bit_planar_formats)
{
  run_combine_test (GST_VIDEO_FORMAT_I420_12LE, GST_VIDEO_FORMAT_I420_12LE,
      GST_VIDEO_FORMAT_A420_12LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I420_12BE, GST_VIDEO_FORMAT_I420_12BE,
      GST_VIDEO_FORMAT_A420_12BE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I422_12LE, GST_VIDEO_FORMAT_I422_12LE,
      GST_VIDEO_FORMAT_A422_12LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_I422_12BE, GST_VIDEO_FORMAT_I422_12BE,
      GST_VIDEO_FORMAT_A422_12BE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_12LE, GST_VIDEO_FORMAT_Y444_12LE,
      GST_VIDEO_FORMAT_A444_12LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_12BE, GST_VIDEO_FORMAT_Y444_12BE,
      GST_VIDEO_FORMAT_A444_12BE, 0);
}

GST_END_TEST;

GST_START_TEST (test_16_bit_planar_formats)
{
  run_combine_test (GST_VIDEO_FORMAT_Y444_16LE, GST_VIDEO_FORMAT_Y444_16LE,
      GST_VIDEO_FORMAT_A444_16LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_16LE, GST_VIDEO_FORMAT_GRAY16_LE,
      GST_VIDEO_FORMAT_A444_16LE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_16BE, GST_VIDEO_FORMAT_Y444_16BE,
      GST_VIDEO_FORMAT_A444_16BE, 0);
  run_combine_test (GST_VIDEO_FORMAT_Y444_16BE, GST_VIDEO_FORMAT_GRAY16_BE,
      GST_VIDEO_FORMAT_A444_16BE, 0);
}

GST_END_TEST;

GST_START_TEST (test_unsupported_gray_packing)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps (GST_VIDEO_FORMAT_I420_10LE);
  GstCaps *alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY10_LE32);
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420_10LE, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_GRAY10_LE32, 0);

  set_input_caps (fixture, sink_caps, alpha_caps);

  /* GRAY10_LE32 is a packed layout, unlike the A420_10LE alpha plane. */
  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad, alpha_buffer),
      GST_FLOW_NOT_NEGOTIATED);
  fail_unless_equals_int (n_output_buffers (), 0);

  gst_buffer_unref (sink_buffer);
  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);

  fixture = setup_alphacombine ();
  sink_caps = make_caps (GST_VIDEO_FORMAT_I420_12LE);
  alpha_caps = make_caps (GST_VIDEO_FORMAT_GRAY16_LE);
  sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420_12LE, 0);
  alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_GRAY16_LE, 0);

  set_input_caps (fixture, sink_caps, alpha_caps);

  /* GRAY16_LE would require reinterpretation as a 12-bit alpha plane. */
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
  assert_output_buffer (output, sink_buffer, alpha_buffer, 0,
      GST_VIDEO_FORMAT_I420, GST_VIDEO_FORMAT_A420);
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
  AllocationQueryData data = { 0, };
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstQuery *query = gst_query_new_allocation (NULL, FALSE);
  GstBufferPool *pool = NULL;
  guint size = 0, min_buffers = 0, max_buffers = 0;

  data.pool = gst_buffer_pool_new ();
  data.allocator = gst_allocator_find (NULL);
  gst_allocation_params_init (&data.params);
  data.params.align = 63;
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
  fail_unless_equals_int (gst_query_get_n_allocation_params (query), 0);

  gst_query_unref (query);
  gst_clear_object (&data.allocator);
  gst_object_unref (data.pool);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_pad_templates_expose_supported_memory_backends)
{
  GstElementFactory *factory = gst_element_factory_find ("alphacombine");
  const GList *templates;

  fail_unless (factory != NULL);

  for (templates = gst_element_factory_get_static_pad_templates (factory);
      templates; templates = templates->next) {
    GstStaticPadTemplate *static_template = templates->data;
    GstCaps *caps = gst_static_caps_get (&static_template->static_caps);
    gboolean has_baseline = FALSE;
    gboolean has_gl_memory = FALSE;
    gboolean has_vulkan_image = FALSE;
    gboolean has_iosurface = FALSE;
    guint i;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      const GstCapsFeatures *features = gst_caps_get_features (caps, i);

      fail_if (gst_caps_features_is_any (features));
      if (gst_caps_features_get_size (features) == 0 ||
          gst_caps_features_contains (features,
              GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
        has_baseline = TRUE;

      if (gst_caps_features_contains (features, "memory:GLMemory"))
        has_gl_memory = TRUE;

      if (gst_caps_features_contains (features, "memory:VulkanImage"))
        has_vulkan_image = TRUE;

      if (gst_caps_features_contains (features, "memory:IOSurface"))
        has_iosurface = TRUE;
    }

    fail_unless (has_baseline);
    fail_unless (has_gl_memory);
    fail_unless (has_vulkan_image);
    fail_unless (has_iosurface);

    gst_caps_unref (caps);
  }

  gst_object_unref (factory);
}

GST_END_TEST;

static void
run_caps_feature_combine_test (GstCaps * sink_caps, GstCaps * alpha_caps,
    const GstCapsFeatures * expected_features,
    const gchar * expected_texture_target)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstBuffer *output;

  set_input_caps (fixture, sink_caps, alpha_caps);
  assert_output_caps_features (fixture, GST_VIDEO_FORMAT_A420,
      expected_features, expected_texture_target);

  fail_unless_equals_int (gst_pad_push (fixture->alpha_srcpad, alpha_buffer),
      GST_FLOW_OK);
  fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
      GST_FLOW_OK);
  output = pop_output_buffer ();
  gst_buffer_unref (output);

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

static void
run_caps_feature_rejection_test (GstCaps * sink_caps, GstCaps * alpha_caps)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstBuffer *sink_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstBuffer *alpha_buffer = make_video_buffer (GST_VIDEO_FORMAT_I420, 0);
  GstFlowReturn alpha_ret;

  set_input_caps (fixture, sink_caps, alpha_caps);

  alpha_ret = gst_pad_push (fixture->alpha_srcpad, alpha_buffer);
  if (alpha_ret == GST_FLOW_OK) {
    fail_unless_equals_int (gst_pad_push (fixture->sink_srcpad, sink_buffer),
        GST_FLOW_NOT_NEGOTIATED);

    fail_unless (gst_pad_push_event (fixture->alpha_srcpad,
            gst_event_new_flush_start ()));
    fail_unless (gst_pad_push_event (fixture->sink_srcpad,
            gst_event_new_flush_start ()));
  } else {
    fail_unless_equals_int (alpha_ret, GST_FLOW_NOT_NEGOTIATED);
    gst_buffer_unref (sink_buffer);
  }

  fail_unless_equals_int (gst_element_set_state (fixture->element,
          GST_STATE_READY), GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (n_output_buffers (), 0);

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_START_TEST (test_plain_and_system_memory_features_are_compatible)
{
  GstCapsFeatures *system_features =
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY);
  GstCapsFeatures *plain_features = gst_caps_features_new_empty ();

  run_caps_feature_combine_test (make_caps (GST_VIDEO_FORMAT_I420),
      make_caps (GST_VIDEO_FORMAT_I420), plain_features, NULL);
  run_caps_feature_combine_test (make_caps_with_feature (GST_VIDEO_FORMAT_I420,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY),
      make_caps_with_feature (GST_VIDEO_FORMAT_I420,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY), system_features, NULL);
  run_caps_feature_combine_test (make_caps (GST_VIDEO_FORMAT_I420),
      make_caps_with_feature (GST_VIDEO_FORMAT_I420,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY), plain_features, NULL);
  run_caps_feature_combine_test (make_caps_with_feature (GST_VIDEO_FORMAT_I420,
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY),
      make_caps (GST_VIDEO_FORMAT_I420), system_features, NULL);

  gst_caps_features_free (system_features);
  gst_caps_features_free (plain_features);
}

GST_END_TEST;

GST_START_TEST (test_vulkan_image_features_are_propagated)
{
  GstCapsFeatures *features =
      gst_caps_features_new_single_static_str ("memory:VulkanImage");

  run_caps_feature_combine_test (make_vulkan_image_caps
      (GST_VIDEO_FORMAT_I420), make_vulkan_image_caps (GST_VIDEO_FORMAT_I420),
      features, NULL);

  gst_caps_features_free (features);
}

GST_END_TEST;

GST_START_TEST (test_iosurface_features_are_propagated)
{
  GstCapsFeatures *features =
      gst_caps_features_new_single_static_str ("memory:IOSurface");

  run_caps_feature_combine_test (make_iosurface_caps (GST_VIDEO_FORMAT_I420),
      make_iosurface_caps (GST_VIDEO_FORMAT_I420), features, NULL);

  gst_caps_features_free (features);
}

GST_END_TEST;

GST_START_TEST (test_unsupported_memory_feature_is_rejected_late)
{
  run_caps_feature_rejection_test (make_caps_with_feature
      (GST_VIDEO_FORMAT_I420, "memory:TestMemory"),
      make_caps_with_feature (GST_VIDEO_FORMAT_I420, "memory:TestMemory"));
}

GST_END_TEST;

GST_START_TEST (test_gl_and_vulkan_image_mismatch_is_rejected_late)
{
  run_caps_feature_rejection_test (make_gl_caps (GST_VIDEO_FORMAT_I420, "2D"),
      make_vulkan_image_caps (GST_VIDEO_FORMAT_I420));
}

GST_END_TEST;

GST_START_TEST (test_meta_feature_mismatch_is_rejected)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps = make_caps_with_features (GST_VIDEO_FORMAT_I420,
      "memory:GLMemory", "meta:GstVideoOverlayComposition");
  GstCaps *alpha_caps = make_caps_with_feature (GST_VIDEO_FORMAT_I420,
      "memory:GLMemory");

  gst_caps_set_simple (sink_caps, "texture-target", G_TYPE_STRING, "2D", NULL);
  gst_caps_set_simple (alpha_caps, "texture-target", G_TYPE_STRING, "2D", NULL);

  fail_if (gst_pad_peer_query_accept_caps (fixture->sink_srcpad, sink_caps));
  fail_unless (gst_pad_peer_query_accept_caps (fixture->alpha_srcpad,
          alpha_caps));

  gst_caps_unref (sink_caps);
  gst_caps_unref (alpha_caps);
  cleanup_alphacombine (fixture);
}

GST_END_TEST;

GST_START_TEST (test_gl_memory_requires_matching_texture_target)
{
  GstCapsFeatures *features =
      gst_caps_features_new_single_static_str ("memory:GLMemory");

  run_caps_feature_combine_test (make_gl_caps (GST_VIDEO_FORMAT_I420, "2D"),
      make_gl_caps (GST_VIDEO_FORMAT_I420, "2D"), features, "2D");
  run_caps_feature_rejection_test (make_gl_caps (GST_VIDEO_FORMAT_I420, "2D"),
      make_gl_caps (GST_VIDEO_FORMAT_I420, "rectangle"));

  gst_caps_features_free (features);
}

GST_END_TEST;

static void
run_caps_query_propagates_downstream_test (GstCaps * downstream_caps,
    GstCaps * expected_sink_caps, GstCaps * expected_alpha_caps)
{
  AlphaCombineFixture *fixture = setup_alphacombine ();
  GstCaps *sink_caps;
  GstCaps *alpha_caps;
  GstCaps *unexpected_raw_caps = make_caps (GST_VIDEO_FORMAT_NV12);

  g_object_set_data (G_OBJECT (fixture->output_sinkpad), "caps-query-caps",
      downstream_caps);
  gst_pad_set_query_function (fixture->output_sinkpad, caps_query_func);

  sink_caps = gst_pad_peer_query_caps (fixture->sink_srcpad, NULL);
  fail_unless (gst_caps_can_intersect (sink_caps, expected_sink_caps));
  fail_if (gst_caps_can_intersect (sink_caps, unexpected_raw_caps));
  gst_caps_unref (sink_caps);

  alpha_caps = gst_pad_peer_query_caps (fixture->alpha_srcpad, NULL);
  fail_unless (gst_caps_can_intersect (alpha_caps, expected_alpha_caps));
  fail_if (gst_caps_can_intersect (alpha_caps, unexpected_raw_caps));
  gst_caps_unref (alpha_caps);

  g_object_set_data (G_OBJECT (fixture->output_sinkpad), "caps-query-caps",
      NULL);
  gst_caps_unref (unexpected_raw_caps);
  gst_caps_unref (expected_alpha_caps);
  gst_caps_unref (expected_sink_caps);
  gst_caps_unref (downstream_caps);
  cleanup_alphacombine (fixture);
}

GST_START_TEST (test_caps_query_propagates_downstream_glmemory)
{
  run_caps_query_propagates_downstream_test (make_gl_caps
      (GST_VIDEO_FORMAT_AV12, "rectangle"), make_gl_caps (GST_VIDEO_FORMAT_NV12,
          "rectangle"), make_gl_caps (GST_VIDEO_FORMAT_NV12, "rectangle"));
}

GST_END_TEST;

GST_START_TEST (test_caps_query_propagates_downstream_vulkan_image)
{
  run_caps_query_propagates_downstream_test (make_vulkan_image_caps
      (GST_VIDEO_FORMAT_AV12), make_vulkan_image_caps (GST_VIDEO_FORMAT_NV12),
      make_vulkan_image_caps (GST_VIDEO_FORMAT_NV12));
}

GST_END_TEST;

GST_START_TEST (test_caps_query_propagates_downstream_iosurface)
{
  run_caps_query_propagates_downstream_test (make_iosurface_caps
      (GST_VIDEO_FORMAT_AV12), make_iosurface_caps (GST_VIDEO_FORMAT_NV12),
      make_iosurface_caps (GST_VIDEO_FORMAT_NV12));
}

GST_END_TEST;

static Suite *
alphacombine_suite (void)
{
  Suite *s = suite_create ("alphacombine");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 20);
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_yv12_to_a420_reorders_uv_planes);
  tcase_add_test (tc_chain, test_8_bit_planar_formats);
  tcase_add_test (tc_chain, test_10_bit_planar_formats);
  tcase_add_test (tc_chain, test_12_bit_planar_formats);
  tcase_add_test (tc_chain, test_16_bit_planar_formats);
  tcase_add_test (tc_chain, test_unsupported_gray_packing);
  tcase_add_test (tc_chain, test_unsupported_format_pair);
  tcase_add_test (tc_chain, test_color_range_mismatch);
  tcase_add_test (tc_chain, test_gap_reuses_previous_alpha_buffer);
  tcase_add_test (tc_chain, test_initial_gap_fails);
  tcase_add_test (tc_chain, test_allocation_query_strips_pool);
  tcase_add_test (tc_chain,
      test_pad_templates_expose_supported_memory_backends);
  tcase_add_test (tc_chain,
      test_plain_and_system_memory_features_are_compatible);
  tcase_add_test (tc_chain, test_vulkan_image_features_are_propagated);
  tcase_add_test (tc_chain, test_iosurface_features_are_propagated);
  tcase_add_test (tc_chain, test_unsupported_memory_feature_is_rejected_late);
  tcase_add_test (tc_chain, test_gl_and_vulkan_image_mismatch_is_rejected_late);
  tcase_add_test (tc_chain, test_meta_feature_mismatch_is_rejected);
  tcase_add_test (tc_chain, test_gl_memory_requires_matching_texture_target);
  tcase_add_test (tc_chain, test_caps_query_propagates_downstream_glmemory);
  tcase_add_test (tc_chain, test_caps_query_propagates_downstream_vulkan_image);
  tcase_add_test (tc_chain, test_caps_query_propagates_downstream_iosurface);

  return s;
}

GST_CHECK_MAIN (alphacombine);
