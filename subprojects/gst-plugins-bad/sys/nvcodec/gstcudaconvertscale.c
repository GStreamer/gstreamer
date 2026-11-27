/* GStreamer
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
 * Copyright (C) 2022 Seungha Yang <seungha@centricular.com>
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
#include <config.h>
#endif

#include <gst/cuda/gstcudautils.h>
#include "gstcudaconvertscale.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_base_convert_debug);
#define GST_CAT_DEFAULT gst_cuda_base_convert_debug

#define GST_CUDA_CONVERT_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, RGBP, BGRP, GBR, " \
    "GBRA, GBR_10LE, GBR_12LE, GBR_16LE, VUYA, GRAY8, GRAY16_LE }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_CONVERT_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_CONVERT_FORMATS))
    );

#define DEFAULT_ADD_BORDERS TRUE

struct _GstCudaBaseConvert
{
  GstCudaBaseTransform parent;

  GstCudaConverter *converter;
  GstCudaStream *other_stream;

  gint borders_h;
  gint borders_w;
  gboolean add_borders;
  gboolean downstream_supports_crop_meta;
  gboolean same_caps;
  GstVideoRectangle in_rect;

  /* orientation */
  /* method configured via property */
  GstVideoOrientationMethod method;
  /* method parsed from tag */
  GstVideoOrientationMethod tag_method;
  /* method currently selected based on "method" and "tag_method" */
  GstVideoOrientationMethod selected_method;
  /* method previously selected and used for negotiation */
  GstVideoOrientationMethod active_method;

  GMutex lock;
};

static void gst_cuda_base_convert_dispose (GObject * object);
static void gst_cuda_base_convert_finalize (GObject * object);
static GstCaps *gst_cuda_base_convert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_cuda_base_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean
gst_cuda_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_base_convert_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_cuda_base_convert_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstFlowReturn gst_cuda_base_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static GstFlowReturn gst_cuda_base_convert_generate_output (GstBaseTransform *
    trans, GstBuffer ** buffer);
static gboolean gst_cuda_base_convert_set_info (GstCudaBaseTransform * btrans,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

/**
 * GstCudaBaseConvert:
 *
 * A baseclass implementation for cuda convert elements
 *
 * Since: 1.22
 */
#define gst_cuda_base_convert_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstCudaBaseConvert,
    gst_cuda_base_convert, GST_TYPE_CUDA_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_cuda_base_convert_debug,
        "cudaconvertscale", 0, "CUDA Base Filter"));

static void
gst_cuda_base_convert_class_init (GstCudaBaseConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCudaBaseTransformClass *btrans_class =
      GST_CUDA_BASE_TRANSFORM_CLASS (klass);

  gobject_class->dispose = gst_cuda_base_convert_dispose;
  gobject_class->finalize = gst_cuda_base_convert_finalize;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->passthrough_on_same_caps = FALSE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_fixate_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_decide_allocation);
  trans_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_transform_meta);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_base_convert_transform);
  trans_class->generate_output =
      GST_DEBUG_FUNCPTR (gst_cuda_base_convert_generate_output);

  btrans_class->set_info = GST_DEBUG_FUNCPTR (gst_cuda_base_convert_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_CUDA_BASE_CONVERT, 0);
}

static void
gst_cuda_base_convert_init (GstCudaBaseConvert * self)
{
  self->add_borders = DEFAULT_ADD_BORDERS;
  g_mutex_init (&self->lock);
}

static void
gst_cuda_base_convert_dispose (GObject * object)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (object);

  gst_clear_cuda_stream (&self->other_stream);
  gst_clear_object (&self->converter);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_cuda_base_convert_finalize (GObject * object)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_cuda_base_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);

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
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static GstCaps *
gst_cuda_base_convert_caps_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);

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
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static GstCaps *
gst_cuda_base_convert_caps_remove_format_and_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY);

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
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

static GstCaps *
gst_cuda_base_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_cuda_base_convert_caps_remove_format_and_rangify_size_info (caps);

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
gst_cuda_base_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (trans);
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      self->selected_method, NULL, GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT |
      GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE, caps, othercaps, NULL);
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
gst_cuda_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoInfo info;
  GstBufferPool *pool;
  GstCaps *caps;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  if (self->same_caps && gst_pad_peer_query (trans->srcpad, query)) {
    gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
    return TRUE;
  }

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    GstStructure *config;

    pool = gst_cuda_buffer_pool_new (ctrans->context);

    config = gst_buffer_pool_get_config (pool);
    /* Forward downstream CUDA stream to upstream */
    if (self->other_stream) {
      GST_DEBUG_OBJECT (self, "Have downstream CUDA stream, forwarding");
      gst_buffer_pool_config_set_cuda_stream (config, self->other_stream);
    } else if (ctrans->stream) {
      GST_DEBUG_OBJECT (self, "Set our stream to proposing buffer pool");
      gst_buffer_pool_config_set_cuda_stream (config, ctrans->stream);
    }

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (ctrans, "failed to set config");
      gst_object_unref (pool);
      return FALSE;
    }

    /* Get updated size by cuda buffer pool */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);

    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_cuda_base_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (trans);
  GstCudaBaseTransform *ctrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min, max;
  GstStructure *config;
  gboolean update_pool = FALSE;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  self->downstream_supports_crop_meta = gst_query_find_allocation_meta (query,
      GST_VIDEO_CROP_META_API_TYPE, NULL);
  GST_DEBUG_OBJECT (self, "Downstream crop meta support: %d",
      self->downstream_supports_crop_meta);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        GstCudaBufferPool *cpool = GST_CUDA_BUFFER_POOL (pool);

        if (cpool->context != ctrans->context) {
          gst_clear_object (&pool);
        }
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
    GST_DEBUG_OBJECT (ctrans, "create our pool");

    pool = gst_cuda_buffer_pool_new (ctrans->context);
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_clear_cuda_stream (&self->other_stream);
  self->other_stream = gst_buffer_pool_config_get_cuda_stream (config);
  if (self->other_stream) {
    GST_DEBUG_OBJECT (self, "Downstream provided CUDA stream");
  } else if (ctrans->stream) {
    GST_DEBUG_OBJECT (self, "Set our stream to decided buffer pool");
    gst_buffer_pool_config_set_cuda_stream (config, ctrans->stream);
  }

  gst_buffer_pool_set_config (pool, config);

  /* Get updated size by cuda buffer pool */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, NULL, &size, NULL, NULL);
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
gst_cuda_base_convert_ensure_converter (GstCudaBaseConvert * self)
{
  GstCudaBaseTransform *btrans = GST_CUDA_BASE_TRANSFORM (self);

  if (self->converter)
    return TRUE;

  self->converter = gst_cuda_converter_new (btrans->context,
      &btrans->in_info, &btrans->out_info, NULL);
  if (!self->converter) {
    GST_ERROR_OBJECT (self, "Couldn't create converter");
    return FALSE;
  }

  g_object_set (self->converter, "dest-x", self->borders_w / 2,
      "dest-y", self->borders_h / 2,
      "dest-width", btrans->out_info.width - self->borders_w,
      "dest-height", btrans->out_info.height - self->borders_h,
      "fill-border", TRUE, "video-direction", self->active_method, NULL);

  return TRUE;
}

static gboolean
gst_cuda_base_convert_set_info (GstCudaBaseTransform * btrans,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (btrans);
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gboolean need_flip = FALSE;
  gint in_width, in_height, in_par_n, in_par_d;
  GstVideoOrientationMethod active_method;

  gst_clear_object (&self->converter);

  g_mutex_lock (&self->lock);
  active_method = self->active_method = self->selected_method;
  g_mutex_unlock (&self->lock);

  if (active_method != GST_VIDEO_ORIENTATION_IDENTITY)
    need_flip = TRUE;

  if (!need_flip && gst_caps_is_equal (incaps, outcaps)) {
    self->same_caps = TRUE;
  } else {
    self->same_caps = FALSE;
  }

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

  self->borders_w = self->borders_h = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (self->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          self->borders_h = out_info->height - to_h;
          self->borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          self->borders_h = 0;
          self->borders_w = out_info->width - to_w;
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
      && in_info->finfo == out_info->finfo && self->borders_w == 0 &&
      self->borders_h == 0 && !need_flip &&
      !needs_color_convert (in_info, out_info)) {
    self->same_caps = TRUE;
  }

  if (!self->same_caps) {
    self->converter =
        gst_cuda_converter_new (btrans->context, in_info, out_info, NULL);
    if (!self->converter) {
      GST_ERROR_OBJECT (self, "Couldn't create converter");
      return FALSE;
    }

    g_object_set (self->converter, "dest-x", self->borders_w / 2,
        "dest-y", self->borders_h / 2,
        "dest-width", out_info->width - self->borders_w,
        "dest-height", out_info->height - self->borders_h,
        "fill-border", TRUE, "video-direction", active_method, NULL);
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
      self->borders_w, self->borders_h, out_info->size);

  self->in_rect.x = 0;
  self->in_rect.y = 0;
  self->in_rect.w = in_info->width;
  self->in_rect.h = in_info->height;

  return TRUE;
}

static gboolean
gst_cuda_base_convert_transform_meta (GstBaseTransform * trans,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  if (meta->info->api == GST_VIDEO_CROP_META_API_TYPE)
    return FALSE;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (trans,
      outbuf, meta, inbuf);
}

static GstFlowReturn
gst_cuda_base_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (trans);
  GstCudaBaseTransform *btrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *mem;
  GstCudaMemory *in_cmem, *out_cmem;
  GstCudaStream *in_stream, *out_stream;
  GstCudaStream *selected_stream = NULL;
  GstVideoRectangle in_rect;

  GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta (inbuf);
  if (crop_meta) {
    in_rect.x = crop_meta->x;
    in_rect.y = crop_meta->y;
    in_rect.w = crop_meta->width;
    in_rect.h = crop_meta->height;
  } else {
    in_rect = self->in_rect;
  }

  if (!gst_cuda_base_convert_ensure_converter (self))
    return GST_FLOW_ERROR;

  g_object_set (self->converter, "src-x", in_rect.x, "src-y", in_rect.y,
      "src-width", in_rect.w, "src-height", in_rect.h, NULL);

  if (gst_buffer_n_memory (inbuf) != 1) {
    GST_ERROR_OBJECT (self, "Invalid input buffer");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory (inbuf, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_ERROR_OBJECT (self, "Input buffer is not CUDA");
    return GST_FLOW_ERROR;
  }
  in_cmem = GST_CUDA_MEMORY_CAST (mem);
  in_stream = gst_cuda_memory_get_stream (in_cmem);

  if (gst_buffer_n_memory (outbuf) != 1) {
    GST_ERROR_OBJECT (self, "Invalid output buffer");
    return GST_FLOW_ERROR;
  }

  mem = gst_buffer_peek_memory (outbuf, 0);
  if (!gst_is_cuda_memory (mem)) {
    GST_ERROR_OBJECT (self, "Input buffer is not CUDA");
    return GST_FLOW_ERROR;
  }
  out_cmem = GST_CUDA_MEMORY_CAST (mem);
  out_stream = gst_cuda_memory_get_stream (out_cmem);

  if (!gst_video_frame_map (&in_frame, &btrans->in_info, inbuf,
          GST_MAP_READ | GST_MAP_CUDA)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &btrans->out_info, outbuf,
          GST_MAP_WRITE | GST_MAP_CUDA)) {
    gst_video_frame_unmap (&in_frame);
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    return GST_FLOW_ERROR;
  }

  /* If downstream does not aware of CUDA stream (i.e., using default stream) */
  if (!out_stream) {
    if (in_stream) {
      GST_TRACE_OBJECT (self, "Use upstram CUDA stream");
      selected_stream = in_stream;
    } else if (btrans->stream) {
      GST_TRACE_OBJECT (self, "Use our CUDA stream");
      selected_stream = btrans->stream;
    }
  } else {
    selected_stream = out_stream;
    if (in_stream) {
      if (in_stream == out_stream) {
        GST_TRACE_OBJECT (self, "Same stream");
      } else {
        GST_TRACE_OBJECT (self, "Different CUDA stream");
        gst_cuda_memory_sync (in_cmem);
      }
    }
  }

  if (!gst_cuda_converter_convert_frame (self->converter, selected_stream,
          &in_frame, &out_frame)) {
    GST_ERROR_OBJECT (self, "Failed to convert frame");
    ret = GST_FLOW_ERROR;
  }

  if (selected_stream != out_stream) {
    GST_MEMORY_FLAG_UNSET (out_cmem, GST_CUDA_MEMORY_TRANSFER_NEED_SYNC);
    GST_TRACE_OBJECT (self, "Waiting for convert sync");
    gst_cuda_context_push (btrans->context);
    CuStreamSynchronize (gst_cuda_stream_get_handle (selected_stream));
    gst_cuda_context_pop (NULL);
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}

static GstFlowReturn
gst_cuda_base_convert_generate_output (GstBaseTransform * trans,
    GstBuffer ** buffer)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (trans);
  gboolean passthrough = self->same_caps;

  if (!trans->queued_buf)
    return GST_FLOW_OK;

  if (passthrough && !self->downstream_supports_crop_meta) {
    if (gst_buffer_get_video_crop_meta (trans->queued_buf)) {
      GST_LOG_OBJECT (self,
          "Buffer has crop meta but downstream does not support crop");
      passthrough = FALSE;
    }
  }

  if (!passthrough) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->generate_output (trans,
        buffer);
  }

  *buffer = trans->queued_buf;
  trans->queued_buf = NULL;

  return GST_FLOW_OK;
}

static void
gst_cuda_base_convert_set_add_border (GstCudaBaseConvert * self,
    gboolean add_border)
{
  gboolean prev = self->add_borders;

  self->add_borders = add_border;
  if (prev != self->add_borders)
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (self));
}

static void
gst_cuda_base_convert_set_orientation (GstCudaBaseConvert * self,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (self, "Unsupported custom orientation");
    return;
  }

  g_mutex_lock (&self->lock);
  if (from_tag)
    self->tag_method = method;
  else
    self->method = method;

  if (self->method == GST_VIDEO_ORIENTATION_AUTO) {
    self->selected_method = self->tag_method;
  } else {
    self->selected_method = self->method;
  }

  if (self->selected_method != self->active_method) {
    GST_DEBUG_OBJECT (self, "Rotation orientation %d -> %d",
        self->active_method, self->selected_method);

    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (self));
  }

  g_mutex_unlock (&self->lock);
}

/**
 * SECTION:element-cudaconvertscale
 * @title: cudaconvertscale
 * @short_description: A CUDA based color conversion and video resizing element
 *
 * This element resizes video frames and change color space.
 * By default the element will try to negotiate to the same size on the source
 * and sinkpad so that no scaling is needed.
 * It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! cudaupload ! cudaconvertscale ! cudadownload ! autovideosink
 * ```
 *
 * Since: 1.22
 */

enum
{
  PROP_CONVERT_SCALE_0,
  PROP_CONVERT_SCALE_ADD_BORDERS,
  PROP_CONVERT_SCALE_VIDEO_DIRECTION,
};

struct _GstCudaConvertScale
{
  GstCudaBaseConvert parent;
};

static void
    gst_cuda_convert_scale_video_direction_interface_init
    (GstVideoDirectionInterface * iface)
{
}

static void gst_cuda_convert_scale_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_convert_scale_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean gst_cuda_convert_scale_sink_event (GstBaseTransform * trans,
    GstEvent * event);

#define gst_cuda_convert_scale_parent_class convert_scale_parent_class
G_DEFINE_TYPE_WITH_CODE (GstCudaConvertScale, gst_cuda_convert_scale,
    GST_TYPE_CUDA_BASE_CONVERT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_DIRECTION,
        gst_cuda_convert_scale_video_direction_interface_init));

static void gst_cuda_convert_scale_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer);

static void
gst_cuda_convert_scale_class_init (GstCudaConvertScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_cuda_convert_scale_set_property;
  gobject_class->get_property = gst_cuda_convert_scale_get_property;

  g_object_class_install_property (gobject_class,
      PROP_CONVERT_SCALE_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstCudaConvertScale:video-direction:
   *
   * Video rotation/flip method to use
   *
   * Since: 1.24
   */
  g_object_class_override_property (gobject_class,
      PROP_CONVERT_SCALE_VIDEO_DIRECTION, "video-direction");

  gst_element_class_set_static_metadata (element_class,
      "CUDA colorspace converter and scaler",
      "Filter/Converter/Video/Scaler/Colorspace/Effect/Hardware",
      "Resizes video and allow color conversion using CUDA",
      "Seungha Yang <seungha@centricular.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  transform_class->passthrough_on_same_caps = FALSE;
  transform_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_cuda_convert_scale_before_transform);
  transform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_cuda_convert_scale_sink_event);
}

static void
gst_cuda_convert_scale_init (GstCudaConvertScale * self)
{
}

static void
gst_cuda_convert_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_SCALE_ADD_BORDERS:
      gst_cuda_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    case PROP_CONVERT_SCALE_VIDEO_DIRECTION:
      gst_cuda_base_convert_set_orientation (base, g_value_get_enum (value),
          FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_convert_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_SCALE_ADD_BORDERS:
      g_value_set_boolean (value, base->add_borders);
      break;
    case PROP_CONVERT_SCALE_VIDEO_DIRECTION:
      g_value_set_enum (value, base->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_convert_scale_before_transform (GstBaseTransform * trans,
    GstBuffer * buffer)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (trans);
  gboolean update = FALSE;
  GstCaps *in_caps;
  GstCaps *out_caps;
  GstBaseTransformClass *klass;

  GST_BASE_TRANSFORM_CLASS (convert_scale_parent_class)->before_transform
      (trans, buffer);

  g_mutex_lock (&base->lock);
  if (base->selected_method != base->active_method)
    update = TRUE;
  g_mutex_unlock (&base->lock);

  if (!update)
    return;

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
gst_cuda_convert_scale_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *taglist;
      GstVideoOrientationMethod method = GST_VIDEO_ORIENTATION_IDENTITY;

      gst_event_parse_tag (event, &taglist);
      if (gst_video_orientation_from_tag (taglist, &method))
        gst_cuda_base_convert_set_orientation (base, method, TRUE);
      break;
    }
    default:
      break;
  }

  return
      GST_BASE_TRANSFORM_CLASS (convert_scale_parent_class)->sink_event
      (trans, event);
}

/**
 * SECTION:element-cudaconvert
 * @title: cudaconvert
 *
 * Convert video frames between supported video formats.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! cudaupload ! cudaconvert ! cudadownload ! autovideosink
 * ```
 *
 * Since: 1.20
 */

struct _GstCudaConvert
{
  GstCudaBaseConvert parent;
};

static GstCaps *gst_cuda_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_cuda_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstCudaConvert, gst_cuda_convert, GST_TYPE_CUDA_BASE_CONVERT);

static void
gst_cuda_convert_class_init (GstCudaConvertClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "CUDA colorspace converter",
      "Filter/Converter/Video/Colorspace/Hardware",
      "Converts video from one colorspace to another using CUDA",
      "Seungha Yang <seungha.yang@navercorp.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_convert_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_cuda_convert_fixate_caps);
}

static void
gst_cuda_convert_init (GstCudaConvert * self)
{
}

static GstCaps *
gst_cuda_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_cuda_base_convert_caps_remove_format_info (caps);

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
gst_cuda_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (base);
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      self->selected_method, NULL, GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT,
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

/**
 * SECTION:element-cudascale
 * @title: cudascale
 *
 * A CUDA based video resizing element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=480 ! cudaupload ! cudascale ! cudadownload ! video/x-raw,width=1280,height=720 ! fakesink
 * ```
 *  This will upload a 640x480 resolution test video to CUDA
 * memory space and resize it to 1280x720 resolution. Then a resized CUDA
 * frame will be downloaded to system memory space.
 *
 * Since: 1.20
 */

enum
{
  PROP_SCALE_0,
  PROP_SCALE_ADD_BORDERS,
};

struct _GstCudaScale
{
  GstCudaBaseConvert parent;
};

static void gst_cuda_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cuda_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_cuda_scale_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_cuda_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstCudaScale, gst_cuda_scale, GST_TYPE_CUDA_BASE_CONVERT);

static void
gst_cuda_scale_class_init (GstCudaScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_cuda_scale_set_property;
  gobject_class->get_property = gst_cuda_scale_get_property;

  /**
   * GstCudaScale:add-borders:
   *
   * Add borders if necessary to keep the display aspect ratio
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_SCALE_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "CUDA video scaler",
      "Filter/Converter/Video/Scaler/Hardware",
      "Resize video using CUDA", "Seungha Yang <seungha.yang@navercorp.com>");
  gst_element_class_mark_as_untrusted_aware (element_class);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_scale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_cuda_scale_fixate_caps);
}

static void
gst_cuda_scale_init (GstCudaScale * self)
{
}

static void
gst_cuda_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_SCALE_ADD_BORDERS:
      gst_cuda_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaBaseConvert *base = GST_CUDA_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_SCALE_ADD_BORDERS:
      g_value_set_boolean (value, base->add_borders);
      break;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_cuda_scale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_cuda_base_convert_caps_rangify_size_info (caps);

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
gst_cuda_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCudaBaseConvert *self = GST_CUDA_BASE_CONVERT (base);
  GstCaps *result;

  result =
      gst_video_convertscale_fixate_caps (GST_OBJECT (self), direction,
      self->selected_method, NULL, GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE,
      caps, othercaps, NULL);
  if (!result)
    return othercaps;

  gst_clear_caps (&othercaps);

  GST_DEBUG_OBJECT (self, "fixated othercaps to %" GST_PTR_FORMAT, result);

  return result;
}
