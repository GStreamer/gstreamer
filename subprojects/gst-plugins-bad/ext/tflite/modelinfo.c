/*
 * GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 * @author: Olivier Crete <olivier.crete@collabora.com>
 *
 * modeinfo.c
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

#include "modelinfo.h"

/**
 * SECTION: ModelInfo
 *
 * The ".modelinfo" files describe the additional metadata for
 * a given serialized model file such as a `.tflite`, `.onnx` or `.pte` files.
 *
 * The ModelInfo files are ini-style. Each section is matched to a
 * particular input or output tensor.
 *
 * The title of the section should ideally match the name of the tensor
 * in the model file.
 *
 * The fields used to match the modelinfo to the model are:
 * * `\[title\]`: The name of the tensor, must be unique
 * * `dims`: The dimensions as a comma-separated list of ints. -1 matches a dynamic dimension and is a wildcard
 * * `dir`: Either "input" or "output"
 * * `type`: The data type match #GstTensorDataType, one of:
 *   * `int4`
 *   * `int8`
 *   * `int16`
 *   * `int32`
 *   * `int64`
 *   * `uint4`
 *   * `uint8`
 *   * `uint16`
 *   * `uint32`
 *   * `uint64`
 *   * `float16`
 *   * `float32`
 *   * `float64`
 *   * `bfloat16`
 *
 * Based on these fields, the following metadata is applied to output tensors:
 * * `id`: The tensor ID so othr elements can identity it, ideally registered in the [Tensor ID Registry](https://github.com/collabora/tensor-id-registry/blob/main/tensor-id-register.md).
 *
 * Those fields are applied to input tensors for normalization:
 * * `mean`: a double or a comma separated list of floats, one per channel.
 * * `stddev`: a double or a comma separated list of floats, one per channel
 *
 * Those are applied with the formula `(val - mean) / stddev`. Those
 * are applied based on a range of [0, 255]. If the input is not in
 * the range of [0, 255], the values will be converted before applyign
 * them. A mean of 127" means 127 for a `uint8` input or 0 for
 * `int8` and 0.5 for `float` inputs.
 *
 * Other fields are ignored for now.
 *
 * The API is meant to be used by plugins
 *
 * Since: 1.28
 */

GST_DEBUG_CATEGORY (analytics_modelinfo_debug);
#define GST_CAT_DEFAULT analytics_modelinfo_debug


static gboolean
key_file_string_matches (GKeyFile * keyfile, const gchar * group,
    const gchar * key, const gchar * value)
{
  gchar *kf_value = g_key_file_get_string (keyfile, group, key, NULL);

  gboolean matches = !g_strcmp0 (kf_value, value);

  g_free (kf_value);

  return matches;
}

gchar *
modelinfo_get_id (ModelInfo * modelinfo, const gchar * tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  return g_key_file_get_string (kf, tensor_name, "id", NULL);
}

GQuark
modelinfo_get_quark_id (ModelInfo * modelinfo, const gchar * tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  GQuark q = 0;
  gchar *id = g_key_file_get_string (kf, tensor_name, "id", NULL);

  if (id)
    q = g_quark_from_string (id);
  g_free (id);

  return q;
}

static gboolean
modelinfo_check_direction (GKeyFile * kf,
    const gchar * tensor_name, ModelInfoTensorDirection dir)
{
  gchar *value;
  gboolean ret = FALSE;

  if (dir == MODELINFO_DIRECTION_UNKNOWN)
    return TRUE;

  value = g_key_file_get_string (kf, tensor_name, "dir", NULL);
  if (!value)
    return TRUE;

  if (dir == MODELINFO_DIRECTION_INPUT)
    ret = g_str_equal (value, "input");
  if (dir == MODELINFO_DIRECTION_OUTPUT)
    ret = g_str_equal (value, "output");

  g_free (value);

  return ret;
}

static gboolean
modelinfo_validate_internal (GKeyFile * kf, const gchar * tensor_name,
    ModelInfoTensorDirection dir, GstTensorDataType data_type, gsize num_dims,
    const gsize * dims, gboolean accept_no_dims)
{
  gsize kf_dims_length = 0;
  gint *kf_dims;
  gsize i;
  gboolean ret = FALSE;

  if (!key_file_string_matches (kf, tensor_name, "type",
          gst_tensor_data_type_get_name (data_type)))
    return FALSE;

  if (!modelinfo_check_direction (kf, tensor_name, dir))
    return FALSE;

  if (!g_key_file_has_key (kf, tensor_name, "dims", NULL))
    return accept_no_dims;

  kf_dims = g_key_file_get_integer_list (kf, tensor_name, "dims",
      &kf_dims_length, NULL);
  if (kf_dims == NULL) {
    GST_ERROR ("Invalid model info file, dims in %s is no in the"
        " right format", tensor_name);
    return FALSE;
  }

  if (kf_dims_length != num_dims)
    goto done;

  for (i = 0; i < kf_dims_length; i++) {
    /* If the keyfile contains dims < 0, then its a wildcard,
     * accept anything */
    if (kf_dims[i] < 0)
      continue;
    /* Dimensions of size "-1" means dynamic, but we didn't accept a wildcard,
     * reject it */
    if (dims[i] == G_MAXSIZE)
      goto done;

    if (kf_dims[i] != dims[i])
      goto done;
  }

  ret = TRUE;
done:
  g_free (kf_dims);
  return ret;
}

static gboolean
modelinfo_validate (ModelInfo * modelinfo, const gchar * tensor_name,
    ModelInfoTensorDirection dir, GstTensorDataType data_type, gsize num_dims,
    const gsize * dims)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  return modelinfo_validate_internal (kf, tensor_name, dir, data_type,
      num_dims, dims, TRUE);
}

static gboolean
modelinfo_has_tensor_name (ModelInfo * modelinfo, const char *tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  return g_key_file_has_group (kf, tensor_name);
}

static gchar *
modelinfo_find_tensor_name_by_index (ModelInfo * modelinfo,
    ModelInfoTensorDirection dir, gsize index)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar **groups;
  gsize i, j;
  gchar *tensor_name = NULL;

  groups = g_key_file_get_groups (kf, NULL);

  for (i = 0, j = 0; groups[i]; i++) {
    if (!modelinfo_check_direction (kf, groups[i], dir))
      continue;

    if (index == j++) {
      tensor_name = g_strdup (groups[i]);
      break;
    }

    j++;
  }

  g_strfreev (groups);
  return tensor_name;
}

static gchar *
modelinfo_find_tensor_name_by_dims (ModelInfo * modelinfo,
    ModelInfoTensorDirection dir, GstTensorDataType data_type,
    gsize num_dims, const gsize * dims)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar **groups;
  gsize i;
  gchar *tensor_name = NULL;

  groups = g_key_file_get_groups (kf, NULL);

  for (i = 0; groups[i]; i++) {
    if (modelinfo_validate_internal (kf, groups[i], dir, data_type,
            num_dims, dims, FALSE)) {
      tensor_name = g_strdup (groups[i]);
      break;
    }
  }

  g_strfreev (groups);
  return tensor_name;
}


ModelInfo *
modelinfo_load (const gchar * model_filename)
{
  GKeyFile *kf = g_key_file_new ();
  gchar *filename;
  gboolean ret;
  gchar *last_dot;

  g_key_file_set_list_separator (kf, ',');

  GST_DEBUG_CATEGORY_INIT (analytics_modelinfo_debug, "modelinfo",
      0, "analytics model info");

  filename = g_strconcat (model_filename, ".modelinfo", NULL);
  ret = g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, NULL);
  g_free (filename);
  if (ret)
    return (ModelInfo *) kf;

  last_dot = g_utf8_strrchr (model_filename, -1, '.');
  if (last_dot && !g_utf8_strchr (last_dot, -1, '/')) {
    gchar *tmp = g_strndup (model_filename, last_dot - model_filename);
    filename = g_strconcat (tmp, ".modelinfo", NULL);
    g_free (tmp);
    ret = g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, NULL);
    g_free (filename);
    if (ret)
      return (ModelInfo *) kf;
  }

  g_key_file_free (kf);
  return NULL;
}

void
modelinfo_free (ModelInfo * modelinfo)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  g_key_file_free (kf);
}


gchar *
modelinfo_find_tensor_name (ModelInfo * modelinfo,
    ModelInfoTensorDirection dir, gsize index, const gchar * in_tensor_name,
    GstTensorDataType data_type, gsize num_dims, const gsize * dims)
{
  gchar *tensor_name = NULL;

  if (in_tensor_name && modelinfo_has_tensor_name (modelinfo, in_tensor_name)) {
    if (modelinfo_validate (modelinfo, in_tensor_name, dir, data_type,
            num_dims, dims)) {
      return g_strdup (in_tensor_name);
    }
  }

  tensor_name = modelinfo_find_tensor_name_by_index (modelinfo, dir, index);
  if (tensor_name) {
    if (modelinfo_validate (modelinfo, tensor_name, dir, data_type,
            num_dims, dims)) {
      return tensor_name;
    }
    g_free (tensor_name);
  }

  return modelinfo_find_tensor_name_by_dims (modelinfo, dir, data_type,
      num_dims, dims);
}

static gsize
modelinfo_get_doubles (ModelInfo * modelinfo, const gchar * tensor_name,
    const gchar * param_name, gsize num_channels, gdouble ** out_doubles)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gdouble *doubles;
  gsize length;

  doubles = g_key_file_get_double_list (kf, tensor_name, param_name, &length,
      NULL);

  if (doubles == NULL)
    return 0;

  if (length != 1 && length != num_channels) {
    g_free (doubles);
    return 0;
  }

  *out_doubles = doubles;
  return length;
}

gsize
modelinfo_get_normalization_means (ModelInfo * modelinfo,
    const gchar * tensor_name, gsize num_channels, gdouble ** means)
{
  return modelinfo_get_doubles (modelinfo, tensor_name, "mean",
      num_channels, means);
}

gsize
modelinfo_get_normalization_stddevs (ModelInfo * modelinfo,
    const gchar * tensor_name, gsize num_channels, gdouble ** means)
{
  return modelinfo_get_doubles (modelinfo, tensor_name, "stddev",
      num_channels, means);
}
