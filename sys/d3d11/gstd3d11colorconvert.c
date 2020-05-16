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

/**
 * SECTION:element-d3d11convert
 * @title: d3d11colorconvert
 *
 * This element resizes video frames and change color space.
 * By default the element will try to negotiate to the same size on the source
 * and sinkpad so that no scaling is needed.
 * It is therefore safe to insert this element in a pipeline to
 * get more robust behaviour without any cost if no scaling is needed.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=NV12 ! d3d11upload ! d3d11convert ! d3d11videosink
 * ]|
 *  This will output a test video (generated in NV12 format) in a video
 * window. If the video sink selected does not support NV12
 * d3d11colorconvert will automatically convert the video to a format understood
 * by the video sink.
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstd3d11colorconvert.h"
#include "gstd3d11utils.h"
#include "gstd3d11memory.h"
#include "gstd3d11device.h"
#include "gstd3d11bufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_d3d11_color_convert_debug);
#define GST_CAT_DEFAULT gst_d3d11_color_convert_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, GST_D3D11_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
            GST_D3D11_FORMATS))
    );

#define gst_d3d11_color_convert_parent_class parent_class
G_DEFINE_TYPE (GstD3D11ColorConvert,
    gst_d3d11_color_convert, GST_TYPE_D3D11_BASE_FILTER);

static void gst_d3d11_color_convert_dispose (GObject * object);
static GstCaps *gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d11_color_convert_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query);
static gboolean
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

static GstFlowReturn gst_d3d11_color_convert_transform (GstBaseTransform *
    trans, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

/* copies the given caps */
static GstCaps *
gst_d3d11_color_convert_caps_remove_format_and_rangify_size_info (GstCaps *
    caps)
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
gst_d3d11_color_convert_class_init (GstD3D11ColorConvertClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstD3D11BaseFilterClass *bfilter_class = GST_D3D11_BASE_FILTER_CLASS (klass);

  gobject_class->dispose = gst_d3d11_color_convert_dispose;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "Direct3D11 colorspace converter and scaler",
      "Filter/Converter/Scaler/Video/Hardware",
      "Resizes video and allow color conversion using D3D11",
      "Seungha Yang <seungha.yang@navercorp.com>, "
      "Jeongki Kim <jeongki.kim@jeongki.kim>");

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_fixate_caps);
  trans_class->filter_meta =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_filter_meta);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_decide_allocation);
  trans_class->transform =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_transform);

  bfilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_d3d11_color_convert_set_info);

  GST_DEBUG_CATEGORY_INIT (gst_d3d11_color_convert_debug,
      "d3d11convert", 0, "d3d11convert element");
}

static void
gst_d3d11_color_convert_init (GstD3D11ColorConvert * self)
{
}

static void
gst_d3d11_color_convert_clear_shader_resource (GstD3D11ColorConvert * self)
{
  gint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->shader_resource_view[i]) {
      ID3D11ShaderResourceView_Release (self->shader_resource_view[i]);
      self->shader_resource_view[i] = NULL;
    }

    if (self->render_target_view[i]) {
      ID3D11RenderTargetView_Release (self->render_target_view[i]);
      self->render_target_view[i] = NULL;
    }
  }

  self->num_input_view = 0;
  self->num_output_view = 0;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (self->in_texture[i]) {
      ID3D11Texture2D_Release (self->in_texture[i]);
      self->in_texture[i] = NULL;
    }

    if (self->out_texture[i]) {
      ID3D11Texture2D_Release (self->out_texture[i]);
      self->out_texture[i] = NULL;
    }
  }

  if (self->converter)
    gst_d3d11_color_converter_free (self->converter);
  self->converter = NULL;
}

static void
gst_d3d11_color_convert_dispose (GObject * object)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (object);

  gst_d3d11_color_convert_clear_shader_resource (self);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_d3d11_color_convert_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *tmp2;
  GstCaps *result;

  /* Get all possible caps that we can transform to */
  tmp = gst_d3d11_color_convert_caps_remove_format_and_rangify_size_info (caps);

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
  GstVideoFormatFlags in_flags, t_flags;
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
gst_d3d11_color_convert_fixate_format (GstBaseTransform * trans,
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
    gst_structure_remove_fields (tests, "height", "width", "pixel-aspect-ratio",
        "display-aspect-ratio", NULL);
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

static GstCaps *
gst_d3d11_color_convert_get_fixed_format (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = gst_caps_copy (othercaps);
  }

  gst_d3d11_color_convert_fixate_format (trans, caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    }
  }

  return result;
}

static GstCaps *
gst_d3d11_color_convert_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
  0,};

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
gst_d3d11_color_convert_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *format;

  GST_DEBUG_OBJECT (base,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  format = gst_d3d11_color_convert_get_fixed_format (base, direction, caps,
      othercaps);

  if (gst_caps_is_empty (format)) {
    GST_ERROR_OBJECT (base, "Could not convert formats");
    return format;
  }

  othercaps =
      gst_d3d11_color_convert_fixate_size (base, direction, caps, othercaps);
  if (gst_caps_get_size (othercaps) == 1) {
    gint i;
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
gst_d3d11_color_convert_filter_meta (GstBaseTransform * trans,
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
gst_d3d11_color_convert_propose_allocation (GstBaseTransform * trans,
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

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query))
    return FALSE;

  /* passthrough, we're done */
  if (decide_query == NULL)
    return TRUE;

  gst_query_parse_allocation (query, &caps, NULL);

  if (caps == NULL)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, NULL, NULL, NULL);
    if (!GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (filter->device, &info, 0,
        D3D11_BIND_SHADER_RESOURCE);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |= D3D11_BIND_SHADER_RESOURCE;
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

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;
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
gst_d3d11_color_convert_decide_allocation (GstBaseTransform * trans,
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
  gint i;

  gst_query_parse_allocation (query, &outcaps, NULL);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps))
    return FALSE;

  size = GST_VIDEO_INFO_SIZE (&info);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool && !GST_IS_D3D11_BUFFER_POOL (pool)) {
      gst_object_unref (pool);
      pool = NULL;
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d11_buffer_pool_new (filter->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  d3d11_params = gst_buffer_pool_config_get_d3d11_allocation_params (config);
  if (!d3d11_params) {
    d3d11_params = gst_d3d11_allocation_params_new (filter->device, &info, 0,
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
  } else {
    /* Set bind flag */
    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&info); i++) {
      d3d11_params->desc[i].BindFlags |=
          (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
    }
  }

  gst_buffer_pool_config_set_d3d11_allocation_params (config, d3d11_params);
  gst_d3d11_allocation_params_free (d3d11_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  size = GST_D3D11_BUFFER_POOL (pool)->buffer_size;

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static gboolean
create_shader_input_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = { 0 };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11ShaderResourceView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_input_view)
    return TRUE;

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

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
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

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (is_semiplanar) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipLevels = 1;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateShaderResourceView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);

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
    if (view[i])
      ID3D11ShaderResourceView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

static gboolean
create_shader_output_resource (GstD3D11ColorConvert * self,
    GstD3D11Device * device, const GstD3D11Format * format, GstVideoInfo * info)
{
  D3D11_TEXTURE2D_DESC texture_desc = { 0, };
  D3D11_RENDER_TARGET_VIEW_DESC view_desc = { 0, };
  HRESULT hr;
  ID3D11Device *device_handle;
  ID3D11Texture2D *tex[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i;

  if (self->num_output_view)
    return TRUE;

  device_handle = gst_d3d11_device_get_device_handle (device);

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

      hr = ID3D11Device_CreateTexture2D (device_handle,
          &texture_desc, NULL, &tex[i]);
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

    hr = ID3D11Device_CreateTexture2D (device_handle,
        &texture_desc, NULL, &tex[0]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Failed to create texture (0x%x)", (guint) hr);
      goto error;
    }

    if (is_semiplanar) {
      ID3D11Resource_AddRef (tex[0]);
      tex[1] = tex[0];
    }
  }

  view_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  view_desc.Texture2D.MipSlice = 0;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (format->resource_format[i] == DXGI_FORMAT_UNKNOWN)
      break;

    view_desc.Format = format->resource_format[i];
    hr = ID3D11Device_CreateRenderTargetView (device_handle,
        (ID3D11Resource *) tex[i], &view_desc, &view[i]);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self,
          "Failed to create %dth render target view (0x%x)", i, (guint) hr);
      goto error;
    }
  }

  self->num_output_view = i;

  GST_DEBUG_OBJECT (self, "%d render view created", self->num_output_view);

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    self->out_texture[i] = tex[i];
    self->render_target_view[i] = view[i];
  }

  return TRUE;

error:
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (view[i])
      ID3D11RenderTargetView_Release (view[i]);
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (tex[i])
      ID3D11Texture2D_Release (tex[i]);
  }

  return FALSE;
}

static gboolean
gst_d3d11_color_convert_set_info (GstD3D11BaseFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (filter);
  const GstVideoInfo *unknown_info;

  if (gst_base_transform_is_passthrough (GST_BASE_TRANSFORM (filter)))
    return TRUE;

  gst_d3d11_color_convert_clear_shader_resource (self);

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (in_info)),
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode)
    goto format_mismatch;

  /* FIXME: add support border */
  if (in_info->width == out_info->width && in_info->height == out_info->height
      && in_info->finfo == out_info->finfo) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
    return TRUE;
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

  self->converter = gst_d3d11_color_converter_new (filter->device,
      in_info, out_info);

  if (!self->converter) {
    GST_ERROR_OBJECT (self, "couldn't set converter");
    return FALSE;
  }

  /* setup D3D11_BOX struct for fallback copy */
  self->in_src_box.left = 0;
  self->in_src_box.top = 0;
  self->in_src_box.front = 0;
  self->in_src_box.back = 1;
  self->in_src_box.right = GST_VIDEO_INFO_WIDTH (in_info);
  self->in_src_box.bottom = GST_VIDEO_INFO_HEIGHT (in_info);

  self->out_src_box.left = 0;
  self->out_src_box.top = 0;
  self->out_src_box.front = 0;
  self->out_src_box.back = 1;
  self->out_src_box.right = GST_VIDEO_INFO_WIDTH (out_info);
  self->out_src_box.bottom = GST_VIDEO_INFO_HEIGHT (out_info);

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

static GstFlowReturn
gst_d3d11_color_convert_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstD3D11BaseFilter *filter = GST_D3D11_BASE_FILTER (trans);
  GstD3D11ColorConvert *self = GST_D3D11_COLOR_CONVERT (trans);
  ID3D11DeviceContext *context_handle;
  ID3D11ShaderResourceView *resource_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  ID3D11RenderTargetView *render_view[GST_VIDEO_MAX_PLANES] = { NULL, };
  gint i, j, view_index;
  gboolean copy_input = FALSE;
  gboolean copy_output = FALSE;
  GstD3D11Device *device = filter->device;

  context_handle = gst_d3d11_device_get_device_context_handle (device);

  view_index = 0;
  for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (inbuf, i);
    GstD3D11Memory *d3d11_mem;
    GstMapInfo info;

    g_assert (gst_is_d3d11_memory (mem));

    d3d11_mem = (GstD3D11Memory *) mem;
    /* map to transfer pending staging data if any */
    if (d3d11_mem->desc.Usage == D3D11_USAGE_DEFAULT) {
      gst_memory_map (mem, &info, GST_MAP_READ | GST_MAP_D3D11);
      gst_memory_unmap (mem, &info);
    }

    if (gst_d3d11_memory_ensure_shader_resource_view (d3d11_mem)) {
      GST_TRACE_OBJECT (self, "Use input texture resource without copy");

      for (j = 0; j < d3d11_mem->num_shader_resource_views; j++) {
        resource_view[view_index] = d3d11_mem->shader_resource_view[j];
        view_index++;
      }
    } else {
      GST_TRACE_OBJECT (self, "Render using fallback input texture");
      copy_input = TRUE;

      if (!create_shader_input_resource (self, device,
              self->in_d3d11_format, &filter->in_info)) {
        GST_ERROR_OBJECT (self, "Failed to configure fallback input texture");
        return GST_FLOW_ERROR;
      }
      break;
    }
  }

  /* if input memory has no resource view,
   * copy texture into our fallback texture */
  if (copy_input) {
    gst_d3d11_device_lock (device);
    for (i = 0; i < gst_buffer_n_memory (inbuf); i++) {
      GstMemory *mem = gst_buffer_peek_memory (inbuf, i);
      GstD3D11Memory *d3d11_mem;

      g_assert (gst_is_d3d11_memory (mem));

      d3d11_mem = (GstD3D11Memory *) mem;

      ID3D11DeviceContext_CopySubresourceRegion (context_handle,
          (ID3D11Resource *) self->in_texture[i], 0, 0, 0, 0,
          (ID3D11Resource *) d3d11_mem->texture, d3d11_mem->subresource_index,
          &self->in_src_box);
    }
    gst_d3d11_device_unlock (device);
  }

  view_index = 0;
  for (i = 0; i < gst_buffer_n_memory (outbuf); i++) {
    GstMemory *mem = gst_buffer_peek_memory (outbuf, i);
    GstD3D11Memory *d3d11_mem;

    g_assert (gst_is_d3d11_memory (mem));

    d3d11_mem = (GstD3D11Memory *) mem;

    if (gst_d3d11_memory_ensure_render_target_view (d3d11_mem)) {
      GST_TRACE_OBJECT (self, "Render to output texture directly");

      for (j = 0; j < d3d11_mem->num_render_target_views; j++) {
        render_view[view_index] = d3d11_mem->render_target_view[j];
        view_index++;
      }
    } else {
      GST_TRACE_OBJECT (self, "Render to fallback output texture");
      copy_output = TRUE;

      if (!create_shader_output_resource (self, device, self->out_d3d11_format,
              &filter->out_info)) {
        GST_ERROR_OBJECT (self, "Failed to configure fallback output texture");
        return GST_FLOW_ERROR;
      }
      break;
    }
  }

  if (!gst_d3d11_color_converter_convert (self->converter,
          copy_input ? self->shader_resource_view : resource_view,
          copy_output ? self->render_target_view : render_view)) {
    GST_ERROR_OBJECT (self, "Failed to convert");

    return GST_FLOW_ERROR;
  }

  if (copy_output) {
    gst_d3d11_device_lock (device);
    for (i = 0; i < gst_buffer_n_memory (outbuf); i++) {
      GstMemory *mem = gst_buffer_peek_memory (outbuf, i);
      GstD3D11Memory *d3d11_mem;

      g_assert (gst_is_d3d11_memory (mem));

      d3d11_mem = (GstD3D11Memory *) mem;

      ID3D11DeviceContext_CopySubresourceRegion (context_handle,
          (ID3D11Resource *) d3d11_mem->texture, d3d11_mem->subresource_index,
          0, 0, 0, (ID3D11Resource *) self->out_texture[i], 0,
          &self->out_src_box);
    }
    gst_d3d11_device_unlock (device);
  } else {
    for (i = 0; i < gst_buffer_n_memory (outbuf); i++) {
      GstMemory *mem = gst_buffer_peek_memory (outbuf, i);
      GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
    }
  }

  return GST_FLOW_OK;
}
