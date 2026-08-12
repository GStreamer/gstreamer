/* GStreamer unit test for onnxinference
 *
 * Copyright (C) 2026 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.com>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>

#include "inference-utils.h"

#define TEST_WIDTH 4
#define TEST_HEIGHT 4
#define TEST_NUM_PIXELS (TEST_WIDTH * TEST_HEIGHT)
#define TEST_NUM_CHANNELS 3

static void assert_model_fails_to_start (const gchar * model_name);

#define ONNX_TEST_ASSERT_TENSOR_VALUES_F32(tensor, expected, n_values, epsilon) \
  assert_tensor_values_f32 ((tensor), (expected), (n_values), \
      (epsilon), __FILE__, __LINE__)

#define ONNX_TEST_ASSERT_TENSOR_VALUES_I32(tensor, expected, n_values) \
  assert_tensor_values_i32 ((tensor), (expected), (n_values), \
      __FILE__, __LINE__)

static GstHarness *
harness_new_with_model (const gchar * model_path)
{
  gchar *launch = g_strdup_printf ("onnxinference model-file=%s", model_path);
  GstHarness *h = gst_harness_new_parse (launch);

  gst_harness_play (h);
  g_free (launch);

  return h;
}

/* Test that different per-channel normalization ranges (0-255, 0-1, -1 to 1, mixed) produce the correct scaled float values. */
GST_START_TEST (test_normalization_variants)
{
  const struct
  {
    const gchar *ranges;
    gfloat r, g, b;
  } tests[] = {
    {
          "0.0,255.0;0.0,255.0;0.0,255.0",
        11.f, 22.f, 33.f}, {
          "0.0,1.0;0.0,1.0;0.0,1.0",
        11.f / 255.f, 22.f / 255.f, 33.f / 255.f}, {
          "-1.0,1.0;-1.0,1.0;-1.0,1.0",
          11.f * (2.f / 255.f) - 1.f, 22.f * (2.f / 255.f) - 1.f,
        33.f * (2.f / 255.f) - 1.f}, {
          "0.0,255.0;-1.0,1.0;0.0,1.0",
        11.f, 22.f * (2.f / 255.f) - 1.f, 33.f / 255.f},
  };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
        "onnxinference", "flatten_float32in_float32out.onnx",
        tests[i].ranges);
    GstHarness *h = harness_new_with_model (tmp_model);
    GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB,
        TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
    GstBuffer *out;
    GstTensorMeta *tmeta;
    const GstTensor *tensor;
    gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];

    gst_harness_set_src_caps_str (h,
        "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

    out = gst_harness_push_and_pull (h, in);
    fail_unless (out);

    fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
    tmeta = gst_buffer_get_tensor_meta (out);
    fail_unless_equals_int (tmeta->num_tensors, 1);
    tensor = gst_tensor_meta_get (tmeta, 0);
    fail_unless (tensor != NULL);

    fill_expected_flat_rgb_f32 (expected, TEST_NUM_PIXELS,
        tests[i].r, tests[i].g, tests[i].b);
    ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
        G_N_ELEMENTS (expected), 1e-5f);

    gst_buffer_unref (out);
    gst_harness_teardown (h);
    cleanup_temp_model (tmp_model);
  }
}

GST_END_TEST;

/* Test that a model with an int32 output produces a correctly typed tensor with the right values. */
GST_START_TEST (test_output_int32)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "int32out.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gint32 expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_INT32);
  fail_unless_equals_int ((gint) tensor->num_dims, 4);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 4);
  fail_unless_equals_int ((gint) tensor->dims[2], 4);
  fail_unless_equals_int ((gint) tensor->dims[3], 3);

  fill_expected_flat_rgb_i32 (expected, TEST_NUM_PIXELS, 11, 22, 33);
  ONNX_TEST_ASSERT_TENSOR_VALUES_I32 (tensor, expected,
      G_N_ELEMENTS (expected));

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that a model with a uint8 output does not fail to reach PAUSED state. */
GST_START_TEST (test_output_uint8_rejected)
{
  gchar *model = g_build_filename (GST_ONNX_TEST_DATA_PATH,
      "uint8out.onnx", NULL);
  GstElement *e = gst_element_factory_make ("onnxinference", NULL);

  g_object_set (e, "model-file", model, NULL);
  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      == GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
  g_free (model);
}

GST_END_TEST;

/* Test that a model with a float64 output does not fail to reach PAUSED state. */
GST_START_TEST (test_output_float64_rejected)
{
  gchar *model = g_build_filename (GST_ONNX_TEST_DATA_PATH,
      "float64out.onnx", NULL);
  GstElement *e = gst_element_factory_make ("onnxinference", NULL);

  g_object_set (e, "model-file", model, NULL);
  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      == GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
  g_free (model);
}

GST_END_TEST;

/* Test that a model with a dynamic batch dimension is handled correctly. */
GST_START_TEST (test_dynamic_batch)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "dynamic_batch.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);

  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 48);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test a model whose input is a 3-D CHW tensor, verifying the channel plane layout is preserved in the output. */
GST_START_TEST (test_3d_input)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "flatten_3d_float32.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGBP,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[3 * 16];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGBP,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 3);
  fail_unless_equals_int ((gint) tensor->dims[1], 16);

  fill_expected_chw_rgb_f32 (expected, TEST_NUM_PIXELS, 11.f, 22.f, 33.f);
  ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test a model with two outputs in different dimension orders (row-major and col-major), verifying tensor IDs and values. */
GST_START_TEST (test_multi_output_tensor_id_and_dims_order)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "multi_output.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  const GstTensor *tensor_by_id;
  gfloat expected_flat[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  gfloat expected_chw[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  guint i;
  gboolean seen_output_0 = FALSE;
  gboolean seen_output_1 = FALSE;

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);
  fill_expected_flat_rgb_f32 (expected_flat, TEST_NUM_PIXELS, 11.f, 22.f, 33.f);
  fill_expected_chw_rgb_f32 (expected_chw, TEST_NUM_PIXELS, 11.f, 22.f, 33.f);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 2);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-2")) == NULL);

  for (i = 0; i < tmeta->num_tensors; i++) {
    tensor = gst_tensor_meta_get (tmeta, i);
    fail_unless (tensor != NULL);
    fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);

    if (tensor->id == g_quark_from_static_string ("output-0")) {
      seen_output_0 = TRUE;
      fail_unless_equals_int ((gint) tensor->num_dims, 2);
      fail_unless_equals_int ((gint) tensor->dims[0], 1);
      fail_unless_equals_int ((gint) tensor->dims[1], 48);
      fail_unless_equals_int (tensor->dims_order,
          GST_TENSOR_DIM_ORDER_ROW_MAJOR);
      ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected_flat,
          G_N_ELEMENTS (expected_flat), 1e-6f);
    } else if (tensor->id == g_quark_from_static_string ("output-1")) {
      seen_output_1 = TRUE;
      fail_unless_equals_int ((gint) tensor->num_dims, 3);
      fail_unless_equals_int ((gint) tensor->dims[0], 1);
      fail_unless_equals_int ((gint) tensor->dims[1], 3);
      fail_unless_equals_int ((gint) tensor->dims[2], 16);
      fail_unless_equals_int (tensor->dims_order,
          GST_TENSOR_DIM_ORDER_COL_MAJOR);
      ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected_chw,
          G_N_ELEMENTS (expected_chw), 1e-6f);
    }
  }

  fail_unless (seen_output_0);
  fail_unless (seen_output_1);

  tensor_by_id = gst_tensor_meta_get_by_id (tmeta,
      g_quark_from_static_string ("output-0"));
  fail_unless (tensor_by_id != NULL);
  fail_unless_equals_int (tensor_by_id->id,
      g_quark_from_static_string ("output-0"));

  tensor_by_id = gst_tensor_meta_get_by_id (tmeta,
      g_quark_from_static_string ("output-1"));
  fail_unless (tensor_by_id != NULL);
  fail_unless_equals_int (tensor_by_id->id,
      g_quark_from_static_string ("output-1"));

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that an RGB input reordered to planar CHW layout by the model produces correct float32 channel-plane output. */
GST_START_TEST (test_planar_chw_input)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "planar_chw.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGBP,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGBP,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);

  fail_unless_equals_int ((gint) tensor->num_dims, 4);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 3);
  fail_unless_equals_int ((gint) tensor->dims[2], 4);
  fail_unless_equals_int ((gint) tensor->dims[3], 4);

  fill_expected_chw_rgb_f32 (expected, TEST_NUM_PIXELS, 11.f, 22.f, 33.f);
  ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test a GRAY8 input with a single-channel model, verifying the grayscale plane is correctly flattened. */
GST_START_TEST (test_gray8_input)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "flatten_gray.onnx",
      "0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_gray_buffer (GST_VIDEO_FORMAT_GRAY8,
      NULL, TEST_WIDTH, TEST_HEIGHT, 42);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=GRAY8,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 16);

  fill_expected_gray_f32 (expected, TEST_NUM_PIXELS, 42.f);
  ONNX_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

static void
assert_model_fails_to_start (const gchar * model_name)
{
  gchar *model = g_build_filename (GST_ONNX_TEST_DATA_PATH,
      model_name, NULL);
  GstElement *e = gst_element_factory_make ("onnxinference", NULL);

  g_object_set (e, "model-file", model, NULL);
  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      != GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
  g_free (model);
}

/* Test that models with unsupported dimensions or corrupt data fail to reach PAUSED state. */
GST_START_TEST (test_invalid_model_shapes_and_files)
{
  assert_model_fails_to_start ("invalid_dims_1d.onnx");
  assert_model_fails_to_start ("invalid_dims_5d.onnx");
  assert_model_fails_to_start ("invalid_dims_channels_2.onnx");
  assert_model_fails_to_start ("corrupt_model.onnx");
  assert_model_fails_to_start ("multi_input_two_tensors.onnx");
}

GST_END_TEST;

/* Test that a missing model file does not prevent cycling back to NULL from READY. */
GST_START_TEST (test_missing_model_and_state_cycle)
{
  GstElement *e = gst_element_factory_make ("onnxinference", NULL);

  g_object_set (e, "model-file", "/tmp/this-file-does-not-exist.onnx", NULL);

  fail_unless (gst_element_set_state (e, GST_STATE_READY)
      != GST_STATE_CHANGE_FAILURE);
  gst_element_set_state (e, GST_STATE_NULL);

  fail_unless (gst_element_set_state (e, GST_STATE_READY)
      != GST_STATE_CHANGE_FAILURE);
  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
}

GST_END_TEST;

/* Test that PTS, DTS, duration and the DISCONT flag are forwarded unchanged from the input buffer to the output. */
GST_START_TEST (test_timestamp_and_flags_propagation)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "flatten_float32in_float32out.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB,
      TEST_WIDTH, TEST_HEIGHT, 11, 22, 33, 55);
  GstBuffer *out;

  GST_BUFFER_PTS (in) = 123 * GST_MSECOND;
  GST_BUFFER_DTS (in) = 122 * GST_MSECOND;
  GST_BUFFER_DURATION (in) = 33 * GST_MSECOND;
  GST_BUFFER_FLAG_SET (in, GST_BUFFER_FLAG_DISCONT);

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless_equals_uint64 (GST_BUFFER_PTS (out), 123 * GST_MSECOND);
  fail_unless_equals_uint64 (GST_BUFFER_DTS (out), 122 * GST_MSECOND);
  fail_unless_equals_uint64 (GST_BUFFER_DURATION (out), 33 * GST_MSECOND);
  fail_unless (GST_BUFFER_FLAG_IS_SET (out, GST_BUFFER_FLAG_DISCONT));

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that caps queries return non-empty results and that accept-caps correctly accepts RGB but rejects I420. */
GST_START_TEST (test_transform_caps_and_accept_caps)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "flatten_uint8in_float32out.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstPad *sinkpad = gst_element_get_static_pad (h->element, "sink");
  GstCaps *filter = gst_caps_from_string ("video/x-raw,format=RGB");
  GstCaps *caps = gst_pad_query_caps (sinkpad, filter);

  fail_unless (caps != NULL);
  fail_if (gst_caps_is_empty (caps));

  gst_caps_unref (caps);
  gst_caps_unref (filter);

  caps =
      gst_caps_from_string
      ("video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
  fail_unless (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps =
      gst_caps_from_string
      ("video/x-raw,format=I420,width=4,height=4,framerate=30/1");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  gst_object_unref (sinkpad);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that caps whose width or height do not match the model's expected dimensions are rejected. */
GST_START_TEST (test_accept_caps_dimension_mismatch)
{
  gchar *tmp_model = setup_model_with_ranges (GST_ONNX_TEST_DATA_PATH,
      "onnxinference", "flatten_uint8in_float32out.onnx",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstPad *sinkpad = gst_element_get_static_pad (h->element, "sink");
  GstCaps *caps;

  caps =
      gst_caps_from_string
      ("video/x-raw,format=RGB,width=1,height=4,framerate=30/1");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  caps =
      gst_caps_from_string
      ("video/x-raw,format=RGB,width=4,height=1,framerate=30/1");
  fail_if (gst_pad_query_accept_caps (sinkpad, caps));
  gst_caps_unref (caps);

  gst_object_unref (sinkpad);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

static Suite *
onnxinference_suite (void)
{
  Suite *s = suite_create ("onnxinference");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_normalization_variants);
  tcase_add_test (tc, test_output_int32);
  tcase_add_test (tc, test_output_uint8_rejected);
  tcase_add_test (tc, test_output_float64_rejected);
  tcase_add_test (tc, test_dynamic_batch);
  tcase_add_test (tc, test_3d_input);
  tcase_add_test (tc, test_multi_output_tensor_id_and_dims_order);
  tcase_add_test (tc, test_planar_chw_input);
  tcase_add_test (tc, test_gray8_input);
  tcase_add_test (tc, test_invalid_model_shapes_and_files);
  tcase_add_test (tc, test_missing_model_and_state_cycle);
  tcase_add_test (tc, test_timestamp_and_flags_propagation);
  tcase_add_test (tc, test_transform_caps_and_accept_caps);
  tcase_add_test (tc, test_accept_caps_dimension_mismatch);

  return s;
}

GST_CHECK_MAIN (onnxinference);
