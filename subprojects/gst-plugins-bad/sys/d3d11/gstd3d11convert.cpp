/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 * Copyright (C) 2012-2014 Matthew Waters <ystree00@gmail.com>
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) <2019> Jeongki Kim <jeongki.kim@jeongki.kim>
 * Copyright (C) 2020 Thibault Saunier <tsaunier@igalia.com>
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
#  include <config.h>
#endif

#include "gstd3d11convert.h"
#include "gstd3d11converter.h"
#include "gstd3d11videoprocessor.h"
#include "gstd3d11pluginutils.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_convert_debug);
#define GST_CAT_DEFAULT gst_d3d11_convert_debug

static GstStaticCaps sink_template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SINK_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_SINK_FORMATS));

static GstStaticCaps src_template_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_SRC_FORMATS) "; "
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES
    (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_D3D11_SRC_FORMATS));

#define DEFAULT_ADD_BORDERS TRUE

struct _GstD3D11BaseConvert
{
  GstD3D11BaseFilter parent;

  const GstD3D11Format *in_d3d11_format;
  const GstD3D11Format *out_d3d11_format;

  ID3D11Texture2D *in_texture[GST_VIDEO_MAX_PLANES];
  ID3D11ShaderResourceView *shader_resource_view[GST_VIDEO_MAX_PLANES];
  guint num_input_view;

  ID3D11Texture2D *out_texture[GST_VIDEO_MAX_PLANES];
  ID3D11RenderTargetView *render_target_view[GST_VIDEO_MAX_PLANES];
  guint num_output_view;

  GstD3D11Converter *converter;
  GstD3D11VideoProcessor *processor;
  gboolean processor_in_use;

  /* used for border rendering */
  RECT in_rect;
  RECT out_rect;

  gint borders_h;
  gint borders_w;

  /* Updated by subclass */
  gboolean add_borders;
};

/**
 * GstD3D11BaseConvert:
 *
 * A baseclass implementation for d3d11 convert elements
 *
 * Since: 1.20
 */
#define gst_d3d11_base_convert_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstD3D11BaseConvert, gst_d3d11_base_convert,
    GST_TYPE_D3D11_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_d3d11_convert_debug, "d3d11convert", 0,
        "d3d11convert"));

static void gst_d3d11_base_convert_dispose (GObject * object);
static GstCaps *gst_d3d11_base_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_base_convert_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d11_base_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean
gst_d3d11_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_d3d11_base_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

static GstFlowReturn gst_d3d11_base_convert_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d11_base_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

/* copies the given caps */
static GstCaps *
gst_d3d11_base_convert_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);

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
gst_d3d11_base_convert_caps_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);

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
gst_d3d11_base_convert_caps_remove_format_and_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY);

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

static void
gst_d3d11_base_convert_class_init (GstD3D11BaseConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstD3D11BaseFilterClass *bfilter_class = GST_D3D11_BASE_FILTER_CLASS (klass);
  GstCaps *caps;

  gobject_class->dispose = gst_d3d11_base_convert_dispose;

  caps = gst_d3d11_get_updated_template_caps (&sink_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

  caps = gst_d3d11_get_updated_template_caps (&src_template_caps);
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
  gst_caps_unref (caps);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_fixate_caps);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_filter_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_decide_allocation);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_transform);

  bfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_d3d11_base_convert_set_info);

  gst_type_mark_as_plugin_api (GST_TYPE_D3D11_BASE_CONVERT,
      (GstPluginAPIFlags) 0);
}

static void
gst_d3d11_base_convert_init (GstD3D11BaseConvert * self)
{
  self->add_borders = DEFAULT_ADD_BORDERS;
}

static void
gst_d3d11_base_convert_clear_shader_resource (GstD3D11BaseConvert * self)
{
  gint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (self->shader_resource_view[i]);
    GST_D3D11_CLEAR_COM (self->render_target_view[i]);
  }

  self->num_input_view = 0;
  self->num_output_view = 0;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (self->in_texture[i]);
    GST_D3D11_CLEAR_COM (self->out_texture[i]);
  }

  g_clear_pointer (&self->converter, gst_d3d11_converter_free);
  g_clear_pointer (&self->processor, gst_d3d11_video_processor_free);

  self->processor_in_use = FALSE;
}

static void
gst_d3d11_base_convert_dispose (GObject * object)
{
  GstD3D11BaseConvert *self = GST_D3D11_BASE_CONVERT (object);

  gst_d3d11_base_convert_clear_shader_resource (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_d3d11_base_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d11_base_convert_caps_remove_format_and_rangify_size_info (caps);

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

/*
 * This is an incomplete matrix of in formats and a score for the prefered output
 * format.
 *
 *         out: RGB24   RGB16  ARGB  AYUV  YUV444  YUV422 YUV420 YUV411 YUV410  PAL  GRAY
 *  in
 * RGB24          0      2       1     2     2       3      4      5      6      7    8
 * RGB16          1      0       1     2     2       3      4      5      6      7    8
 * ARGB           2      3       0     1     4       5      6      7      8      9    10
 * AYUV           3      4       1     0     2       5      6      7      8      9    10
 * YUV444         2      4       3     1     0       5      6      7      8      9    10
 * YUV422         3      5       4     2     1       0      6      7      8      9    10
 * YUV420         4      6       5     3     2       1      0      7      8      9    10
 * YUV411         4      6       5     3     2       1      7      0      8      9    10
 * YUV410         6      8       7     5     4       3      2      1      0      9    10
 * PAL            1      3       2     6     4       6      7      8      9      0    10
 * GRAY           1      4       3     2     1       5      6      7      8      9    0
 *
 * PAL or GRAY are never prefered, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is prefered and if any, preferably horizontal
 * We would like to keep the alpha, even if we would need to to colorspace conversion
 * or lose depth.
 */
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical subsample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal subsample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

/* calculate how much loss a conversion would be */
static void
score_value (GstBaseTransform * base, const GstVideoFormatInfo * in_info,
    const GValue * val, gint * min_loss, const GstVideoFormatInfo ** out_info)
{
  const gchar *fname;
  const GstVideoFormatInfo *t_info;
  guint in_flags, t_flags;
  gint loss;

  fname = g_value_get_string (val);
  t_info = gst_video_format_get_info (gst_video_format_from_string (fname));
  if (!t_info || t_info->format == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  /* accept input format immediately without loss */
  if (in_info == t_info) {
    *min_loss = 0;
    *out_info = t_info;
    return;
  }

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  t_flags = GST_VIDEO_FORMAT_INFO_FLAGS (t_info);
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (t_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (t_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (t_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (t_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (t_info->bits))
      loss += SCORE_DEPTH_LOSS + (in_info->bits - t_info->bits);
  }

  GST_DEBUG_OBJECT (base, "score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (in_info),
      GST_VIDEO_FORMAT_INFO_NAME (t_info), loss);

  if (loss < *min_loss) {
    GST_DEBUG_OBJECT (base, "found new best %d", loss);
    *out_info = t_info;
    *min_loss = loss;
  }
}

static void
gst_d3d11_base_convert_fixate_format (GstBaseTransform * trans,
    GstCaps * caps, GstCaps * result)
{
  GstStructure *ins, *outs;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = NULL;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  ins = gst_caps_get_structure (caps, 0);
  in_format = gst_structure_get_string (ins, "format");
  if (!in_format) {
    return;
  }

  GST_DEBUG_OBJECT (trans, "source format %s", in_format);

  in_info =
      gst_video_format_get_info (gst_video_format_from_string (in_format));
  if (!in_info)
    return;

  outs = gst_caps_get_structure (result, 0);

  capslen = gst_caps_get_size (result);
  GST_DEBUG ("iterate %d structures", capslen);
  for (i = 0; i < capslen; i++) {
    GstStructure *tests;
    const GValue *format;

    tests = gst_caps_get_structure (result, i);
    format = gst_structure_get_value (tests, "format");

    /* should not happen */
    if (format == NULL)
      continue;

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      GST_DEBUG_OBJECT (trans, "have %d formats", len);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          score_value (trans, in_info, val, &min_loss, &out_info);
          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      score_value (trans, in_info, format, &min_loss, &out_info);
    }
  }
  if (out_info)
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (out_info), NULL);
}

static gboolean
subsampling_unchanged (GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  guint i;
  const GstVideoFormatInfo *in_format, *out_format;

  if (GST_VIDEO_INFO_N_COMPONENTS (in_info) !=
      GST_VIDEO_INFO_N_COMPONENTS (out_info))
    return FALSE;

  in_format = in_info->finfo;
  out_format = out_info->finfo;

  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (in_info); i++) {
    if (GST_VIDEO_FORMAT_INFO_W_SUB (in_format,
            i) != GST_VIDEO_FORMAT_INFO_W_SUB (out_format, i))
      return FALSE;
    if (GST_VIDEO_FORMAT_INFO_H_SUB (in_format,
            i) != GST_VIDEO_FORMAT_INFO_H_SUB (out_format, i))
      return FALSE;
  }

  return TRUE;
}

static void
transfer_colorimetry_from_input (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstStructure *out_caps_s = gst_caps_get_structure (out_caps, 0);
  GstStructure *in_caps_s = gst_caps_get_structure (in_caps, 0);
  gboolean have_colorimetry =
      gst_structure_has_field (out_caps_s, "colorimetry");
  gboolean have_chroma_site =
      gst_structure_has_field (out_caps_s, "chroma-site");

  /* If the output already has colorimetry and chroma-site, stop,
   * otherwise try and transfer what we can from the input caps */
  if (have_colorimetry && have_chroma_site)
    return;

  {
    GstVideoInfo in_info, out_info;
    const GValue *in_colorimetry =
        gst_structure_get_value (in_caps_s, "colorimetry");

    if (!gst_video_info_from_caps (&in_info, in_caps)) {
      GST_WARNING_OBJECT (trans,
          "Failed to convert sink pad caps to video info");
      return;
    }
    if (!gst_video_info_from_caps (&out_info, out_caps)) {
      GST_WARNING_OBJECT (trans,
          "Failed to convert src pad caps to video info");
      return;
    }

    if (!have_colorimetry && in_colorimetry != NULL) {
      if ((GST_VIDEO_INFO_IS_YUV (&out_info)
              && GST_VIDEO_INFO_IS_YUV (&in_info))
          || (GST_VIDEO_INFO_IS_RGB (&out_info)
              && GST_VIDEO_INFO_IS_RGB (&in_info))
          || (GST_VIDEO_INFO_IS_GRAY (&out_info)
              && GST_VIDEO_INFO_IS_GRAY (&in_info))) {
        /* Can transfer the colorimetry intact from the input if it has it */
        gst_structure_set_value (out_caps_s, "colorimetry", in_colorimetry);
      } else {
        gchar *colorimetry_str;

        /* Changing between YUV/RGB - forward primaries and transfer function, but use
         * default range and matrix.
         * the primaries is used for conversion between RGB and XYZ (CIE 1931 coordinate).
         * the transfer function could be another reference (e.g., HDR)
         */
        out_info.colorimetry.primaries = in_info.colorimetry.primaries;
        out_info.colorimetry.transfer = in_info.colorimetry.transfer;

        colorimetry_str =
            gst_video_colorimetry_to_string (&out_info.colorimetry);
        gst_caps_set_simple (out_caps, "colorimetry", G_TYPE_STRING,
            colorimetry_str, NULL);
        g_free (colorimetry_str);
      }
    }

    /* Only YUV output needs chroma-site. If the input was also YUV and had the same chroma
     * subsampling, transfer the siting. If the sub-sampling is changing, then the planes get
     * scaled anyway so there's no real reason to prefer the input siting. */
    if (!have_chroma_site && GST_VIDEO_INFO_IS_YUV (&out_info)) {
      if (GST_VIDEO_INFO_IS_YUV (&in_info)) {
        const GValue *in_chroma_site =
            gst_structure_get_value (in_caps_s, "chroma-site");
        if (in_chroma_site != NULL
            && subsampling_unchanged (&in_info, &out_info))
          gst_structure_set_value (out_caps_s, "chroma-site", in_chroma_site);
      }
    }
  }
}

static GstCaps *
gst_d3d11_base_convert_get_fixed_format (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = gst_caps_copy (othercaps);
  }

  gst_d3d11_base_convert_fixate_format (trans, caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    } else {
      /* Try and preserve input colorimetry / chroma information */
      transfer_colorimetry_from_input (trans, caps, result);
    }
  }

  return result;
}

static GstCaps *
gst_d3d11_base_convert_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, NULL);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int_round (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int_round (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              NULL);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scale sized - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int_round (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the dimensions with the DAR that was closest
       * to the correct DAR. This changes the DAR but there's not much else to
       * do here.
       */
      if (set_w * ABS (set_h - h) < ABS (f_w - w) * f_h) {
        f_h = set_h;
        f_w = set_w;
      }
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_d3d11_base_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *format = NULL;

  GST_DEBUG_OBJECT (base,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  format = gst_d3d11_base_convert_get_fixed_format (base, direction, caps,
      othercaps);

  if (gst_caps_is_empty (format)) {
    GST_ERROR_OBJECT (base, "Could not convert formats");
    return format;
  }

  /* convert mode is "all" or "size" here */
  othercaps =
      gst_d3d11_base_convert_fixate_size (base, direction, caps, othercaps);

  if (gst_caps_get_size (othercaps) == 1) {
    guint i;
    const gchar *format_fields[] = { "format", "colorimetry", "chroma-site" };
    GstStructure *format_struct = gst_caps_get_structure (format, 0);
    GstStructure *fixated_struct;

    othercaps = gst_caps_make_writable (othercaps);
    fixated_struct = gst_caps_get_structure (othercaps, 0);

    for (i = 0; i < G_N_ELEMENTS (format_fields); i++) {
      if (gst_structure_has_field (format_struct, format_fields[i])) {
        gst_structure_set (fixated_struct, format_fields[i], G_TYPE_STRING,
            gst_structure_get_string (format_struct, format_fields[i]), NULL);
      } else {
        gst_structure_remove_field (fixated_struct, format_fields[i]);
      }
    }
  }
  gst_caps_unref (format);

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_d3d11_base_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params)
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
gst_d3d11_base_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstVideoInfo info;
  GstBufferPool *pool = NULL;
  GstCaps *caps;
  guint n_pools, i;
  GstStructure *config;
  guint size;
  GstD3D11AllocationParams *d3d11_params;
  const GstD3D11Format *d3d11_format;
  guint bind_flags = D3D11_BIND_SHADER_RESOURCE;
  DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
  UINT supported = 0;
  HRESULT hr;
  ID3D11Device *device_handle;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  d3d11_format = gst_d3d11_device_format_from_gst (filter->device,
      GST_VIDEO_INFO_FORMAT (&info));
  if (!d3d11_format) {
    GST_ERROR_OBJECT (filter, "Unknown format caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  if (d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    dxgi_format = d3d11_format->resource_format[0];
  } else {
    dxgi_format = d3d11_format->dxgi_format;
  }

  device_handle = gst_d3d11_device_get_device_handle (filter->device);
  hr = device_handle->CheckFormatSupport (dxgi_format, &supported);
  if (gst_d3d11_result (hr, filter->device) &&
      (supported & D3D11_FORMAT_SUPPORT_RENDER_TARGET) ==
      D3D11_FORMAT_SUPPORT_RENDER_TARGET) {
    bind_flags |= D3D11_BIND_RENDER_TARGET;
  }

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (pool) {
      if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
        if (dpool->device != filter->device)
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (filter->device, &info,
        (GstD3D11AllocationFlags) 0, bind_flags);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |= bind_flags;
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  /* size will be updated by d3d11 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, 0, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config))
    goto config_failed;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);

  /* d3d11 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
config_failed:
  {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }
}

static gboolean
gst_d3d11_base_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstCaps *outcaps = NULL;
  GstBufferPool *pool = NULL;
  guint size, min = 0, max = 0;
  GstStructure *config;
  GstD3D11AllocationParams *d3d11_params;
  gboolean update_pool = FALSE;
  GstVideoInfo info;
  guint i;
  const GstD3D11Format *d3d11_format;
  guint bind_flags = D3D11_BIND_RENDER_TARGET;
  DXGI_FORMAT dxgi_format = DXGI_FORMAT_UNKNOWN;
  UINT supported = 0;
  HRESULT hr;
  ID3D11Device *device_handle;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  d3d11_format = gst_d3d11_device_format_from_gst (filter->device,
      GST_VIDEO_INFO_FORMAT (&info));
  if (!d3d11_format) {
    GST_ERROR_OBJECT (filter, "Unknown format caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  if (d3d11_format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    dxgi_format = d3d11_format->resource_format[0];
  } else {
    dxgi_format = d3d11_format->dxgi_format;
  }

  device_handle = gst_d3d11_device_get_device_handle (filter->device);
  hr = device_handle->CheckFormatSupport (dxgi_format, &supported);
  if (gst_d3d11_result (hr, filter->device) &&
      (supported & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) ==
      D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) {
    bind_flags |= D3D11_BIND_SHADER_RESOURCE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        GstD3D11BufferPool *dpool = GST_D3D11_BUFFER_POOL (pool);
        if (dpool->device != filter->device)
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (filter->device, &info,
        (GstD3D11AllocationFlags) 0, bind_flags);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |= bind_flags;
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* d3d11 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
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
create_shader_input_resource (GstD3D11BaseConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc;
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_input_view)
    return TRUE;

  memset (&texture_desc, 0, sizeof (texture_desc));
  memset (&view_desc, 0, sizeof (view_desc));

  device_handle = gst_d3d11_device_get_device_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = device_handle->CreateTexture2D (&texture_desc, NULL, &tex[i]);
      if (!gst_d3d11_result (hr, device)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    gboolean is_semiplanar = FALSE;

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010 ||
        format->dxgi_format == DXGI_FORMAT_P016)
      is_semiplanar = TRUE;

    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    /* semiplanar format resolution of should be even number */
    if (is_semiplanar) {
      texture_desc.Width = GST_ROUND_UP_2 (texture_desc.Width);
      texture_desc.Height = GST_ROUND_UP_2 (texture_desc.Height);
    }

    hr = device_handle->CreateTexture2D (&texture_desc, NULL, &tex[0]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (is_semiplanar) {
      tex[0]->AddRef ();
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipLevels = 1;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = device_handle->CreateShaderResourceView (tex[i], &view_desc, &view[i]);

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self,
          "Failed to create resource view (0x%x)", (guint) hr);
      goto error;
    }
  }

  self->num_input_view = i;

  GST_DEBUG_OBJECT (self,
      "%d shader resource view created", self->num_input_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->in_texture[i] = tex[i];
    self->shader_resource_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (tex[i]);
  }

  return FALSE;
}

/* 16.0 / 255.0 ~= 0.062745 */
static const float luma_black_level_limited = 0.062745f;

static inline void
clear_rtv_color_rgb (GstD3D11BaseConvert * self,
    ID3D11DeviceContext * context_handle, ID3D11RenderTargetView * rtv,
    gboolean full_range)
{
  const FLOAT clear_color_full[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  const FLOAT clear_color_limited[4] =
      { luma_black_level_limited, luma_black_level_limited,
    luma_black_level_limited, 1.0f
  };
  const FLOAT *target;

  if (full_range)
    target = clear_color_full;
  else
    target = clear_color_limited;

  context_handle->ClearRenderTargetView (rtv, target);
}

static inline void
clear_rtv_color_vuya (GstD3D11BaseConvert * self,
    ID3D11DeviceContext * context_handle, ID3D11RenderTargetView * rtv,
    gboolean full_range)
{
  const FLOAT clear_color_full[4] = { 0.5f, 0.5f, 0.0f, 1.0f };
  const FLOAT clear_color_limited[4] =
      { 0.5f, 0.5f, luma_black_level_limited, 1.0f };
  const FLOAT *target;

  if (full_range)
    target = clear_color_full;
  else
    target = clear_color_limited;

  context_handle->ClearRenderTargetView (rtv, target);
}

static inline void
clear_rtv_color_luma (GstD3D11BaseConvert * self,
    ID3D11DeviceContext * context_handle, ID3D11RenderTargetView * rtv,
    gboolean full_range)
{
  const FLOAT clear_color_full[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  const FLOAT clear_color_limited[4] =
      { luma_black_level_limited, luma_black_level_limited,
    luma_black_level_limited, 1.0f
  };
  const FLOAT *target;

  if (full_range)
    target = clear_color_full;
  else
    target = clear_color_limited;

  context_handle->ClearRenderTargetView (rtv, target);
}

static inline void
clear_rtv_color_chroma (GstD3D11BaseConvert * self,
    ID3D11DeviceContext * context_handle, ID3D11RenderTargetView * rtv)
{
  const FLOAT clear_color[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

  context_handle->ClearRenderTargetView (rtv, clear_color);
}

static void
clear_rtv_color_all (GstD3D11BaseConvert * self, GstVideoInfo * info,
    ID3D11DeviceContext * context_handle,
    ID3D11RenderTargetView * rtv[GST_VIDEO_MAX_PLANES])
{
  gint i;
  gboolean full_range = info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (!rtv[i])
      break;

    if (GST_VIDEO_INFO_IS_RGB (info)) {
      clear_rtv_color_rgb (self, context_handle, rtv[i], full_range);
    } else {
      if (GST_VIDEO_INFO_N_PLANES (info) == 1) {
        clear_rtv_color_vuya (self, context_handle, rtv[i], full_range);
      } else {
        if (i == 0)
          clear_rtv_color_luma (self, context_handle, rtv[i], full_range);
        else
          clear_rtv_color_chroma (self, context_handle, rtv[i]);
      }
    }
  }
}

static gboolean
create_shader_output_resource (GstD3D11BaseConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc;
  D3D11_RENDER_TARGET_VIEW_DESC view_desc;
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_output_view)
    return TRUE;

  memset (&texture_desc, 0, sizeof (texture_desc));
  memset (&view_desc, 0, sizeof (view_desc));

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Usage = D3D11_USAGE_DEFAULT;
  texture_desc.BindFlags =
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

  if (format->dxgi_format == DXGI_FORMAT_UNKNOWN) {
    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
      if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
        break;

      texture_desc.Width = GST_VIDEO_INFO_COMP_WIDTH (info, i);
      texture_desc.Height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);
      texture_desc.Format = format->resource_format[i];

      hr = device_handle->CreateTexture2D (&texture_desc, NULL, &tex[i]);
      if (!gst_d3d11_result (hr, device)) {
        GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
        goto error;
      }
    }
  } else {
    gboolean is_semiplanar = FALSE;

    if (format->dxgi_format == DXGI_FORMAT_NV12 ||
        format->dxgi_format == DXGI_FORMAT_P010 ||
        format->dxgi_format == DXGI_FORMAT_P016)
      is_semiplanar = TRUE;

    texture_desc.Width = GST_VIDEO_INFO_WIDTH (info);
    texture_desc.Height = GST_VIDEO_INFO_HEIGHT (info);
    texture_desc.Format = format->dxgi_format;

    /* semiplanar format resolution of should be even number */
    if (is_semiplanar) {
      texture_desc.Width = GST_ROUND_UP_2 (texture_desc.Width);
      texture_desc.Height = GST_ROUND_UP_2 (texture_desc.Height);
    }

    hr = device_handle->CreateTexture2D (&texture_desc, NULL, &tex[0]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (is_semiplanar) {
      tex[0]->AddRef ();
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipSlice = 0;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = device_handle->CreateRenderTargetView (tex[i], &view_desc, &view[i]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self,
          "Failed to create %dth render target view (0x%x)", i, (guint) hr);
      goto error;
    }
  }

  gst_d3d11_device_lock (device);
  clear_rtv_color_all (self, info, context_handle, view);
  gst_d3d11_device_unlock (device);

  self->num_output_view = i;

  GST_DEBUG_OBJECT (self, "%d render view created", self->num_output_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->out_texture[i] = tex[i];
    self->render_target_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    GST_D3D11_CLEAR_COM (tex[i]);
  }

  return FALSE;
}

static gboolean
gst_d3d11_base_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstD3D11BaseConvert *self = GST_D3D11_BASE_CONVERT (filter);
  const GstVideoInfo *unknown_info;
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  D3D11_VIEWPORT view_port;
  gint border_offset_x = 0;
  gint border_offset_y = 0;

  if (gst_base_transform_is_passthrough (GST_BASE_TRANSFORM (filter)))
    return TRUE;

  if (!gst_util_fraction_multiply (in_info->width,
          in_info->height, in_info->par_n, in_info->par_d, &from_dar_n,
          &from_dar_d)) {
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
      GST_INFO_OBJECT (self, "Display aspect ratio update %d/%d -> %d/%d",
          from_dar_n, from_dar_d, to_dar_n, to_dar_d);
    }
  }

  gst_d3d11_base_convert_clear_shader_resource (self);

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  if (in_info->width == out_info->width && in_info->height == out_info->height
      && in_info->finfo == out_info->finfo && self->borders_w == 0 &&
      self->borders_h == 0) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
    return TRUE;
  } else {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);
  }

  self->in_d3d11_format =
      gst_d3d11_device_format_from_gst (filter->device,
      GST_VIDEO_INFO_FORMAT (in_info));
  if (!self->in_d3d11_format) {
    unknown_info = in_info;
    goto format_unknown;
  }

  self->out_d3d11_format =
      gst_d3d11_device_format_from_gst (filter->device,
      GST_VIDEO_INFO_FORMAT (out_info));
  if (!self->out_d3d11_format) {
    unknown_info = out_info;
    goto format_unknown;
  }

  self->converter =
      gst_d3d11_converter_new (filter->device, in_info, out_info, nullptr);

  if (!self->converter) {
    GST_ERROR_OBJECT (self, "couldn't set converter");
    return FALSE;
  }
#if (GST_D3D11_DXGI_HEADER_VERSION >= 4)
  /* If both input and output formats are native DXGI format */
  if (self->in_d3d11_format->dxgi_format != DXGI_FORMAT_UNKNOWN &&
      self->out_d3d11_format->dxgi_format != DXGI_FORMAT_UNKNOWN) {
    gboolean hardware = FALSE;
    GstD3D11VideoProcessor *processor = NULL;

    gst_d3d11_device_lock (filter->device);
    g_object_get (filter->device, "hardware", &hardware, NULL);
    if (hardware) {
      processor = gst_d3d11_video_processor_new (filter->device,
          in_info->width, in_info->height, out_info->width, out_info->height);
    }

    if (processor) {
      const GstDxgiColorSpace *in_color_space;
      const GstDxgiColorSpace *out_color_space;

      in_color_space = gst_d3d11_video_info_to_dxgi_color_space (in_info);
      out_color_space = gst_d3d11_video_info_to_dxgi_color_space (out_info);

      if (in_color_space && out_color_space) {
        DXGI_FORMAT in_dxgi_format = self->in_d3d11_format->dxgi_format;
        DXGI_FORMAT out_dxgi_format = self->out_d3d11_format->dxgi_format;
        DXGI_COLOR_SPACE_TYPE in_dxgi_color_space =
            (DXGI_COLOR_SPACE_TYPE) in_color_space->dxgi_color_space_type;
        DXGI_COLOR_SPACE_TYPE out_dxgi_color_space =
            (DXGI_COLOR_SPACE_TYPE) out_color_space->dxgi_color_space_type;

        if (!gst_d3d11_video_processor_check_format_conversion (processor,
                in_dxgi_format, in_dxgi_color_space, out_dxgi_format,
                out_dxgi_color_space)) {
          GST_DEBUG_OBJECT (self, "Conversion is not supported by device");
          gst_d3d11_video_processor_free (processor);
          processor = NULL;
        } else {
          GST_DEBUG_OBJECT (self, "video processor supports conversion");
          gst_d3d11_video_processor_set_input_dxgi_color_space (processor,
              in_dxgi_color_space);
          gst_d3d11_video_processor_set_output_dxgi_color_space (processor,
              out_dxgi_color_space);
        }
      } else {
        GST_WARNING_OBJECT (self,
            "Couldn't determine input and/or output dxgi colorspace");
        gst_d3d11_video_processor_free (processor);
        processor = NULL;
      }
    }

    self->processor = processor;
    gst_d3d11_device_unlock (filter->device);
  }
#endif

  GST_DEBUG_OBJECT (self, "from=%dx%d (par=%d/%d dar=%d/%d), size %"
      G_GSIZE_FORMAT " -> to=%dx%d (par=%d/%d dar=%d/%d borders=%d:%d), "
      "size %" G_GSIZE_FORMAT,
      in_info->width, in_info->height, in_info->par_n, in_info->par_d,
      from_dar_n, from_dar_d, in_info->size, out_info->width,
      out_info->height, out_info->par_n, out_info->par_d, to_dar_n, to_dar_d,
      self->borders_w, self->borders_h, out_info->size);

  self->in_rect.left = 0;
  self->in_rect.top = 0;
  self->in_rect.right = GST_VIDEO_INFO_WIDTH (in_info);
  self->in_rect.bottom = GST_VIDEO_INFO_HEIGHT (in_info);

  if (self->borders_w) {
    border_offset_x = self->borders_w / 2;
    self->out_rect.left = border_offset_x;
    self->out_rect.right = GST_VIDEO_INFO_WIDTH (out_info) - border_offset_x;
  } else {
    self->out_rect.left = 0;
    self->out_rect.right = GST_VIDEO_INFO_WIDTH (out_info);
  }

  if (self->borders_h) {
    border_offset_y = self->borders_h / 2;
    self->out_rect.top = border_offset_y;
    self->out_rect.bottom = GST_VIDEO_INFO_HEIGHT (out_info) - border_offset_y;
  } else {
    self->out_rect.top = 0;
    self->out_rect.bottom = GST_VIDEO_INFO_HEIGHT (out_info);
  }

  view_port.TopLeftX = border_offset_x;
  view_port.TopLeftY = border_offset_y;
  view_port.Width = GST_VIDEO_INFO_WIDTH (out_info) - self->borders_w;
  view_port.Height = GST_VIDEO_INFO_HEIGHT (out_info) - self->borders_h;
  view_port.MinDepth = 0.0f;
  view_port.MaxDepth = 1.0f;

  gst_d3d11_converter_update_viewport (self->converter, &view_port);

  return TRUE;

  /* ERRORS */
format_mismatch:
  {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }
format_unknown:
  {
    GST_ERROR_OBJECT (self,
        "%s couldn't be converted to d3d11 format",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (unknown_info)));
    return FALSE;
  }
}

static gboolean
gst_d3d11_base_convert_prefer_video_processor (GstD3D11BaseConvert * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (self);
  GstMemory *mem;
  GstD3D11Memory *dmem;

  if (!self->processor) {
    GST_TRACE_OBJECT (self, "Processor is unavailable");
    return FALSE;
  }

  if (gst_buffer_n_memory (inbuf) != 1 || gst_buffer_n_memory (outbuf) != 1) {
    GST_TRACE_OBJECT (self, "Num memory objects is mismatched, in: %d, out: %d",
        gst_buffer_n_memory (inbuf), gst_buffer_n_memory (outbuf));
    return FALSE;
  }

  mem = gst_buffer_peek_memory (inbuf, 0);
  g_assert (gst_is_d3d11_memory (mem));

  dmem = (GstD3D11Memory *) mem;
  if (dmem->device != filter->device) {
    GST_TRACE_OBJECT (self, "Input memory belongs to different device");
    return FALSE;
  }

  /* If we can use shader, and video processor was not used previously,
   * we prefer to use shader instead of video processor
   * because video processor implementation is vendor dependent
   * and not flexible */
  if (!self->processor_in_use &&
      gst_d3d11_memory_get_shader_resource_view_size (dmem)) {
    GST_TRACE_OBJECT (self, "SRV is available");
    return FALSE;
  }

  if (!gst_d3d11_video_processor_get_input_view (self->processor, dmem)) {
    GST_TRACE_OBJECT (self, "PIV is unavailable");
    return FALSE;
  }

  mem = gst_buffer_peek_memory (outbuf, 0);
  g_assert (gst_is_d3d11_memory (mem));

  dmem = (GstD3D11Memory *) mem;
  if (dmem->device != filter->device) {
    GST_TRACE_OBJECT (self, "Output memory belongs to different device");
    return FALSE;
  }

  if (!gst_d3d11_video_processor_get_output_view (self->processor, dmem)) {
    GST_TRACE_OBJECT (self, "POV is unavailable");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_base_convert_transform_using_processor (GstD3D11BaseConvert * self,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11Memory *in_mem, *out_mem;
  ID3D11VideoProcessorInputView *piv;
  ID3D11VideoProcessorOutputView *pov;

  in_mem = (GstD3D11Memory *) gst_buffer_peek_memory (inbuf, 0);
  out_mem = (GstD3D11Memory *) gst_buffer_peek_memory (outbuf, 0);

  piv = gst_d3d11_video_processor_get_input_view (self->processor, in_mem);
  if (!piv) {
    GST_ERROR_OBJECT (self, "ID3D11VideoProcessorInputView is unavailable");
    return FALSE;
  }

  pov = gst_d3d11_video_processor_get_output_view (self->processor, out_mem);
  if (!pov) {
    GST_ERROR_OBJECT (self, "ID3D11VideoProcessorOutputView is unavailable");
    return FALSE;
  }

  /* Clear background color with black */
  if (self->borders_w || self->borders_h) {
    GstD3D11BaseFilter *bfilter = GST_D3D11_BASE_FILTER_CAST (self);
    ID3D11DeviceContext *context_handle =
        gst_d3d11_device_get_device_context_handle (bfilter->device);
    ID3D11RenderTargetView *render_view[GST_VIDEO_MAX_PLANES] = { NULL, };

    if (!gst_d3d11_buffer_get_render_target_view (outbuf, render_view)) {
      GST_ERROR_OBJECT (self, "ID3D11RenderTargetView is unavailable");
      return FALSE;
    }

    gst_d3d11_device_lock (bfilter->device);
    clear_rtv_color_all (self, &bfilter->out_info, context_handle, render_view);
    gst_d3d11_device_unlock (bfilter->device);
  }

  return gst_d3d11_video_processor_render (self->processor,
      &self->in_rect, piv, &self->out_rect, pov);
}

static GstFlowReturn
gst_d3d11_base_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstD3D11BaseConvert *self = GST_D3D11_BASE_CONVERT (trans);
  GstD3D11Device *device = filter->device;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  ID3D11ShaderResourceView *resource_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *render_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView **target_rtv;
  guint i;
  gboolean copy_input = FALSE;
  gboolean copy_output = FALSE;
  GstMapInfo in_map[GST_VIDEO_MAX_PLANES];
  GstMapInfo out_map[GST_VIDEO_MAX_PLANES];

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  if (!gst_d3d11_buffer_map (inbuf, device_handle, in_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    goto invalid_memory;
  }

  if (!gst_d3d11_buffer_map (outbuf, device_handle, out_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    gst_d3d11_buffer_unmap (inbuf, in_map);
    goto invalid_memory;
  }

  if (gst_d3d11_base_convert_prefer_video_processor (self, inbuf, outbuf)) {
    gboolean ret =
        gst_d3d11_base_convert_transform_using_processor (self, inbuf, outbuf);

    if (!ret) {
      GST_ERROR_OBJECT (self, "Couldn't convert using video processor");
      goto conversion_failed;
    }

    self->processor_in_use = TRUE;

    GST_TRACE_OBJECT (self, "Conversion done by video processor");

    gst_d3d11_buffer_unmap (inbuf, in_map);
    gst_d3d11_buffer_unmap (outbuf, out_map);

    return GST_FLOW_OK;
  }

  /* Ensure shader resource views */
  if (!gst_d3d11_buffer_get_shader_resource_view (inbuf, resource_view)) {
    if (!create_shader_input_resource (self, device,
            self->in_d3d11_format, &filter->in_info)) {
      GST_ERROR_OBJECT (self, "Failed to configure fallback input texture");
      goto fallback_failed;
    }

    copy_input = TRUE;
    gst_d3d11_device_lock (device);
    for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
      GstD3D11Memory *mem =
          (GstD3D11Memory *) gst_buffer_peek_memory (inbuf, i);
      guint subidx;
      D3D11_BOX src_box = { 0, };
      D3D11_TEXTURE2D_DESC src_desc;
      D3D11_TEXTURE2D_DESC dst_desc;

      subidx = gst_d3d11_memory_get_subresource_index (mem);
      gst_d3d11_memory_get_texture_desc (mem, &src_desc);

      self->in_texture[i]->GetDesc (&dst_desc);

      src_box.left = 0;
      src_box.top = 0;
      src_box.front = 0;
      src_box.back = 1;
      src_box.right = MIN (src_desc.Width, dst_desc.Width);
      src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

      context_handle->CopySubresourceRegion (self->in_texture[i], 0, 0, 0, 0,
          (ID3D11Resource *) in_map[i].data, subidx, &src_box);
    }
    gst_d3d11_device_unlock (device);
  }

  /* Ensure render target views */
  if (!gst_d3d11_buffer_get_render_target_view (outbuf, render_view)) {
    if (!create_shader_output_resource (self, device,
            self->out_d3d11_format, &filter->out_info)) {
      GST_ERROR_OBJECT (self, "Failed to configure fallback output texture");
      goto fallback_failed;
    }

    copy_output = TRUE;
  }

  /* If we need border, clear render target view first */
  if (copy_output) {
    target_rtv = self->render_target_view;
  } else {
    target_rtv = render_view;
  }

  /* We need to clear background color as our shader wouldn't touch border
   * area. Likely output texture was initialized with zeros which is fine for
   * RGB, but it's not black color in case of YUV */
  if (self->borders_w || self->borders_h) {
    gst_d3d11_device_lock (device);
    clear_rtv_color_all (self, &filter->out_info, context_handle, target_rtv);
    gst_d3d11_device_unlock (device);
  }

  if (!gst_d3d11_converter_convert (self->converter,
          copy_input ? self->shader_resource_view : resource_view,
          target_rtv, NULL, NULL)) {
    goto conversion_failed;
  }

  if (copy_output) {
    gst_d3d11_device_lock (device);
    for (i = 0; i < gst_buffer_n_memory (outbuf); i++) {
      GstD3D11Memory *mem =
          (GstD3D11Memory *) gst_buffer_peek_memory (outbuf, i);
      guint subidx;
      D3D11_BOX src_box = { 0, };
      D3D11_TEXTURE2D_DESC src_desc;
      D3D11_TEXTURE2D_DESC dst_desc;

      self->out_texture[i]->GetDesc (&src_desc);
      subidx = gst_d3d11_memory_get_subresource_index (mem);
      gst_d3d11_memory_get_texture_desc (mem, &dst_desc);

      src_box.left = 0;
      src_box.top = 0;
      src_box.front = 0;
      src_box.back = 1;
      src_box.right = MIN (src_desc.Width, dst_desc.Width);
      src_box.bottom = MIN (src_desc.Height, dst_desc.Height);

      context_handle->CopySubresourceRegion ((ID3D11Resource *) out_map[i].data,
          subidx, 0, 0, 0, self->out_texture[i], 0, &src_box);
    }
    gst_d3d11_device_unlock (device);
  }

  gst_d3d11_buffer_unmap (inbuf, in_map);
  gst_d3d11_buffer_unmap (outbuf, out_map);

  return GST_FLOW_OK;

invalid_memory:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL), ("Invalid memory"));
    return GST_FLOW_ERROR;
  }
fallback_failed:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("Couldn't prepare fallback memory"));
    gst_d3d11_buffer_unmap (inbuf, in_map);
    gst_d3d11_buffer_unmap (outbuf, out_map);

    return GST_FLOW_ERROR;
  }
conversion_failed:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("Couldn't convert texture"));
    gst_d3d11_buffer_unmap (inbuf, in_map);
    gst_d3d11_buffer_unmap (outbuf, out_map);

    return GST_FLOW_ERROR;
  }
}

static void
gst_d3d11_base_convert_set_add_border (GstD3D11BaseConvert * self,
    gboolean add_border)
{
  gboolean prev = self->add_borders;

  self->add_borders = add_border;
  if (prev != self->add_borders)
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM_CAST (self));
}

/**
 * SECTION:element-d3d11convert
 * @title: d3d11convert
 * @short_description: A Direct3D11 based color conversion and video resizing element
 *
 * This element resizes video frames and change color space.
 * By default the element will try to negotiate to the same size on the source
 * and sinkpad so that no scaling is needed.
 * It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! video/x-raw,format=NV12 ! d3d11upload ! d3d11convert ! d3d11videosink
 * ```
 *  This will output a test video (generated in NV12 format) in a video
 * window. If the video sink selected does not support NV12
 * d3d11convert will automatically convert the video to a format understood
 * by the video sink.
 *
 * Since: 1.18
 *
 */

enum
{
  PROP_CONVERT_0,
  PROP_CONVERT_ADD_BORDERS,
};

struct _GstD3D11Convert
{
  GstD3D11BaseConvert parent;
};

G_DEFINE_TYPE (GstD3D11Convert, gst_d3d11_convert, GST_TYPE_D3D11_BASE_CONVERT);

static void gst_d3d11_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_d3d11_convert_class_init (GstD3D11ConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_d3d11_convert_set_property;
  gobject_class->get_property = gst_d3d11_convert_get_property;

  /**
   * GstD3D11Convert:add-borders:
   *
   * Add black borders if necessary to keep the display aspect ratio
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_CONVERT_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add black borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 colorspace converter and scaler",
      "Filter/Converter/Scaler/Video/Hardware",
      "Resizes video and allow color conversion using Direct3D11",
      "Seungha Yang <seungha.yang@navercorp.com>, "
      "Jeongki Kim <jeongki.kim@jeongki.kim>");
}

static void
gst_d3d11_convert_init (GstD3D11Convert * self)
{
}

static void
gst_d3d11_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11BaseConvert *base = GST_D3D11_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_ADD_BORDERS:
      gst_d3d11_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11BaseConvert *base = GST_D3D11_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_ADD_BORDERS:
      g_value_set_boolean (value, base->add_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * SECTION:element-d3d11colorconvert
 * @title: d3d11colorconvert
 *
 * A Direct3D11 based color conversion element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! video/x-raw,format=NV12 ! d3d11upload ! d3d11colorconvert ! d3d11download ! video/x-raw,format=RGBA ! fakesink
 * ```
 *  This will upload a test video (generated in NV12 format) to Direct3D11
 * memory space and convert it to RGBA format. Then a converted Direct3D11
 * frame will be downloaded to system memory space.
 *
 * Since: 1.20
 *
 */
struct _GstD3D11ColorConvert
{
  GstD3D11BaseConvert parent;
};

static GstCaps *gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_color_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstD3D11ColorConvert, gst_d3d11_color_convert,
    GST_TYPE_D3D11_BASE_CONVERT);

static void
gst_d3d11_color_convert_class_init (GstD3D11ColorConvertClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 colorspace converter",
      "Filter/Converter/Video/Hardware",
      "Color conversion using Direct3D11",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_fixate_caps);
}

static void
gst_d3d11_color_convert_init (GstD3D11ColorConvert * self)
{
}

static GstCaps *
gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d11_base_convert_caps_remove_format_info (caps);

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
gst_d3d11_color_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *format = NULL;

  GST_DEBUG_OBJECT (base,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  format = gst_d3d11_base_convert_get_fixed_format (base, direction, caps,
      othercaps);
  gst_caps_unref (othercaps);

  if (gst_caps_is_empty (format)) {
    GST_ERROR_OBJECT (base, "Could not convert formats");
  } else {
    GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, format);
  }

  return format;
}

/**
 * SECTION:element-d3d11scale
 * @title: d3d11scale
 *
 * A Direct3D11 based video resizing element
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=480 ! d3d11upload ! d3d11scale ! d3d11download ! video/x-raw,width=1280,height=720 ! fakesink
 * ```
 *  This will upload a 640x480 resolution test video to Direct3D11
 * memory space and resize it to 1280x720 resolution. Then a resized Direct3D11
 * frame will be downloaded to system memory space.
 *
 * Since: 1.20
 *
 */

enum
{
  PROP_SCALE_0,
  PROP_SCALE_ADD_BORDERS,
};

struct _GstD3D11Scale
{
  GstD3D11BaseConvert parent;
};

static void gst_d3d11_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d11_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_d3d11_scale_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

G_DEFINE_TYPE (GstD3D11Scale, gst_d3d11_scale, GST_TYPE_D3D11_BASE_CONVERT);

static void
gst_d3d11_scale_class_init (GstD3D11ScaleClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_d3d11_scale_set_property;
  gobject_class->get_property = gst_d3d11_scale_get_property;

  /**
   * GstD3D11Scale:add-borders:
   *
   * Add black borders if necessary to keep the display aspect ratio
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_SCALE_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add black borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 scaler",
      "Filter/Converter/Video/Scaler/Hardware",
      "Resizes video using Direct3D11",
      "Seungha Yang <seungha@centricular.com>");

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_scale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_d3d11_scale_fixate_caps);
}

static void
gst_d3d11_scale_init (GstD3D11Scale * self)
{
}

static void
gst_d3d11_scale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstD3D11BaseConvert *base = GST_D3D11_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_ADD_BORDERS:
      gst_d3d11_base_convert_set_add_border (base, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_scale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11BaseConvert *base = GST_D3D11_BASE_CONVERT (object);

  switch (prop_id) {
    case PROP_CONVERT_ADD_BORDERS:
      g_value_set_boolean (value, base->add_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_d3d11_scale_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d11_base_convert_caps_rangify_size_info (caps);

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
gst_d3d11_scale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GST_DEBUG_OBJECT (base,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  othercaps =
      gst_d3d11_base_convert_fixate_size (base, direction, caps, othercaps);

  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}
