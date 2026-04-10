/* GStreamer unit test for tfliteinference
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

#include <math.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#define TEST_WIDTH 4
#define TEST_HEIGHT 4
#define TEST_NUM_PIXELS (TEST_WIDTH * TEST_HEIGHT)
#define TEST_NUM_CHANNELS 3

static void
fill_expected_flat_rgb_f32 (gfloat * out, gfloat r, gfloat g, gfloat b)
{
  gsize i;

  for (i = 0; i < TEST_NUM_PIXELS; i++) {
    out[i * 3 + 0] = r;
    out[i * 3 + 1] = g;
    out[i * 3 + 2] = b;
  }
}

static void
fill_expected_chw_rgb_f32 (gfloat * out, gfloat r, gfloat g, gfloat b)
{
  gsize i;

  for (i = 0; i < TEST_NUM_PIXELS; i++) {
    out[0 * TEST_NUM_PIXELS + i] = r;
    out[1 * TEST_NUM_PIXELS + i] = g;
    out[2 * TEST_NUM_PIXELS + i] = b;
  }
}

static void
fill_expected_flat_rgb_u8 (guint8 * out, guint8 r, guint8 g, guint8 b)
{
  gsize i;

  for (i = 0; i < TEST_NUM_PIXELS; i++) {
    out[i * 3 + 0] = r;
    out[i * 3 + 1] = g;
    out[i * 3 + 2] = b;
  }
}

static void
fill_expected_flat_rgb_i8 (gint8 * out, gint8 r, gint8 g, gint8 b)
{
  gsize i;

  for (i = 0; i < TEST_NUM_PIXELS; i++) {
    out[i * 3 + 0] = r;
    out[i * 3 + 1] = g;
    out[i * 3 + 2] = b;
  }
}

static void
tflite_test_assert_tensor_values_f32 (const GstTensor * tensor,
    const gfloat * expected, gsize n_values, gfloat epsilon,
    const gchar * file, gint line)
{
  GstMapInfo map;
  const gfloat *actual;
  gsize i;

  fail_unless (tensor, "%s:%d tensor is NULL", file, line);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless (gst_buffer_map (tensor->data, &map, GST_MAP_READ),
      "%s:%d failed to map tensor data", file, line);
  fail_unless (map.size == n_values * sizeof (gfloat),
      "%s:%d tensor size=%zu expected=%zu", file, line, map.size,
      n_values * sizeof (gfloat));

  actual = (const gfloat *) map.data;
  for (i = 0; i < n_values; i++) {
    gfloat diff = fabsf (actual[i] - expected[i]);
    fail_unless (diff <= epsilon,
        "%s:%d value[%zu]=%.8f expected %.8f (diff %.8f > %.8f)",
        file, line, i, actual[i], expected[i], diff, epsilon);
  }

  gst_buffer_unmap (tensor->data, &map);
}

static void
tflite_test_assert_tensor_values_u8 (const GstTensor * tensor,
    const guint8 * expected, gsize n_values, const gchar * file, gint line)
{
  GstMapInfo map;
  const guint8 *actual;
  gsize i;

  fail_unless (tensor, "%s:%d tensor is NULL", file, line);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_UINT8);
  fail_unless (gst_buffer_map (tensor->data, &map, GST_MAP_READ),
      "%s:%d failed to map tensor data", file, line);
  fail_unless (map.size == n_values * sizeof (guint8),
      "%s:%d tensor size=%zu expected=%zu", file, line, map.size,
      n_values * sizeof (guint8));

  actual = (const guint8 *) map.data;
  for (i = 0; i < n_values; i++) {
    fail_unless (actual[i] == expected[i],
        "%s:%d value[%zu]=%u expected %u", file, line, i,
        (guint) actual[i], (guint) expected[i]);
  }

  gst_buffer_unmap (tensor->data, &map);
}

static void
tflite_test_assert_tensor_values_i8 (const GstTensor * tensor,
    const gint8 * expected, gsize n_values, const gchar * file, gint line)
{
  GstMapInfo map;
  const gint8 *actual;
  gsize i;

  fail_unless (tensor, "%s:%d tensor is NULL", file, line);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_INT8);
  fail_unless (gst_buffer_map (tensor->data, &map, GST_MAP_READ),
      "%s:%d failed to map tensor data", file, line);
  fail_unless (map.size == n_values * sizeof (gint8),
      "%s:%d tensor size=%zu expected=%zu", file, line, map.size,
      n_values * sizeof (gint8));

  actual = (const gint8 *) map.data;
  for (i = 0; i < n_values; i++) {
    fail_unless (actual[i] == expected[i],
        "%s:%d value[%zu]=%d expected %d", file, line, i,
        (gint) actual[i], (gint) expected[i]);
  }

  gst_buffer_unmap (tensor->data, &map);
}

static gchar *
setup_model_with_modelinfo (const gchar * base_model_name,
    const gchar * modelinfo_content)
{
  gchar *base_model =
      g_build_filename (GST_TFLITE_TEST_DATA_PATH, base_model_name, NULL);
  gchar *tmp_model = g_strdup_printf ("%s%ctfliteinference-%u-%s",
      g_get_tmp_dir (), G_DIR_SEPARATOR, g_random_int (), base_model_name);
  gchar *tmp_modelinfo = g_strdup_printf ("%s.modelinfo", tmp_model);
  GFile *src_file = g_file_new_for_path (base_model);
  GFile *dst_file = g_file_new_for_path (tmp_model);

  fail_unless (g_file_copy (src_file, dst_file, G_FILE_COPY_OVERWRITE, NULL,
          NULL, NULL, NULL));
  fail_unless (g_file_set_contents (tmp_modelinfo, modelinfo_content, -1,
          NULL));

  g_object_unref (src_file);
  g_object_unref (dst_file);
  g_free (base_model);
  g_free (tmp_modelinfo);

  return tmp_model;
}

static gchar *
setup_model_with_ranges (const gchar * base_model_name, const gchar * ranges)
{
  gchar *base_model =
      g_build_filename (GST_TFLITE_TEST_DATA_PATH, base_model_name, NULL);
  gchar *base_modelinfo = g_strdup_printf ("%s.modelinfo", base_model);
  GKeyFile *kf = g_key_file_new ();
  gchar **groups;
  gchar *data;
  gsize len;
  gsize i;
  gchar *tmp_model;

  g_key_file_set_list_separator (kf, ',');
  fail_unless (g_key_file_load_from_file (kf, base_modelinfo, G_KEY_FILE_NONE,
          NULL));

  groups = g_key_file_get_groups (kf, NULL);
  for (i = 0; groups[i]; i++) {
    gchar *dir = g_key_file_get_string (kf, groups[i], "dir", NULL);

    if (dir && g_strcmp0 (dir, "input") == 0)
      g_key_file_set_string (kf, groups[i], "ranges", ranges);

    g_free (dir);
  }
  g_strfreev (groups);

  data = g_key_file_to_data (kf, &len, NULL);
  tmp_model = setup_model_with_modelinfo (base_model_name, data);

  g_free (data);
  g_key_file_unref (kf);
  g_free (base_modelinfo);
  g_free (base_model);

  return tmp_model;
}

static void
cleanup_temp_model (gchar * model_path)
{
  gchar *modelinfo = g_strdup_printf ("%s.modelinfo", model_path);

  if (model_path)
    g_remove (model_path);
  if (modelinfo)
    g_remove (modelinfo);

  g_free (modelinfo);
  g_free (model_path);
}

static GstHarness *
harness_new_with_model (const gchar * model_path)
{
  gchar *launch = g_strdup_printf ("tfliteinference model-file=%s", model_path);
  GstHarness *h = gst_harness_new_parse (launch);

  gst_harness_play (h);
  g_free (launch);

  return h;
}

/* Pull all downstream events and return the last CAPS event's caps. */
static GstCaps *
pull_output_caps (GstHarness * h)
{
  GstEvent *event;
  GstCaps *caps = NULL;

  while ((event = gst_harness_try_pull_event (h)) != NULL) {
    if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
      GstCaps *c;
      gst_event_parse_caps (event, &c);
      gst_caps_replace (&caps, c);
    }
    gst_event_unref (event);
  }

  return caps;
}

typedef struct
{
  const gchar *id;
  const gchar *type;
  const gchar *dims_order;
  gint dims[8];
  gint n_dims;
} TfliteTestTensorInfo;

/*
 * Build the expected downstream caps for tfliteinference, mirroring exactly
 * how gsttfliteinference.c constructs its model_outcaps and then intersects it
 * with the incoming video caps.
 */
static GstCaps *
build_expected_output_caps (const gchar * format, gint width, gint height,
    gint fps_n, gint fps_d, const gchar * group_id,
    const TfliteTestTensorInfo * tensors, gint n_tensors)
{
  GstStructure *tensors_s;
  GValue v_tensors_set = G_VALUE_INIT;
  GstCaps *caps;
  gint i;

  tensors_s = gst_structure_new_empty ("tensorgroups");
  g_value_init (&v_tensors_set, GST_TYPE_UNIQUE_LIST);

  for (i = 0; i < n_tensors; i++) {
    GstStructure *tensor_desc;
    GValue val_dims = G_VALUE_INIT;
    GValue val = G_VALUE_INIT;
    GValue val_caps = G_VALUE_INIT;
    GstCaps *tensor_caps;
    gint j;

    tensor_desc = gst_structure_new_empty ("tensor/strided");
    gst_value_array_init (&val_dims, tensors[i].n_dims);
    g_value_init (&val, G_TYPE_INT);

    for (j = 0; j < tensors[i].n_dims; j++) {
      g_value_set_int (&val, tensors[i].dims[j]);
      gst_value_array_append_value (&val_dims, &val);
    }
    g_value_unset (&val);

    gst_structure_set (tensor_desc,
        "tensor-id", G_TYPE_STRING, tensors[i].id,
        "dims-order", G_TYPE_STRING, tensors[i].dims_order,
        "type", G_TYPE_STRING, tensors[i].type, NULL);
    gst_structure_take_value (tensor_desc, "dims", &val_dims);

    tensor_caps = gst_caps_new_full (tensor_desc, NULL);
    g_value_init (&val_caps, GST_TYPE_CAPS);
    gst_value_set_caps (&val_caps, tensor_caps);
    gst_caps_unref (tensor_caps);
    gst_value_unique_list_append_and_take_value (&v_tensors_set, &val_caps);
  }

  gst_structure_take_value (tensors_s, group_id, &v_tensors_set);

  caps = gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, format,
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
      "tensors", GST_TYPE_STRUCTURE, tensors_s, NULL);
  gst_structure_free (tensors_s);

  return caps;
}

/* Creates buffers with color R=11 G=22 B=33 A=55*/

static GstBuffer *
create_solid_color_buffer (GstVideoFormat format)
{
  GstVideoInfo info;
  GstBuffer *buf;
  GstMapInfo map;
  guint y, x;

  fail_unless (gst_video_info_set_format (&info, format, TEST_WIDTH,
          TEST_HEIGHT));

  buf = gst_buffer_new_and_alloc (info.size);
  fail_unless (gst_buffer_map (buf, &map, GST_MAP_WRITE));

  for (y = 0; y < TEST_HEIGHT; y++) {
    guint8 *r = map.data +
        GST_VIDEO_INFO_COMP_OFFSET (&info, GST_VIDEO_COMP_R) +
        GST_VIDEO_INFO_COMP_STRIDE (&info, GST_VIDEO_COMP_R) * y;
    guint8 *g = map.data +
        GST_VIDEO_INFO_COMP_OFFSET (&info, GST_VIDEO_COMP_G) +
        GST_VIDEO_INFO_COMP_STRIDE (&info, GST_VIDEO_COMP_G) * y;
    guint8 *b = map.data +
        GST_VIDEO_INFO_COMP_OFFSET (&info, GST_VIDEO_COMP_B) +
        GST_VIDEO_INFO_COMP_STRIDE (&info, GST_VIDEO_COMP_B) * y;
    guint8 *a = NULL;

    if (GST_VIDEO_INFO_HAS_ALPHA (&info))
      a = map.data +
          GST_VIDEO_INFO_COMP_OFFSET (&info, GST_VIDEO_COMP_A) +
          GST_VIDEO_INFO_COMP_STRIDE (&info, GST_VIDEO_COMP_A) * y;

    for (x = 0; x < TEST_WIDTH; x++) {
      r[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_R)] = 11;
      g[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_G)] = 22;
      b[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_B)] = 33;
      if (a)
        a[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_A)] = 55;
    }
  }

  gst_buffer_unmap (buf, &map);
  return buf;
}

#define TFLITE_TEST_ASSERT_TENSOR_VALUES_F32(tensor, expected, n_values, epsilon) \
  tflite_test_assert_tensor_values_f32 ((tensor), (expected), (n_values), \
      (epsilon), __FILE__, __LINE__)

#define TFLITE_TEST_ASSERT_TENSOR_VALUES_U8(tensor, expected, n_values) \
  tflite_test_assert_tensor_values_u8 ((tensor), (expected), (n_values), \
      __FILE__, __LINE__)

#define TFLITE_TEST_ASSERT_TENSOR_VALUES_I8(tensor, expected, n_values) \
  tflite_test_assert_tensor_values_i8 ((tensor), (expected), (n_values), \
      __FILE__, __LINE__)

/* Test that RGB, RGBA, BGR and BGRA input formats all produce correct float32 output tensors. */
GST_START_TEST (test_input_formats)
{
  const GstVideoFormat formats[] = {
    GST_VIDEO_FORMAT_RGB, GST_VIDEO_FORMAT_RGBA,
    GST_VIDEO_FORMAT_BGR, GST_VIDEO_FORMAT_BGRA
  };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (formats); i++) {
    gchar *tmp_model =
        setup_model_with_ranges ("flatten_float32in_float32out.tflite",
        "0.0,255.0;0.0,255.0;0.0,255.0");
    GstHarness *h = harness_new_with_model (tmp_model);
    GstBuffer *in = create_solid_color_buffer (formats[i]);
    GstBuffer *out;
    GstTensorMeta *tmeta;
    const GstTensor *tensor;
    gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
    gchar *caps_str;
    GstCaps *actual_caps, *expected_caps;
    const TfliteTestTensorInfo out_tensors[] = {
      {"output-0", "float32", "row-major", {1, 48}, 2}
    };

    caps_str = g_strdup_printf ("video/x-raw,format=%s,width=%d,height=%d,"
        "framerate=30/1", gst_video_format_to_string (formats[i]),
        TEST_WIDTH, TEST_HEIGHT);
    gst_harness_set_src_caps_str (h, caps_str);
    g_free (caps_str);

    out = gst_harness_push_and_pull (h, in);
    fail_unless (out);
    fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
    tmeta = gst_buffer_get_tensor_meta (out);
    fail_unless_equals_int (tmeta->num_tensors, 1);
    tensor = gst_tensor_meta_get (tmeta, 0);
    fail_unless (tensor != NULL);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-0")) == tensor);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-1")) == NULL);
    fail_unless_equals_int (tensor->id,
        g_quark_from_static_string ("output-0"));
    fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
    fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
    fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
    fail_unless_equals_int ((gint) tensor->num_dims, 2);
    fail_unless_equals_int ((gint) tensor->dims[0], 1);
    fail_unless_equals_int ((gint) tensor->dims[1], 48);

    actual_caps = pull_output_caps (h);
    expected_caps =
        build_expected_output_caps (gst_video_format_to_string (formats[i]),
        TEST_WIDTH, TEST_HEIGHT, 30, 1, "flatten_float32in_float32out-group",
        out_tensors, G_N_ELEMENTS (out_tensors));
    fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
    gst_caps_unref (actual_caps);
    gst_caps_unref (expected_caps);

    fill_expected_flat_rgb_f32 (expected, 11, 22, 33);
    TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
        G_N_ELEMENTS (expected), 1e-6f);

    gst_buffer_unref (out);
    gst_harness_teardown (h);
    cleanup_temp_model (tmp_model);
  }
}

GST_END_TEST;

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
    gchar *tmp_model =
        setup_model_with_ranges ("flatten_float32in_float32out.tflite",
        tests[i].ranges);
    GstHarness *h = harness_new_with_model (tmp_model);
    GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
    GstBuffer *out;
    GstTensorMeta *tmeta;
    const GstTensor *tensor;
    gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
    GstCaps *actual_caps, *expected_caps;
    const TfliteTestTensorInfo out_tensors[] = {
      {"output-0", "float32", "row-major", {1, 48}, 2}
    };

    gst_harness_set_src_caps_str (h,
        "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");

    out = gst_harness_push_and_pull (h, in);
    fail_unless (out);

    fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
    tmeta = gst_buffer_get_tensor_meta (out);
    fail_unless_equals_int (tmeta->num_tensors, 1);
    tensor = gst_tensor_meta_get (tmeta, 0);
    fail_unless (tensor != NULL);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-0")) == tensor);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-1")) == NULL);
    fail_unless_equals_int (tensor->id,
        g_quark_from_static_string ("output-0"));
    fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
    fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
    fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
    fail_unless_equals_int ((gint) tensor->num_dims, 2);
    fail_unless_equals_int ((gint) tensor->dims[0], 1);
    fail_unless_equals_int ((gint) tensor->dims[1], 48);

    actual_caps = pull_output_caps (h);
    expected_caps = build_expected_output_caps ("RGB",
        TEST_WIDTH, TEST_HEIGHT, 30, 1,
        "flatten_float32in_float32out-group",
        out_tensors, G_N_ELEMENTS (out_tensors));
    fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
    gst_caps_unref (actual_caps);
    gst_caps_unref (expected_caps);

    fill_expected_flat_rgb_f32 (expected, tests[i].r, tests[i].g, tests[i].b);
    TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
        G_N_ELEMENTS (expected), 1e-5f);

    gst_buffer_unref (out);
    gst_harness_teardown (h);
    cleanup_temp_model (tmp_model);
  }
}

GST_END_TEST;

/* Test that models with uint8 and int8 output tensors produce the correct data type in the tensor meta. */
GST_START_TEST (test_output_dtypes)
{
  const struct
  {
    const gchar *model_name;
    GstTensorDataType expected_type;
  } tests[] = {
    {
        "uint8out.tflite", GST_TENSOR_DATA_TYPE_UINT8}, {
        "int8out.tflite", GST_TENSOR_DATA_TYPE_INT8}
  };
  guint i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    gchar *tmp_model = setup_model_with_ranges (tests[i].model_name,
        "0.0,255.0;0.0,255.0;0.0,255.0");
    GstHarness *h = harness_new_with_model (tmp_model);
    GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
    GstBuffer *out;
    GstTensorMeta *tmeta;
    const GstTensor *tensor;
    guint8 expected_u8[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
    gint8 expected_i8[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
    GstCaps *actual_caps, *expected_caps;
    const gchar *group_id;
    TfliteTestTensorInfo out_tensor;

    group_id = (tests[i].expected_type == GST_TENSOR_DATA_TYPE_UINT8)
        ? "uint8out-group" : "int8out-group";
    out_tensor.id = "output-0";
    out_tensor.type = gst_tensor_data_type_get_name (tests[i].expected_type);
    out_tensor.dims_order = "row-major";
    out_tensor.dims[0] = 1;
    out_tensor.dims[1] = 4;
    out_tensor.dims[2] = 4;
    out_tensor.dims[3] = 3;
    out_tensor.n_dims = 4;

    gst_harness_set_src_caps_str (h,
        "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
    out = gst_harness_push_and_pull (h, in);
    fail_unless (out);

    fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
    tmeta = gst_buffer_get_tensor_meta (out);
    fail_unless_equals_int (tmeta->num_tensors, 1);
    tensor = gst_tensor_meta_get (tmeta, 0);
    fail_unless (tensor != NULL);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-0")) == tensor);
    fail_unless (gst_tensor_meta_get_by_id (tmeta,
            g_quark_from_static_string ("output-1")) == NULL);
    fail_unless_equals_int (tensor->id,
        g_quark_from_static_string ("output-0"));
    fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
    fail_unless_equals_int (tensor->data_type, tests[i].expected_type);
    fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
    fail_unless_equals_int ((gint) tensor->num_dims, 4);
    fail_unless_equals_int ((gint) tensor->dims[0], 1);
    fail_unless_equals_int ((gint) tensor->dims[1], 4);
    fail_unless_equals_int ((gint) tensor->dims[2], 4);
    fail_unless_equals_int ((gint) tensor->dims[3], 3);

    actual_caps = pull_output_caps (h);
    expected_caps = build_expected_output_caps ("RGB",
        TEST_WIDTH, TEST_HEIGHT, 30, 1, group_id, &out_tensor, 1);
    fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
    gst_caps_unref (actual_caps);
    gst_caps_unref (expected_caps);

    if (tests[i].expected_type == GST_TENSOR_DATA_TYPE_UINT8) {
      fill_expected_flat_rgb_u8 (expected_u8, 11, 22, 33);
      TFLITE_TEST_ASSERT_TENSOR_VALUES_U8 (tensor, expected_u8,
          G_N_ELEMENTS (expected_u8));
    } else {
      fill_expected_flat_rgb_i8 (expected_i8, 11, 22, 33);
      TFLITE_TEST_ASSERT_TENSOR_VALUES_I8 (tensor, expected_i8,
          G_N_ELEMENTS (expected_i8));
    }

    gst_buffer_unref (out);
    gst_harness_teardown (h);
    cleanup_temp_model (tmp_model);
  }
}

GST_END_TEST;

/* Test that a model with a dynamic batch dimension (-1) is handled correctly. */
GST_START_TEST (test_dynamic_dims)
{
  gchar *tmp_model = setup_model_with_ranges ("dynamic_batch.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  GstCaps *actual_caps, *expected_caps;
  const TfliteTestTensorInfo out_tensors[] = {
    {"output-0", "float32", "row-major", {-1, 48}, 2}
  };

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless (tensor != NULL);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-0")) == tensor);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-1")) == NULL);
  fail_unless_equals_int (tensor->id, g_quark_from_static_string ("output-0"));
  fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 48);

  actual_caps = pull_output_caps (h);
  expected_caps = build_expected_output_caps ("RGB",
      TEST_WIDTH, TEST_HEIGHT, 30, 1, "dynamic_batch-group",
      out_tensors, G_N_ELEMENTS (out_tensors));
  fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
  gst_caps_unref (actual_caps);
  gst_caps_unref (expected_caps);

  fill_expected_flat_rgb_f32 (expected, 11.f, 22.f, 33.f);
  TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test a model with two outputs in different dimension orders (row-major and col-major), verifying tensor IDs and values. */
GST_START_TEST (test_multi_output_tensor_id_and_dims_order)
{
  gchar *tmp_model = setup_model_with_ranges ("multi_output.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  const GstTensor *tensor_by_id;
  gfloat expected_flat[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  gfloat expected_chw[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  guint i;
  gboolean seen_output_0 = FALSE;
  gboolean seen_output_1 = FALSE;
  GstCaps *actual_caps, *expected_caps;
  const TfliteTestTensorInfo out_tensors[] = {
    {"output-0", "float32", "row-major", {1, 3, 16}, 3},
    {"output-1", "float32", "col-major", {1, 48}, 2}
  };

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);
  fill_expected_flat_rgb_f32 (expected_flat, 11.f, 22.f, 33.f);
  fill_expected_chw_rgb_f32 (expected_chw, 11.f, 22.f, 33.f);

  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 2);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-2")) == NULL);

  for (i = 0; i < tmeta->num_tensors; i++) {
    tensor = gst_tensor_meta_get (tmeta, i);
    fail_unless (tensor != NULL);
    fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
    fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);

    if (tensor->id == g_quark_from_static_string ("output-0")) {
      seen_output_0 = TRUE;
      fail_unless_equals_int ((gint) tensor->num_dims, 3);
      fail_unless_equals_int ((gint) tensor->dims[0], 1);
      fail_unless_equals_int ((gint) tensor->dims[1], 3);
      fail_unless_equals_int ((gint) tensor->dims[2], 16);
      fail_unless_equals_int (tensor->dims_order,
          GST_TENSOR_DIM_ORDER_ROW_MAJOR);
      TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected_chw,
          G_N_ELEMENTS (expected_chw), 1e-6f);
    } else if (tensor->id == g_quark_from_static_string ("output-1")) {
      seen_output_1 = TRUE;
      fail_unless_equals_int ((gint) tensor->num_dims, 2);
      fail_unless_equals_int ((gint) tensor->dims[0], 1);
      fail_unless_equals_int ((gint) tensor->dims[1], 48);
      fail_unless_equals_int (tensor->dims_order,
          GST_TENSOR_DIM_ORDER_COL_MAJOR);
      TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected_flat,
          G_N_ELEMENTS (expected_flat), 1e-6f);
    } else {
      fail_unless (FALSE, "Unexpected tensor id in output meta");
    }
  }

  fail_unless (seen_output_0);
  fail_unless (seen_output_1);

  actual_caps = pull_output_caps (h);
  expected_caps = build_expected_output_caps ("RGB",
      TEST_WIDTH, TEST_HEIGHT, 30, 1, "multi_output-group",
      out_tensors, G_N_ELEMENTS (out_tensors));
  fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
  gst_caps_unref (actual_caps);
  gst_caps_unref (expected_caps);

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

static void
assert_model_fails_to_start (const gchar * model_name)
{
  gchar *model = g_build_filename (GST_TFLITE_TEST_DATA_PATH,
      model_name, NULL);
  GstElement *e = gst_element_factory_make ("tfliteinference", NULL);

  g_object_set (e, "model-file", model, NULL);
  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      != GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
  g_free (model);
}

/* Test that models with multiple inputs, unsupported dimensions, or corrupt data fail to reach PAUSED state. */
GST_START_TEST (test_invalid_model_shapes_and_files)
{
  assert_model_fails_to_start ("multi_input_two_tensors.tflite");
  assert_model_fails_to_start ("invalid_dims_1d.tflite");
  assert_model_fails_to_start ("invalid_dims_5d.tflite");
  assert_model_fails_to_start ("invalid_dims_channels_2.tflite");
  assert_model_fails_to_start ("corrupt_model.tflite");
}

GST_END_TEST;

/* Test that a missing or non-existent model file causes a state-change failure and the element can cycle back to NULL. */
GST_START_TEST (test_missing_model_and_state_cycle)
{
  GstElement *e = gst_element_factory_make ("tfliteinference", NULL);

  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      != GST_STATE_CHANGE_FAILURE);

  g_object_set (e, "model-file", "/tmp/this-file-does-not-exist.tflite", NULL);
  fail_if (gst_element_set_state (e, GST_STATE_PAUSED)
      != GST_STATE_CHANGE_FAILURE);

  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
}

GST_END_TEST;

/* Test that caps queries return non-empty results and that accept-caps correctly accepts RGB but rejects I420. */
GST_START_TEST (test_transform_caps_and_accept_caps)
{
  gchar *model = g_build_filename (GST_TFLITE_TEST_DATA_PATH,
      "flatten_uint8in_float32out.tflite", NULL);
  GstHarness *h = harness_new_with_model (model);
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
  g_free (model);
}

GST_END_TEST;

/* Test a model whose input is a 3-D CHW tensor, verifying that the channel plane layout is preserved in the output. */
GST_START_TEST (test_3d_input_tensor)
{
  gchar *tmp_model = setup_model_with_ranges ("flatten_3d_float32.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[3 * 16];
  GstCaps *actual_caps, *expected_caps;
  const TfliteTestTensorInfo out_tensors[] = {
    {"output-0", "float32", "row-major", {3, 16}, 2}
  };

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=RGB,width=4,height=4,framerate=30/1");
  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless (tensor != NULL);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-0")) == tensor);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-1")) == NULL);
  fail_unless_equals_int (tensor->id, g_quark_from_static_string ("output-0"));
  fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 3);
  fail_unless_equals_int ((gint) tensor->dims[1], 16);

  actual_caps = pull_output_caps (h);
  expected_caps = build_expected_output_caps ("RGB",
      TEST_WIDTH, TEST_HEIGHT, 30, 1, "flatten_3d_float32-group",
      out_tensors, G_N_ELEMENTS (out_tensors));
  fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
  gst_caps_unref (actual_caps);
  gst_caps_unref (expected_caps);

  fill_expected_chw_rgb_f32 (expected, 11, 22, 33);
  TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that ARGB input (alpha first, big-endian) is correctly unpacked and produces the right float32 RGB values. */
GST_START_TEST (test_argb_input)
{
  gchar *tmp_model =
      setup_model_with_ranges ("flatten_float32in_float32out.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_ARGB);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=ARGB,width=4,height=4,framerate=30/1");
  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);
  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);

  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless (tensor != NULL);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-0")) == tensor);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-1")) == NULL);
  fail_unless_equals_int (tensor->id, g_quark_from_static_string ("output-0"));
  fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 48);
  fill_expected_flat_rgb_f32 (expected, 11.f, 22.f, 33.f);
  TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that ABGR input (alpha first, reversed channel order) is correctly unpacked and produces the right float32 RGB values. */
GST_START_TEST (test_abgr_input)
{
  gchar *tmp_model =
      setup_model_with_ranges ("flatten_float32in_float32out.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_ABGR);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];

  gst_harness_set_src_caps_str (h,
      "video/x-raw,format=ABGR,width=4,height=4,framerate=30/1");
  out = gst_harness_push_and_pull (h, in);
  fail_unless (out);
  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);

  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless (tensor != NULL);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-0")) == tensor);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-1")) == NULL);
  fail_unless_equals_int (tensor->id, g_quark_from_static_string ("output-0"));
  fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 48);
  fill_expected_flat_rgb_f32 (expected, 11.f, 22.f, 33.f);
  TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that PTS, DTS, duration and the DISCONT flag are forwarded unchanged from the input buffer to the output. */
GST_START_TEST (test_timestamp_and_flags_propagation)
{
  gchar *tmp_model =
      setup_model_with_ranges ("flatten_float32in_float32out.tflite",
      "0.0,255.0;0.0,255.0;0.0,255.0");
  GstHarness *h = harness_new_with_model (tmp_model);
  GstBuffer *in = create_solid_color_buffer (GST_VIDEO_FORMAT_RGB);
  GstBuffer *out;
  GstTensorMeta *tmeta;
  const GstTensor *tensor;
  gfloat expected[TEST_NUM_PIXELS * TEST_NUM_CHANNELS];
  GstCaps *actual_caps, *expected_caps;
  const TfliteTestTensorInfo out_tensors[] = {
    {"output-0", "float32", "row-major", {1, 48}, 2}
  };

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

  fail_unless (gst_buffer_get_tensor_meta (out) != NULL);
  tmeta = gst_buffer_get_tensor_meta (out);
  fail_unless_equals_int (tmeta->num_tensors, 1);
  tensor = gst_tensor_meta_get (tmeta, 0);
  fail_unless (tensor != NULL);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-0")) == tensor);
  fail_unless (gst_tensor_meta_get_by_id (tmeta,
          g_quark_from_static_string ("output-1")) == NULL);
  fail_unless_equals_int (tensor->id, g_quark_from_static_string ("output-0"));
  fail_unless_equals_int (tensor->layout, GST_TENSOR_LAYOUT_CONTIGUOUS);
  fail_unless_equals_int (tensor->data_type, GST_TENSOR_DATA_TYPE_FLOAT32);
  fail_unless_equals_int (tensor->dims_order, GST_TENSOR_DIM_ORDER_ROW_MAJOR);
  fail_unless_equals_int ((gint) tensor->num_dims, 2);
  fail_unless_equals_int ((gint) tensor->dims[0], 1);
  fail_unless_equals_int ((gint) tensor->dims[1], 48);

  actual_caps = pull_output_caps (h);
  expected_caps = build_expected_output_caps ("RGB",
      TEST_WIDTH, TEST_HEIGHT, 30, 1,
      "flatten_float32in_float32out-group",
      out_tensors, G_N_ELEMENTS (out_tensors));
  fail_unless (gst_caps_is_equal (actual_caps, expected_caps));
  gst_caps_unref (actual_caps);
  gst_caps_unref (expected_caps);

  fill_expected_flat_rgb_f32 (expected, 11.f, 22.f, 33.f);
  TFLITE_TEST_ASSERT_TENSOR_VALUES_F32 (tensor, expected,
      G_N_ELEMENTS (expected), 1e-6f);

  gst_buffer_unref (out);
  gst_harness_teardown (h);
  cleanup_temp_model (tmp_model);
}

GST_END_TEST;

/* Test that caps whose width or height do not match the model's expected dimensions are rejected. */
GST_START_TEST (test_accept_caps_dimension_mismatch)
{
  gchar *model = g_build_filename (GST_TFLITE_TEST_DATA_PATH,
      "flatten_uint8in_float32out.tflite", NULL);
  GstHarness *h = harness_new_with_model (model);
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
  g_free (model);
}

GST_END_TEST;

static Suite *
tfliteinference_suite (void)
{
  Suite *s = suite_create ("tfliteinference");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_input_formats);
  tcase_add_test (tc, test_normalization_variants);
  tcase_add_test (tc, test_output_dtypes);
  tcase_add_test (tc, test_dynamic_dims);
  tcase_add_test (tc, test_multi_output_tensor_id_and_dims_order);
  tcase_add_test (tc, test_invalid_model_shapes_and_files);
  tcase_add_test (tc, test_missing_model_and_state_cycle);
  tcase_add_test (tc, test_transform_caps_and_accept_caps);
  tcase_add_test (tc, test_3d_input_tensor);
  tcase_add_test (tc, test_argb_input);
  tcase_add_test (tc, test_abgr_input);
  tcase_add_test (tc, test_timestamp_and_flags_propagation);
  tcase_add_test (tc, test_accept_caps_dimension_mismatch);

  return s;
}

GST_CHECK_MAIN (tfliteinference);
