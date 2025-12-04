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
 * SECTION: GstAnalyticsModelInfo
 * @title: GstAnalyticsModelInfo
 * @short_description: A GstAnalyticsModelInfo to store model information
 * @symbols:
 * - GstAnalyticsModelInfo
 *
 * The #GstAnalyticsModelInfo is an object storing artifical neural network
 * model metadata describing the input and output tensors. These information's
 * are required by inference elements.
 *
 * The ".modelinfo" files describe the additional metadata for
 * a given serialized model file such as a `.tflite`, `.onnx` or `.pte` files.
 *
 * The ModelInfo files are ini-style. Each section is matched to a
 * particular input or output tensor.
 *
 * The title of the section must match the name of the tensor in the model file.
 *
 * The fields used to match the modelinfo to the model are:
 *  `\[title\]`: The name of the tensor, must be unique
 *  `dims`: The dimensions as a comma-separated list of ints. -1 matches a dynamic dimension and is a wildcard
 *  `dir`: Either "input" or "output"
 *  `type`: The data type match #GstTensorDataType, one of:
 *    `int4`
 *    `int8`
 *    `int16`
 *    `int32`
 *    `int64`
 *    `uint4`
 *    `uint8`
 *    `uint16`
 *    `uint32`
 *    `uint64`
 *    `float16`
 *    `float32`
 *    `float64`
 *    `bfloat16`
 *
 * Based on these fields, the following metadata is applied to output tensors:
 *  `id`: The tensor ID so other elements can identity it, ideally registered in the [Tensor ID Registry](https://github.com/collabora/tensor-id-registry/blob/main/tensor-id-register.md).
 *  `group-id`: The group ID that groups related tensors together (e.g., all outputs from the same model)
 *  `dims-order`: The dimension ordering, either "row-major" or "col-major". Defaults to "row-major" if not specified.
 *
 * Those fields are applied to input tensors for normalization:
 *  `ranges`: semicolon-separated list of comma-separated pairs of floats,
 *   each representing (min, max) for a single channel or dimension.
 *   For per-channel normalization: `ranges=0.0,255.0;-1.0,1.0;0.0,1.0` (R,G,B)
 *   For single range (applies to all channels): `ranges=0.0,255.0`
 *   The inference elements will convert 8-bit input [0-255] to target ranges using:
 *     output[i] = input[i] * scale[i] + offset[i]
 *   where for each channel i:
 *     scale[i] = (max[i] - min[i]) / 255.0
 *     offset[i] = min[i]
 *
 *   Common ranges:
 *    `0.0,255.0` - No normalization (passthrough, scale=1.0, offset=0.0)
 *    `0.0,1.0` - Normalized to [0,1] range (scale≈0.00392, offset=0.0)
 *    `-1.0,1.0` - Normalized to [-1,1] range (scale≈0.00784, offset=-1.0)
 *    `16.0,235.0` - TV/limited range (scale≈0.859, offset=16.0)
 *
 * Other fields are ignored for now.
 *
 * The API is meant to be used by inference elements
 *
 * Since: 1.28
 */

/**
 * gst_analytics_modelinfo_get_type:
 *
 * Get the GType of the #GstAnalyticsModelInfo boxed type.
 *
 * Returns: The GType
 *
 * Since: 1.28
 */
G_DEFINE_BOXED_TYPE (GstAnalyticsModelInfo, gst_analytics_modelinfo,
    (GBoxedCopyFunc) g_key_file_ref,
    (GBoxedFreeFunc) gst_analytics_modelinfo_free)
#define GST_CAT_DEFAULT analytics_modelinfo_debug
    GST_DEBUG_CATEGORY (analytics_modelinfo_debug);

     static gboolean
         key_file_string_matches (GKeyFile * keyfile, const gchar * group,
    const gchar * key, const gchar * value)
{
  gchar *kf_value = g_key_file_get_string (keyfile, group, key, NULL);

  gboolean matches = !g_strcmp0 (kf_value, value);

  g_free (kf_value);

  return matches;
}

/**
 * modelinfo_check_version:
 * @kf: The loaded GKeyFile
 *
 * Checks if the modelinfo version is supported. Files without version
 * are treated as version 1.0 for backward compatibility.
 *
 * Returns: TRUE if version is supported, FALSE otherwise
 */
static gboolean
modelinfo_check_version (GKeyFile * kf)
{
  gchar *file_version;
  gboolean has_version_section;
  gboolean supported = FALSE;
  gchar **version_parts;
  gint major = 0, minor = 0;

  /* Check if modelinfo section exists */
  has_version_section = g_key_file_has_group (kf, GST_MODELINFO_SECTION_NAME);

  if (!has_version_section) {
    /* v1.0 is the first public version and requires [modelinfo] section. */
    GST_ERROR ("No [modelinfo] section found. This is a pre-v1.0 format file. "
        "Please regenerate modelinfo using modelinfo-generator.py to create "
        "a v%s compatible file.", GST_MODELINFO_VERSION_STR);
    return FALSE;
  }

  /* Get version string */
  file_version = g_key_file_get_string (kf, GST_MODELINFO_SECTION_NAME,
      "version", NULL);

  if (!file_version) {
    GST_ERROR ("Modelinfo section exists but no version field found. "
        "v1.0 is the first public version and requires version field. "
        "Please regenerate modelinfo using modelinfo-generator.py to create "
        "a v%s compatible file.", GST_MODELINFO_VERSION_STR);
    return FALSE;
  }

  /* Parse version string (format: "Major.Minor") */
  version_parts = g_strsplit (file_version, ".", -1);

  if (!version_parts || !version_parts[0] || !version_parts[1] ||
      version_parts[2] != NULL) {
    GST_ERROR ("Invalid version format: '%s'. Expected format: 'Major.Minor'",
        file_version);
    g_strfreev (version_parts);
    g_free (file_version);
    return FALSE;
  }

  major = g_ascii_strtoll (version_parts[0], NULL, 10);
  minor = g_ascii_strtoll (version_parts[1], NULL, 10);

  /* Check if version is supported
   * Major version must match exactly.
   * Minor versions can be older (backward compatible within same major) */
  if (major != GST_MODELINFO_VERSION_MAJOR) {
    /* Major version mismatch - not supported */
    if (major < GST_MODELINFO_VERSION_MAJOR) {
      GST_ERROR
          ("Modelinfo major version %d is not supported by this version of "
          "GStreamer (current major: %d). Please use the modelinfo-generator.py "
          "script with --upgrade to upgrade the file to version %s.", major,
          GST_MODELINFO_VERSION_MAJOR, GST_MODELINFO_VERSION_STR);
    } else {
      GST_ERROR ("Modelinfo version %s is not supported by this version of "
          "GStreamer (current: %s). Please upgrade GStreamer.",
          file_version, GST_MODELINFO_VERSION_STR);
    }
    supported = FALSE;
  } else if (minor > GST_MODELINFO_VERSION_MINOR) {
    /* Newer minor version in same major - log warning but still supported */
    GST_WARNING ("Modelinfo minor version %d is newer than supported (%d). "
        "Some features may not be available.",
        minor, GST_MODELINFO_VERSION_MINOR);
    supported = TRUE;
  } else {
    /* Same major, same or older minor - fully supported */
    supported = TRUE;
  }

  g_strfreev (version_parts);
  g_free (file_version);
  return supported;
}

/**
 * gst_analytics_modelinfo_get_id:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @tensor_name: The name of the tensor
 *
 * Get the tensor ID from the modelinfo for the specified tensor name.
 *
 * The tensor ID is ideally registered in the [Tensor ID Registry](https://github.com/collabora/tensor-id-registry/blob/main/tensor-id-register.md).
 *
 * Returns: (nullable): The tensor ID string, or %NULL if not found.
 *    The caller must free this with g_free() when done.
 *
 * Since: 1.28
 */
gchar *
gst_analytics_modelinfo_get_id (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar *id = g_key_file_get_string (kf, tensor_name, "id", NULL);

  /* Check for placeholder that needs to be filled */
  if (id && g_str_has_prefix (id, "PLACEHOLDER")) {
    GST_WARNING ("Modelinfo file contains unresolved placeholder for id "
        "in tensor '%s': %s. Please regenerate the modelinfo file using "
        "modelinfo-generator.py --prompt and provide the correct values.",
        tensor_name, id);
  }

  return id;
}

/**
 * gst_analytics_modelinfo_get_group_id:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 *
 * Get the group ID that groups related tensors together (e.g., all outputs
 * from the same model).
 *
 * The group ID is stored in the modelinfo section and is global for all
 * tensors in the model.
 *
 * Returns: (nullable): The group ID string, or %NULL if not found.
 *    The caller must free this with g_free() when done.
 *
 * Since: 1.28
 */
gchar *
gst_analytics_modelinfo_get_group_id (GstAnalyticsModelInfo * modelinfo)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar *group_id;

  /* group-id is in [modelinfo] section (global for all tensors in v2.0+)
   * Major version compatibility is already checked in modelinfo_load() */
  group_id = g_key_file_get_string (kf, GST_MODELINFO_SECTION_NAME,
      "group-id", NULL);

  /* Check for placeholder that needs to be filled */
  if (group_id && g_str_has_prefix (group_id, "PLACEHOLDER")) {
    GST_WARNING
        ("Modelinfo file contains unresolved placeholder for group-id: %s. "
        "Please regenerate the modelinfo file using "
        "modelinfo-generator.py --prompt and provide the correct values.",
        group_id);
  }

  return group_id;
}

/**
 * gst_analytics_modelinfo_get_quark_id:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @tensor_name: The name of the tensor
 *
 * Get the tensor ID as a GQuark for efficient string comparison and storage.
 *
 * Using GQuark is more efficient than string comparison when you need to
 * compare multiple IDs.
 *
 * Returns: The GQuark of the tensor ID, or 0 if not found
 *
 * Since: 1.28
 */
GQuark
gst_analytics_modelinfo_get_quark_id (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  GQuark q = 0;
  gchar *id = g_key_file_get_string (kf, tensor_name, "id", NULL);

  if (id)
    q = g_quark_from_string (id);
  g_free (id);

  return q;
}

/**
 * gst_analytics_modelinfo_get_quark_group_id:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 *
 * Get the group ID as a GQuark for efficient string comparison and storage.
 *
 * Using GQuark is more efficient than string comparison when you need to
 * compare multiple group IDs.
 *
 * Returns: The GQuark of the group ID, or 0 if not found
 *
 * Since: 1.28
 */
GQuark
gst_analytics_modelinfo_get_quark_group_id (GstAnalyticsModelInfo * modelinfo)
{
  GQuark q = 0;
  gchar *id = gst_analytics_modelinfo_get_group_id (modelinfo);

  if (id)
    q = g_quark_from_string (id);
  g_free (id);

  return q;
}

static gboolean
modelinfo_check_direction (GKeyFile * kf,
    const gchar * tensor_name, GstAnalyticsModelInfoTensorDirection dir)
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
    GstAnalyticsModelInfoTensorDirection dir, GstTensorDataType data_type,
    gsize num_dims, const gsize * dims, gboolean accept_no_dims)
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
modelinfo_validate (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name, GstAnalyticsModelInfoTensorDirection dir,
    GstTensorDataType data_type, gsize num_dims, const gsize * dims)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  return modelinfo_validate_internal (kf, tensor_name, dir, data_type,
      num_dims, dims, TRUE);
}

static gboolean
modelinfo_has_tensor_name (GstAnalyticsModelInfo * modelinfo,
    const char *tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  return g_key_file_has_group (kf, tensor_name);
}

static gchar *
modelinfo_find_tensor_name_by_index (GstAnalyticsModelInfo * modelinfo,
    GstAnalyticsModelInfoTensorDirection dir, gsize index)
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
modelinfo_find_tensor_name_by_dims (GstAnalyticsModelInfo * modelinfo,
    GstAnalyticsModelInfoTensorDirection dir, GstTensorDataType data_type,
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


/**
 * gst_analytics_modelinfo_load:
 * @model_filename: Path to the model file (e.g., "model.onnx", "model.tflite")
 *
 * Load a modelinfo file associated with the given model file.
 *
 * This function attempts to load a `.modelinfo` file in the following order:
 * 1. `{model_filename}.modelinfo`
 * 2. `{model_filename_without_extension}.modelinfo`
 *
 * The modelinfo file contains metadata for the model's input and output tensors,
 * including normalization ranges, dimension ordering, tensor IDs, etc.
 *
 * The loaded modelinfo must be freed with gst_analytics_modelinfo_free()
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): A new #GstAnalyticsModelInfo instance,
 *    or %NULL if the modelinfo file could not be found or loaded.
 *
 * Since: 1.28
 */
GstAnalyticsModelInfo *
gst_analytics_modelinfo_load (const gchar * model_filename)
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
  if (ret) {
    /* Version check */
    if (!modelinfo_check_version (kf)) {
      GST_ERROR ("Unsupported modelinfo version in file");
      g_key_file_free (kf);
      return NULL;
    }
    return (GstAnalyticsModelInfo *) kf;
  }

  last_dot = g_utf8_strrchr (model_filename, -1, '.');
  if (last_dot && !g_utf8_strchr (last_dot, -1, '/')) {
    gchar *tmp = g_strndup (model_filename, last_dot - model_filename);
    filename = g_strconcat (tmp, ".modelinfo", NULL);
    g_free (tmp);
    ret = g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, NULL);
    g_free (filename);
    if (ret) {
      /* Version check */
      if (!modelinfo_check_version (kf)) {
        GST_ERROR ("Unsupported modelinfo version in file");
        g_key_file_free (kf);
        return NULL;
      }
      return (GstAnalyticsModelInfo *) kf;
    }
  }

  g_key_file_free (kf);
  return NULL;
}

/**
 * gst_analytics_modelinfo_free:
 * @model_info: (transfer full) (nullable): Instance of #GstAnalyticsModelInfo
 *
 * Free a modelinfo object allocated by gst_analytics_modelinfo_load().
 *
 * This function should be called when the modelinfo is no longer needed
 * to release the associated resources.
 *
 * Since: 1.28
 */
void
gst_analytics_modelinfo_free (GstAnalyticsModelInfo * modelinfo)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;

  g_key_file_free (kf);
}


/**
 * gst_analytics_modelinfo_find_tensor_name:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @dir: The tensor direction (input or output)
 * @index: The tensor index within the specified direction
 * @in_tensor_name: (nullable): An optional tensor name hint to check first
 * @data_type: The tensor data type to match
 * @num_dims: The number of dimensions
 * @dims: (array length=num_dims): The dimension sizes. Use -1 for dynamic dimensions.
 *
 * Find the name of a tensor in the modelinfo that matches the given criteria.
 *
 * The function performs the following checks in order:
 * 1. If @in_tensor_name is provided and exists in modelinfo, validate it matches
 * 2. Search by index for the specified direction and validate
 * 3. Search by dimensions and data type
 *
 * Returns: (nullable): The tensor name if found, or %NULL otherwise.
 *    The caller must free this with g_free() when done.
 *
 * Since: 1.28
 */
gchar *
gst_analytics_modelinfo_find_tensor_name (GstAnalyticsModelInfo * modelinfo,
    GstAnalyticsModelInfoTensorDirection dir, gsize index,
    const gchar * in_tensor_name, GstTensorDataType data_type, gsize num_dims,
    const gsize * dims)
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


/**
 * gst_analytics_modelinfo_get_target_ranges:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @tensor_name: The name of the tensor
 * @num_ranges: (out): The number of ranges
 * @mins: (out) (transfer full) (array length=num_ranges): The minimum values for each target range
 * @maxs: (out) (transfer full) (array length=num_ranges): The maximum values for each target range
 *
 * Retrieve all target ranges (min/max pairs) expected by the model for a given tensor.
 *
 * This function retrieves all target ranges from the `ranges` field in the modelinfo.
 * Each range represents the expected input range for a channel or dimension that the
 * model requires.
 *
 * The function reads from the `ranges` field: Semicolon-separated list of
 * comma-separated pairs (min,max) for per-channel target ranges
 * (e.g., "0.0,1.0;-1.0,1.0;0.0,1.0" for RGB channels with different normalization targets).
 *
 * The caller must free @mins and @maxs with g_free() when done.
 *
 * Returns: %TRUE if range information was found and valid, %FALSE otherwise
 *
 * Since: 1.28
 */
gboolean
gst_analytics_modelinfo_get_target_ranges (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name, gsize * num_ranges, gdouble ** mins,
    gdouble ** maxs)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar *ranges_str = NULL;
  gchar **range_parts = NULL;
  gsize local_num_ranges = 0;
  gsize i;

  *mins = NULL;
  *maxs = NULL;
  *num_ranges = 0;

  /* Parse 'ranges' field: "min,max;..." */
  ranges_str = g_key_file_get_string (kf, tensor_name, "ranges", NULL);
  if (!ranges_str) {
    GST_DEBUG ("Tensor '%s': no ranges specified, returning FALSE",
        tensor_name);
    return FALSE;
  }

  /* Check for placeholder */
  if (g_str_has_prefix (ranges_str, "PLACEHOLDER")) {
    GST_ERROR
        ("Modelinfo file contains unresolved placeholder for ranges in tensor '%s'. "
        "Please regenerate the modelinfo file using modelinfo-generator.py --prompt "
        "and provide the correct values.", tensor_name);
    g_free (ranges_str);
    return FALSE;
  }

  /* Parse ranges: semicolon-separated, each is "min,max" */
  range_parts = g_strsplit (ranges_str, ";", -1);
  local_num_ranges = g_strv_length (range_parts);

  *mins = g_new (gdouble, local_num_ranges);
  *maxs = g_new (gdouble, local_num_ranges);

  for (i = 0; i < local_num_ranges; i++) {
    gchar **minmax = g_strsplit (range_parts[i], ",", 2);
    if (g_strv_length (minmax) == 2) {
      (*mins)[i] = g_ascii_strtod (minmax[0], NULL);
      (*maxs)[i] = g_ascii_strtod (minmax[1], NULL);
      GST_DEBUG ("Tensor '%s'[%zu]: range=[%f, %f]",
          tensor_name, i, (*mins)[i], (*maxs)[i]);
    } else {
      GST_ERROR ("Invalid range format in tensor '%s'[%zu]: %s",
          tensor_name, i, range_parts[i]);
      g_strfreev (minmax);
      g_free (*mins);
      g_free (*maxs);
      *mins = NULL;
      *maxs = NULL;
      g_strfreev (range_parts);
      g_free (ranges_str);
      return FALSE;
    }
    g_strfreev (minmax);
  }

  *num_ranges = local_num_ranges;
  g_strfreev (range_parts);
  g_free (ranges_str);

  return TRUE;
}

/**
 * gst_analytics_modelinfo_get_input_scales_offsets:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @tensor_name: The name of the tensor
 * @num_input_ranges: The number of input ranges (channels/dimensions)
 * @input_mins: (array length=num_input_ranges): The minimum values of the actual input data for each channel
 * @input_maxs: (array length=num_input_ranges): The maximum values of the actual input data for each channel
 * @num_output_ranges: (out): The number of output ranges/scale-offset pairs
 * @output_scales: (out) (transfer full) (array length=num_output_ranges): The scale values for normalization
 * @output_offsets: (out) (transfer full) (array length=num_output_ranges): The offset values for normalization
 *
 * Calculate normalization scales and offsets to transform input data to the target range.
 *
 * This function calculates transformation parameters to convert from the actual input data range
 * [input_min, input_max] to the target range expected by the model [target_min, target_max]:
 *   `normalized_value[i] = input[i] * output_scale[i] + output_offset[i]`
 *
 * The target ranges are read from the modelinfo `ranges` field: Semicolon-separated list of
 * comma-separated pairs (min,max) for per-channel target ranges
 * (e.g., "0.0,255.0;-1.0,1.0;0.0,1.0" for RGB channels with different target ranges).
 *
 * Common input ranges:
 * - [0.0, 255.0]: 8-bit unsigned (uint8)
 * - [-128.0, 127.0]: 8-bit signed (int8)
 * - [0.0, 65535.0]: 16-bit unsigned (uint16)
 * - [-32768.0, 32767.0]: 16-bit signed (int16)
 * - [0.0, 1.0]: Normalized float
 * - [-1.0, 1.0]: Normalized signed float
 *
 * The number of input ranges (@num_input_ranges) must equal the number of target ranges
 * in the modelinfo. The function will return FALSE if they don't match.
 *
 * The caller must free @output_scales and @output_offsets with g_free() when done.
 *
 * Returns: %TRUE on success, %FALSE on error, if ranges field is not found, or if @num_input_ranges
 *          doesn't match the number of target ranges in the modelinfo
 *
 * Since: 1.28
 */
gboolean
gst_analytics_modelinfo_get_input_scales_offsets (GstAnalyticsModelInfo *
    modelinfo, const gchar * tensor_name, gsize num_input_ranges,
    const gdouble * input_mins, const gdouble * input_maxs,
    gsize * num_output_ranges, gdouble ** output_scales,
    gdouble ** output_offsets)
{
  gdouble *target_mins = NULL;
  gdouble *target_maxs = NULL;
  gsize num_target_ranges;
  gsize i;
  gdouble target_min, target_max;
  gdouble input_min, input_max;
  gdouble scale, offset;

  *output_scales = NULL;
  *output_offsets = NULL;
  *num_output_ranges = 0;

  /* Get target ranges from modelinfo */
  if (!gst_analytics_modelinfo_get_target_ranges (modelinfo, tensor_name,
          &num_target_ranges, &target_mins, &target_maxs)) {
    GST_DEBUG ("Tensor '%s': no ranges specified, returning FALSE",
        tensor_name);
    return FALSE;
  }

  /* Validate that input ranges match target ranges */
  if (num_input_ranges != num_target_ranges) {
    GST_ERROR
        ("Tensor '%s': number of input ranges (%zu) doesn't match number of "
        "target ranges in modelinfo (%zu)", tensor_name, num_input_ranges,
        num_target_ranges);
    g_free (target_mins);
    g_free (target_maxs);
    return FALSE;
  }

  /* Allocate output arrays */
  *output_scales = g_new (gdouble, num_target_ranges);
  *output_offsets = g_new (gdouble, num_target_ranges);

  /* Calculate scale and offset for each channel */
  for (i = 0; i < num_target_ranges; i++) {
    target_min = target_mins[i];
    target_max = target_maxs[i];
    input_min = input_mins[i];
    input_max = input_maxs[i];

    /* Calculate scale and offset to transform from input range to target range
     * Formula: output = input * scale + offset
     * where: scale = (target_max - target_min) / (input_max - input_min)
     *        offset = target_min - input_min * scale */
    scale = (target_max - target_min) / (input_max - input_min);
    offset = target_min - input_min * scale;

    (*output_scales)[i] = scale;
    (*output_offsets)[i] = offset;

    GST_DEBUG ("Tensor '%s'[%zu]: input=[%f, %f], target=[%f, %f] to scale=%f, "
        "offset=%f", tensor_name, i, input_min, input_max, target_min,
        target_max, scale, offset);
  }

  *num_output_ranges = num_target_ranges;
  g_free (target_mins);
  g_free (target_maxs);

  return TRUE;
}

/**
 * gst_analytics_modelinfo_get_dims_order:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 * @tensor_name: The name of the tensor
 *
 * Retrieve the dimension ordering for a given tensor.
 *
 * The dimension ordering specifies how multi-dimensional tensor data is
 * laid out in memory:
 * - Row-major (C/NumPy style): Last dimension changes fastest in memory
 * - Column-major (Fortran style): First dimension changes fastest in memory
 *
 * If not specified in the modelinfo, defaults to row-major.
 *
 * Returns: The dimension order as #GstTensorDimOrder
 *
 * Since: 1.28
 */
GstTensorDimOrder
gst_analytics_modelinfo_get_dims_order (GstAnalyticsModelInfo * modelinfo,
    const gchar * tensor_name)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar *dims_order_str;
  GstTensorDimOrder dims_order;

  dims_order_str = g_key_file_get_string (kf, tensor_name, "dims-order", NULL);

  /* Default to row-major if not specified */
  if (dims_order_str && g_str_equal (dims_order_str, "col-major"))
    dims_order = GST_TENSOR_DIM_ORDER_COL_MAJOR;
  else
    dims_order = GST_TENSOR_DIM_ORDER_ROW_MAJOR;

  g_free (dims_order_str);
  return dims_order;
}

/**
 * gst_analytics_modelinfo_get_version:
 * @modelinfo: Instance of #GstAnalyticsModelInfo
 *
 * Retrieve the version string of the modelinfo file format.
 *
 * The version is in the format "Major.Minor" and is stored in the
 * [modelinfo] section of the modelinfo file.
 *
 * Returns: (transfer full): The version string (e.g., "1.0").
 *    The caller must free this with g_free() when done.
 *    Defaults to "1.0" if not specified.
 *
 * Since: 1.28
 */
gchar *
gst_analytics_modelinfo_get_version (GstAnalyticsModelInfo * modelinfo)
{
  GKeyFile *kf = (GKeyFile *) modelinfo;
  gchar *version;

  if (!g_key_file_has_group (kf, GST_MODELINFO_SECTION_NAME)) {
    /* No version section means version 1.0 */
    return g_strdup ("1.0");
  }

  version = g_key_file_get_string (kf, GST_MODELINFO_SECTION_NAME,
      "version", NULL);

  if (!version) {
    /* Section exists but no version field, default to 1.0 */
    return g_strdup ("1.0");
  }

  return version;
}
