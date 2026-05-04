/* Shared test helpers for inference element unit tests.
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

#include "inference-utils.h"

#include <math.h>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <gst/check/gstcheck.h>

#define DEFINE_FILL_EXPECTED_FLAT_RGB_FUNC(name, ctype) \
void \
name (ctype * out, gsize n_pixels, ctype r, ctype g, ctype b) \
{ \
  gsize i; \
 \
  for (i = 0; i < n_pixels; i++) { \
    out[i * 3 + 0] = r; \
    out[i * 3 + 1] = g; \
    out[i * 3 + 2] = b; \
  } \
}

DEFINE_FILL_EXPECTED_FLAT_RGB_FUNC (fill_expected_flat_rgb_u8, guint8);
DEFINE_FILL_EXPECTED_FLAT_RGB_FUNC (fill_expected_flat_rgb_i8, gint8);
DEFINE_FILL_EXPECTED_FLAT_RGB_FUNC (fill_expected_flat_rgb_i32, gint32);
DEFINE_FILL_EXPECTED_FLAT_RGB_FUNC (fill_expected_flat_rgb_f32, gfloat);

void
fill_expected_gray_f32 (gfloat * out, gsize n_values, gfloat value)
{
  gsize i;

  for (i = 0; i < n_values; i++)
    out[i] = value;

}


#define DEFINE_FILL_EXPECTED_CHW_RGB_FUNC(name, ctype)                  \
  void                                                                  \
  name (ctype * out, gsize n_pixels, ctype r, ctype g, ctype b)        \
  {                                                                     \
    gsize i;                                                            \
                                                                        \
    for (i = 0; i < n_pixels; i++) {                                    \
      out[0 * n_pixels + i] = r;                                        \
      out[1 * n_pixels + i] = g;                                        \
      out[2 * n_pixels + i] = b;                                        \
    }                                                                   \
  }

DEFINE_FILL_EXPECTED_CHW_RGB_FUNC (fill_expected_chw_rgb_f32, gfloat);

void
assert_tensor_values_f32 (const GstTensor * tensor,
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

#define DEFINE_ASSERT_TENSOR_VALUES_INT_FUNC(name, ctype, datatype, fmt) \
void \
name (const GstTensor * tensor, const ctype * expected, gsize n_values, \
  const gchar * file, gint line) \
{ \
  GstMapInfo map; \
  const ctype *actual; \
  gsize i; \
 \
  fail_unless (tensor, "%s:%d tensor is NULL", file, line); \
  fail_unless_equals_int (tensor->data_type, datatype); \
  fail_unless (gst_buffer_map (tensor->data, &map, GST_MAP_READ), \
    "%s:%d failed to map tensor data", file, line); \
  fail_unless (map.size == n_values * sizeof (ctype), \
    "%s:%d tensor size=%zu expected=%zu", file, line, map.size, \
    n_values * sizeof (ctype)); \
 \
  actual = (const ctype *) map.data; \
  for (i = 0; i < n_values; i++) { \
  fail_unless (actual[i] == expected[i], \
    "%s:%d value[%zu]=%" fmt " expected %" fmt, file, line, i, \
    (ctype) actual[i], (ctype) expected[i]); \
  } \
 \
  gst_buffer_unmap (tensor->data, &map); \
}

DEFINE_ASSERT_TENSOR_VALUES_INT_FUNC (assert_tensor_values_i32, gint32,
    GST_TENSOR_DATA_TYPE_INT32, G_GINT32_FORMAT);
DEFINE_ASSERT_TENSOR_VALUES_INT_FUNC (assert_tensor_values_u8, guint8,
    GST_TENSOR_DATA_TYPE_UINT8, "hhu");
DEFINE_ASSERT_TENSOR_VALUES_INT_FUNC (assert_tensor_values_i8, gint8,
    GST_TENSOR_DATA_TYPE_INT8, "hhd");

gchar *
setup_model_with_modelinfo (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name,
    const gchar * modelinfo_content)
{
  gchar *base_model = g_build_filename (data_path, base_model_name, NULL);
  gchar *tmp_model = g_strdup_printf ("%s%c%s-%u-%s",
      g_get_tmp_dir (), G_DIR_SEPARATOR, tmp_prefix, g_random_int (),
      base_model_name);
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

gchar *
setup_model_with_ranges (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name,
    const gchar * ranges)
{
  gchar *base_model = g_build_filename (data_path, base_model_name, NULL);
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
  tmp_model = setup_model_with_modelinfo (data_path, tmp_prefix,
      base_model_name, data);

  g_free (data);
  g_key_file_unref (kf);
  g_free (base_modelinfo);
  g_free (base_model);

  return tmp_model;
}

gchar *
setup_model_without_input_info (const gchar * data_path,
    const gchar * tmp_prefix, const gchar * base_model_name)
{
  gchar *base_model = g_build_filename (data_path, base_model_name, NULL);
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
      g_key_file_remove_group (kf, groups[i], NULL);

    g_free (dir);
  }
  g_strfreev (groups);

  data = g_key_file_to_data (kf, &len, NULL);
  tmp_model = setup_model_with_modelinfo (data_path, tmp_prefix,
      base_model_name, data);

  g_free (data);
  g_key_file_unref (kf);
  g_free (base_modelinfo);
  g_free (base_model);

  return tmp_model;
}

void
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

GstBuffer *
create_solid_color_buffer (GstVideoFormat format, guint width, guint height,
    guint8 r_value, guint8 g_value, guint8 b_value, guint8 a_value)
{
  return create_solid_color_buffer_aligned (format, NULL,
      width, height, r_value, g_value, b_value, a_value);
}

GstBuffer *
create_solid_color_buffer_aligned (GstVideoFormat format,
    GstAllocationParams * alloc_params, guint width, guint height,
    guint8 r_value, guint8 g_value, guint8 b_value, guint8 a_value)
{
  GstVideoInfo info;
  GstBuffer *buf;
  GstMapInfo map;
  guint y, x;

  fail_unless (gst_video_info_set_format (&info, format, width, height));

  buf = gst_buffer_new_allocate (NULL, info.size, alloc_params);
  fail_unless (gst_buffer_map (buf, &map, GST_MAP_WRITE));

  for (y = 0; y < height; y++) {
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

    for (x = 0; x < width; x++) {
      r[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_R)] = r_value;
      g[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_G)] = g_value;
      b[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_B)] = b_value;
      if (a)
        a[x * GST_VIDEO_INFO_COMP_PSTRIDE (&info, GST_VIDEO_COMP_A)] = a_value;
    }
  }

  gst_buffer_unmap (buf, &map);
  return buf;
}

GstBuffer *
create_solid_gray_buffer (GstVideoFormat format,
    GstAllocationParams * alloc_params, guint width, guint height, guint8 value)
{
  GstVideoInfo info;
  GstBuffer *buf;
  GstMapInfo map;
  guint y, x;

  fail_unless (gst_video_info_set_format (&info, format, width, height));

  fail_unless (GST_VIDEO_INFO_IS_GRAY (&info));

  buf = gst_buffer_new_allocate (NULL, info.size, alloc_params);
  fail_unless (gst_buffer_map (buf, &map, GST_MAP_WRITE));

  for (y = 0; y < height; y++) {
    guint8 *row = map.data + info.stride[0] * y;
    for (x = 0; x < width; x++)
      row[x] = value;
  }

  gst_buffer_unmap (buf, &map);
  return buf;
}
