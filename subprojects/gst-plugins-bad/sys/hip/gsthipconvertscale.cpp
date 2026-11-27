/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include "gsthipconvertscale.h"
#include <gst/hip/gsthipconverter.h>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_hip_base_convert_debug);
#define GST_CAT_DEFAULT gst_hip_base_convert_debug

#define GST_HIP_CONVERT_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ARGB64, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, RGBP, BGRP, GBR, " \
    "GBRA, GBR_10LE, GBR_12LE, GBR_16LE, VUYA, YUY2, UYVY, RGBA_F32LE, RGBA_F16LE, " \
    "RGBP_F32LE, RGBP_F16LE, RGB_F32LE, RGB_F16LE, GRAY16_LE, GRAY8, GRAY_F32LE, GRAY_F16LE }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_CONVERT_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_CONVERT_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_CONVERT_FORMATS) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_HIP_CONVERT_FORMATS))
    );

static GQuark _size_quark;
static GQuark _scale_quark;
static GQuark _matrix_quark;

#define DEFAULT_ADD_BORDERS TRUE

struct _GstHipBaseConvertPrivate
{
  ~_GstHipBaseConvertPrivate ()
  {
    gst_clear_object (&conv);
    gst_clear_object (&fence_data_pool);
  }

  GstHipConverter *conv = nullptr;
  GstHipFenceDataPool *fence_data_pool = gst_hip_fence_data_pool_new ();

  gint borders_h = 0;
  gint borders_w = 0;
  gboolean add_borders = DEFAULT_ADD_BORDERS;

  /* orientation */
  /* method configured via property */
  GstVideoOrientationMethod method = GST_VIDEO_ORIENTATION_IDENTITY;
  /* method parsed from tag */
  GstVideoOrientationMethod tag_method = GST_VIDEO_ORIENTATION_IDENTITY;
  /* method currently selected based on "method" and "tag_method" */
  GstVideoOrientationMethod selected_method = GST_VIDEO_ORIENTATION_IDENTITY;
  /* method previously selected and used for negotiation */
  GstVideoOrientationMethod active_method = GST_VIDEO_ORIENTATION_IDENTITY;

  std::mutex lock;
};

static void gst_hip_base_convert_finalize (GObject * object);
static GstCaps *gst_hip_base_convert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_hip_base_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean
gst_hip_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_hip_base_convert_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_hip_base_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean gst_hip_base_convert_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_hip_base_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_hip_base_convert_set_info (GstHipBaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

#define gst_hip_base_convert_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstHipBaseConvert,
    gst_hip_base_convert, GST_TYPE_HIP_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_hip_base_convert_debug,
        "hipconvertscale", 0, "hipconvertscale"));

static void
gst_hip_base_convert_class_init (GstHipBaseConvertClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto filter_class = GST_HIP_BASE_FILTER_CLASS (klass);

  object_class->finalize = gst_hip_base_convert_finalize;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_fixate_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_decide_allocation);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_filter_meta);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_hip_base_convert_transform_meta);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_hip_base_convert_transform);

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_hip_base_convert_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_BASE_CONVERT,
      (GstPluginAPIFlags) 0);

  _size_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_SIZE_STR);
  _scale_quark = gst_video_meta_transform_scale_get_quark ();
  _matrix_quark = gst_video_meta_transform_matrix_get_quark ();
}

static void
gst_hip_base_convert_init (GstHipBaseConvert * self)
{
  self->priv = new GstHipBaseConvertPrivate ();
}

static void
gst_hip_base_convert_finalize (GObject * object)
{
  auto self = GST_HIP_BASE_CONVERT (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_hip_base_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_contains (f, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          nullptr);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  return res;
}

static GstCaps *
gst_hip_base_convert_caps_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_contains (f, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, nullptr);
      }
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  return res;
}

static GstCaps *
gst_hip_base_convert_caps_remove_format_and_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_contains (f, GST_CAPS_FEATURE_MEMORY_HIP_MEMORY)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, nullptr);
      }
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          nullptr);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }

  return res;
}

static GstCaps *
gst_hip_base_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_hip_base_convert_caps_remove_format_and_rangify_size_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_hip_base_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_HIP_BASE_CONVERT (trans);
  auto priv = self->priv;
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      priv->selected_method, NULL, (GstVideoConvertScaleFixateFlags)
      (GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT |
          GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE), caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result))
      gst_caps_replace (&result, caps);
  }

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_hip_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_HIP_BASE_FILTER (trans);
  auto self = GST_HIP_BASE_CONVERT (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == nullptr)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (caps == nullptr)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *config;

    pool = gst_hip_buffer_pool_new (filter->device);
    config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "failed to set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size by hip buffer pool */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
        nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_hip_base_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  auto self = GST_HIP_BASE_CONVERT (trans);
  auto filter = GST_HIP_BASE_FILTER (trans);
  GstCaps *outcaps = nullptr;
  GstBufferPool *pool = nullptr;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps)
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_HIP_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto hpool = GST_HIP_BUFFER_POOL (pool);
        if (!gst_hip_device_is_equal (filter->device, hpool->device))
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_from_caps (&vinfo, outcaps);
    size = GST_VIDEO_INFO_SIZE (&vinfo);
    min = max = 0;
  }

  if (!pool) {
    GST_DEBUG_OBJECT (self, "create our pool");

    pool = gst_hip_buffer_pool_new (filter->device);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by hip buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
needs_color_convert (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info)
{
  const GstVideoColorimetry *in_cinfo = &in_info->colorimetry;
  const GstVideoColorimetry *out_cinfo = &out_info->colorimetry;

  if (in_cinfo->range != out_cinfo->range ||
      in_cinfo->matrix != out_cinfo->matrix) {
    return TRUE;
  }

  if (!gst_video_color_primaries_is_equivalent (in_cinfo->primaries,
          out_cinfo->primaries)) {
    return TRUE;
  }

  if (!gst_video_transfer_function_is_equivalent (in_cinfo->transfer,
          GST_VIDEO_INFO_COMP_DEPTH (in_info, 0), out_cinfo->transfer,
          GST_VIDEO_INFO_COMP_DEPTH (out_info, 0))) {
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_hip_base_convert_set_info (GstHipBaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  auto self = GST_HIP_BASE_CONVERT (filter);
  auto priv = self->priv;
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gboolean need_flip = FALSE;
  gint in_width, in_height, in_par_n, in_par_d;
  GstVideoOrientationMethod active_method;

  gst_clear_object (&priv->conv);

  std::lock_guard < std::mutex > lk (priv->lock);
  active_method = priv->active_method = priv->selected_method;

  if (active_method != GST_VIDEO_ORIENTATION_IDENTITY)
    need_flip = TRUE;

  switch (active_method) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UL_LR:
    case GST_VIDEO_ORIENTATION_UR_LL:
      in_width = in_info->height;
      in_height = in_info->width;
      in_par_n = in_info->par_d;
      in_par_d = in_info->par_n;
      break;
    default:
      in_width = in_info->width;
      in_height = in_info->height;
      in_par_n = in_info->par_n;
      in_par_d = in_info->par_d;
      break;
  }

  if (!gst_util_fraction_multiply (in_width,
          in_height, in_par_n, in_par_d, &from_dar_n, &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (out_info->width,
          out_info->height, out_info->par_n, out_info->par_d, &to_dar_n,
          &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  priv->borders_w = priv->borders_h = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (priv->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          priv->borders_h = out_info->height - to_h;
          priv->borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          priv->borders_h = 0;
          priv->borders_w = out_info->width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (self, "Can't calculate borders");
      }
    } else {
      GST_DEBUG_OBJECT (self, "Can't keep DAR!");
    }
  }

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode) {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }

  if (in_width == out_info->width && in_height == out_info->height
      && in_info->finfo == out_info->finfo && priv->borders_w == 0 &&
      priv->borders_h == 0 && !need_flip &&
      !needs_color_convert (in_info, out_info)) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), TRUE);
  } else {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);

    priv->conv = gst_hip_converter_new (filter->device, in_info,
        out_info, nullptr);
    if (!priv->conv) {
      GST_ERROR_OBJECT (self, "Couldn't create converter");
      return FALSE;
    }

    g_object_set (priv->conv, "dest-x", priv->borders_w / 2,
        "dest-y", priv->borders_h / 2,
        "dest-width", out_info->width - priv->borders_w,
        "dest-height", out_info->height - priv->borders_h,
        "fill-border", TRUE, "video-direction", active_method, nullptr);
  }

  GST_DEBUG_OBJECT (self, "%s from=%dx%d (par=%d/%d dar=%d/%d), size %"
      G_GSIZE_FORMAT " -> %s to=%dx%d (par=%d/%d dar=%d/%d borders=%d:%d), "
      "size %" G_GSIZE_FORMAT,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      in_info->width, in_info->height, in_info->par_n, in_info->par_d,
      from_dar_n, from_dar_d, in_info->size,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)),
      out_info->width,
      out_info->height, out_info->par_n, out_info->par_d, to_dar_n, to_dar_d,
      priv->borders_w, priv->borders_h, out_info->size);

  return TRUE;
}

static gboolean
gst_hip_base_convert_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* This element cannot passthrough the crop meta, because it would convert the
   * wrong sub-region of the image, and worst, our output image may not be large
   * enough for the crop to be applied later */
  if (api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  /* propose all other metadata upstream */
  return TRUE;
}

static gboolean
gst_hip_base_convert_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  const auto info = meta->info;
  if (info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  const gchar *valid_tags[] = {
    GST_META_TAG_VIDEO_STR,
    GST_META_TAG_VIDEO_ORIENTATION_STR,
    GST_META_TAG_VIDEO_SIZE_STR,
    nullptr
  };

  if (!gst_meta_api_type_tags_contain_only (info->api, valid_tags)) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
        outbuf, meta, inbuf);
  }

  if (gst_meta_api_type_has_tag (info->api, _size_quark)) {
    if (info->transform_func) {
      auto base = GST_HIP_BASE_FILTER (trans);
      auto self = GST_HIP_BASE_CONVERT (trans);
      auto priv = self->priv;

      GstVideoMetaTransformMatrix trans_matrix;
      GstVideoMetaTransform trans = { &base->in_info, &base->out_info };
      const GstVideoRectangle in_rectangle = { 0, 0,
        GST_VIDEO_INFO_WIDTH (&base->in_info),
        GST_VIDEO_INFO_HEIGHT (&base->in_info)
      };
      const GstVideoRectangle out_rectangle = {
        priv->borders_w / 2,
        priv->borders_h / 2,
        GST_VIDEO_INFO_WIDTH (&base->out_info) - priv->borders_w,
        GST_VIDEO_INFO_HEIGHT (&base->out_info) - priv->borders_h,
      };

      gst_video_meta_transform_matrix_init (&trans_matrix,
          &base->in_info, &in_rectangle, &base->out_info, &out_rectangle);

      if (!info->transform_func (outbuf, meta, inbuf, _matrix_quark,
              &trans_matrix))
        info->transform_func (outbuf, meta, inbuf, _scale_quark, &trans);
    }

    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_hip_base_convert_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_HIP_BASE_CONVERT (trans);
  auto priv = self->priv;
  auto stream = gst_hip_device_get_stream (priv->conv->device);
  GstHipFenceData *fence_data = nullptr;
  GstHipEvent *event = nullptr;

  if (!gst_hip_stream_schedule_wait_buffer (stream, inbuf))
    return GST_FLOW_ERROR;

  if (!gst_hip_fence_data_pool_acquire (priv->fence_data_pool, &fence_data))
    return GST_FLOW_ERROR;

  auto ret = gst_hip_converter_convert_frame (priv->conv, inbuf, outbuf,
      fence_data, stream);
  if (!ret) {
    gst_hip_fence_data_unref (fence_data);
    GST_ERROR_OBJECT (self, "Failed to convert frame");
    return GST_FLOW_ERROR;
  }

  gst_hip_fence_data_push (fence_data, gst_buffer_ref (inbuf),
      (GDestroyNotify) gst_buffer_unref);

  if (gst_hip_stream_record_event (stream, &event)) {
    gst_hip_event_set_notify (event,
        fence_data, (GDestroyNotify) gst_hip_fence_data_unref);
    gst_hip_buffer_set_event (outbuf, event);
    gst_hip_event_unref (event);
  } else {
    GST_ERROR_OBJECT (self, "Couldn't record event");
    gst_hip_stream_synchronize (stream);
    gst_hip_fence_data_unref (fence_data);
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static void
gst_hip_base_convert_set_add_border (GstHipBaseConvert * self,
    gboolean add_border)
{
  auto priv = self->priv;

  std::lock_guard < std::mutex > lk (priv->lock);
  gboolean prev = priv->add_borders;

  priv->add_borders = add_border;
  if (prev != priv->add_borders)
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (self));
}

static void
gst_hip_base_convert_set_orientation (GstHipBaseConvert * self,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  auto priv = self->priv;

  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (self, "Unsupported custom orientation");
    return;
  }

  std::lock_guard < std::mutex > lk (priv->lock);
  if (from_tag)
    priv->tag_method = method;
  else
    priv->method = method;

  if (priv->method == GST_VIDEO_ORIENTATION_AUTO) {
    priv->selected_method = priv->tag_method;
  } else {
    priv->selected_method = priv->method;
  }

  if (priv->selected_method != priv->active_method) {
    GST_DEBUG_OBJECT (self, "Rotation orientation %d -> %d",
        priv->active_method, priv->selected_method);

    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (self));
  }
}

enum
{
  PROP_CONVERT_SCALE_0,
  PROP_CONVERT_SCALE_ADD_BORDERS,
  PROP_CONVERT_SCALE_VIDEO_DIRECTION,
};

struct _GstHipConvertScale
{
  GstHipBaseConvert parent;
};

static void
gst_hip_convert_scale_video_direction_interface_init (GstVideoDirectionInterface
    * iface)
{
}

static void gst_hip_convert_scale_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hip_convert_scale_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_hip_convert_scale_sink_event (GstBaseTransform * trans,
    GstEvent * event);

#define gst_hip_convert_scale_parent_class convert_scale_parent_class
G_DEFINE_TYPE_WITH_CODE (GstHipConvertScale, gst_hip_convert_scale,
    GST_TYPE_HIP_BASE_CONVERT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_DIRECTION,
        gst_hip_convert_scale_video_direction_interface_init));

static void gst_hip_convert_scale_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);

static void
gst_hip_convert_scale_class_init (GstHipConvertScaleClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->set_property = gst_hip_convert_scale_set_property;
  object_class->get_property = gst_hip_convert_scale_get_property;

  g_object_class_install_property (object_class,
      PROP_CONVERT_SCALE_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_override_property (object_class,
      PROP_CONVERT_SCALE_VIDEO_DIRECTION, "video-direction");

  gst_element_class_set_static_metadata (element_class,
      "HIP colorspace converter and scaler",
      "Filter/Converter/Video/Scaler/Colorspace/Effect/Hardware",
      "Resizes video and allow color conversion using HIP",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->passthrough_on_same_caps = FALSE;
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_hip_convert_scale_before_transform);
  trans_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_hip_convert_scale_sink_event);
}

static void
gst_hip_convert_scale_init (GstHipConvertScale * self)
{
}

static void
gst_hip_convert_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto base = GST_HIP_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_SCALE_ADD_BORDERS:
      gst_hip_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    case PROP_CONVERT_SCALE_VIDEO_DIRECTION:
      gst_hip_base_convert_set_orientation (base,
          (GstVideoOrientationMethod) g_value_get_enum (value), FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_convert_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto base = GST_HIP_BASE_CONVERT (object);
  auto priv = base->priv;

  switch (prop_id) {
    case PROP_CONVERT_SCALE_ADD_BORDERS:
      g_value_set_boolean (value, priv->add_borders);
      break;
    case PROP_CONVERT_SCALE_VIDEO_DIRECTION:
      g_value_set_enum (value, priv->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_convert_scale_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  auto base = GST_HIP_BASE_CONVERT (trans);
  auto priv = base->priv;
  GstCaps *in_caps;
  GstCaps *out_caps;
  GstBaseTransformClass *klass;

  GST_BASE_TRANSFORM_CLASS (convert_scale_parent_class)->before_transform
      (trans, buffer);

  {
    std::lock_guard < std::mutex > lk (priv->lock);
    if (priv->selected_method == priv->active_method)
      return;
  }

  /* basetransform wouldn't call set_caps if in/out caps were not changed.
   * Update it manually here */
  GST_DEBUG_OBJECT (base, "Updating caps for direction change");

  in_caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SINK_PAD (trans));
  if (!in_caps) {
    GST_WARNING_OBJECT (trans, "sinkpad has no current caps");
    return;
  }

  out_caps = gst_pad_get_current_caps (GST_BASE_TRANSFORM_SRC_PAD (trans));
  if (!out_caps) {
    GST_WARNING_OBJECT (trans, "srcpad has no current caps");
    gst_caps_unref (in_caps);
    return;
  }

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  klass->set_caps (trans, in_caps, out_caps);
  gst_caps_unref (in_caps);
  gst_caps_unref (out_caps);

  gst_base_transform_reconfigure_src (trans);
}

static gboolean
gst_hip_convert_scale_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  auto base = GST_HIP_BASE_CONVERT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *taglist;
      GstVideoOrientationMethod method = GST_VIDEO_ORIENTATION_IDENTITY;

      gst_event_parse_tag (event, &taglist);
      if (gst_video_orientation_from_tag (taglist, &method))
        gst_hip_base_convert_set_orientation (base, method, TRUE);
      break;
    }
    default:
      break;
  }

  return
      GST_BASE_TRANSFORM_CLASS (convert_scale_parent_class)->sink_event
      (trans, event);
}

struct _GstHipConvert
{
  GstHipBaseConvert parent;
};

static GstCaps *gst_hip_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_hip_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstHipConvert, gst_hip_convert, GST_TYPE_HIP_BASE_CONVERT);

static void
gst_hip_convert_class_init (GstHipConvertClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "HIP colorspace converter",
      "Filter/Converter/Video/Colorspace/Hardware",
      "Converts video from one colorspace to another using HIP",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hip_convert_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_hip_convert_fixate_caps);
}

static void
gst_hip_convert_init (GstHipConvert * self)
{
}

static GstCaps *
gst_hip_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_hip_base_convert_caps_remove_format_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_hip_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_HIP_BASE_CONVERT (base);
  auto priv = self->priv;
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      priv->selected_method, NULL, GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT,
      caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result))
      gst_caps_replace (&result, caps);
  }

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}

enum
{
  PROP_SCALE_0,
  PROP_SCALE_ADD_BORDERS,
};

struct _GstHipScale
{
  GstHipBaseConvert parent;
};

static void gst_hip_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hip_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_hip_scale_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_hip_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstHipScale, gst_hip_scale, GST_TYPE_HIP_BASE_CONVERT);

static void
gst_hip_scale_class_init (GstHipScaleClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  object_class->set_property = gst_hip_scale_set_property;
  object_class->get_property = gst_hip_scale_get_property;

  g_object_class_install_property (object_class, PROP_SCALE_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "HIP video scaler",
      "Filter/Converter/Video/Scaler/Hardware",
      "Resize video using HIP", "Seungha Yang <seungha@centricular.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_hip_scale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_hip_scale_fixate_caps);
}

static void
gst_hip_scale_init (GstHipScale * self)
{
}

static void
gst_hip_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto base = GST_HIP_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_SCALE_ADD_BORDERS:
      gst_hip_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hip_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto base = GST_HIP_BASE_CONVERT (object);
  auto priv = base->priv;

  switch (prop_id) {
    case PROP_SCALE_ADD_BORDERS:
      g_value_set_boolean (value, priv->add_borders);
      break;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_hip_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_hip_base_convert_caps_rangify_size_info (caps);

  if (filter) {
    tmp2 = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
    tmp = tmp2;
  }

  result = tmp;

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_hip_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_HIP_BASE_CONVERT (base);
  auto priv = self->priv;
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      priv->selected_method, NULL, GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE,
      caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}
