/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2005-2012 David Schleef <ds@schleef.org>
 * Copyright (C) 2022 Thibault Saunier <tsaunier@igalia.com>
 * Copyright (C) 2026 Netflix Inc.
 *
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

#include <gst/allocators/allocators.h>

#include "video-convertscale-helper.h"
#include "ext/drm_fourcc.h"

#define GST_CAT_DEFAULT video_convertscale_helper_debug
GST_DEBUG_CATEGORY_STATIC (video_convertscale_helper_debug);

static void
ensure_debug_category (void)
{
#ifndef GST_DISABLE_GST_DEBUG
  static GstDebugCategory *cat = NULL;
  if (g_once_init_enter (&cat)) {
    GST_DEBUG_CATEGORY_INIT (video_convertscale_helper_debug,
        "video-convertscale-helper", 0,
        "video-convertscale-helper helper functions");
    g_once_init_leave (&cat, video_convertscale_helper_debug);
  }
#endif /* GST_DISABLE_GST_DEBUG */
}

#define SWAP_INT(a, b) G_STMT_START { \
  gint __tmp = a; \
  a = b; \
  b = __tmp; \
} G_STMT_END

/*
 * This is an incomplete matrix of in formats and a score for the preferred output
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
 * PAL or GRAY are never preferred, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is preferred and if any, preferably horizontal
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
static gint
score_value (GstObject * object, const GstVideoFormatInfo * in_info,
    GstVideoFormat format, const GstCapsFeatures * features,
    const GstStructure * ins, GstStructure * outs,
    GstVideoConvertScaleFormatLossFunc format_loss)
{
  const GstVideoFormatInfo *t_info;
  GstVideoFormatFlags in_flags, t_flags;
  gint loss = 0;

  t_info = gst_video_format_get_info (format);
  if (!t_info || t_info->format == GST_VIDEO_FORMAT_UNKNOWN)
    return G_MAXINT;

  if (format_loss) {
    loss = format_loss (object, in_info, t_info, features, ins, outs);
    if (loss == G_MAXINT)
      return G_MAXINT;
  }

  /* accept input format immediately without loss */
  if (in_info == t_info)
    return loss;

  loss += SCORE_FORMAT_CHANGE;

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
      loss += SCORE_DEPTH_LOSS;
  }

  GST_DEBUG_OBJECT (object, "score %s -> %s = %d",
      GST_VIDEO_FORMAT_INFO_NAME (in_info),
      GST_VIDEO_FORMAT_INFO_NAME (t_info), loss);

  return loss;
}

static GstCaps *
update_result_with_score (GstCaps * result, gint loss, gint * min_loss,
    GstVideoFormat fmt, GstStructure * outs, const GstCapsFeatures * features,
    guint64 modifier)
{
  if (loss == G_MAXINT || loss > *min_loss) {
    gst_structure_free (outs);
    return result;
  }

  if (loss < *min_loss) {
    *min_loss = loss;
    gst_clear_caps (&result);
  }

  if (!result)
    result = gst_caps_new_empty ();

  const GstVideoFormatInfo *t_info = gst_video_format_get_info (fmt);

  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
    gchar *drm_fmt_name;

    g_assert (modifier != DRM_FORMAT_MOD_INVALID);

    drm_fmt_name =
        gst_video_dma_drm_fourcc_to_string (gst_video_dma_drm_fourcc_from_format
        (t_info->format), modifier);
    gst_structure_set (outs, "drm-format", G_TYPE_STRING, drm_fmt_name, NULL);
    g_free (drm_fmt_name);
  } else {
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (t_info), NULL);
  }

  gst_caps_append_structure_full (result, outs,
      gst_caps_features_copy (features));

  return result;
}

static GstCaps *
_fixate_format (GstObject * object,
    GstVideoConvertScaleFormatLossFunc format_loss, GstCaps * caps,
    GstCaps * othercaps)
{
  GstStructure *ins;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info;
  GstCapsFeatures *features;
  GstVideoFormat fmt;
  gint min_loss = G_MAXINT;
  guint i, capslen;
  GstCaps *result = NULL;

  ins = gst_caps_get_structure (caps, 0);

  if (gst_video_is_dma_drm_caps (caps)) {
    guint32 fourcc;
    GstVideoFormat video_format;

    in_format = gst_structure_get_string (ins, "drm-format");
    if (!in_format)
      return NULL;

    fourcc = gst_video_dma_drm_fourcc_from_string (in_format, NULL);
    video_format = gst_video_dma_drm_fourcc_to_format (fourcc);
    if (video_format == GST_VIDEO_FORMAT_UNKNOWN)
      return NULL;

    in_info = gst_video_format_get_info (video_format);
  } else {
    in_format = gst_structure_get_string (ins, "format");
    if (!in_format)
      return NULL;

    in_info =
        gst_video_format_get_info (gst_video_format_from_string (in_format));
  }

  if (!in_info)
    return NULL;

  GST_DEBUG_OBJECT (object, "source format %s", in_format);

  capslen = gst_caps_get_size (othercaps);
  GST_DEBUG_OBJECT (object, "iterate %d structures", capslen);
  for (i = 0; i < capslen; i++) {
    gboolean is_dma;
    GstStructure *outs;
    const GValue *format;
    guint64 modifier;
    guint32 fourcc;

    features = gst_caps_get_features (othercaps, i);
    outs = gst_caps_get_structure (othercaps, i);

    if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_DMABUF)) {
      format = gst_structure_get_value (outs, "drm-format");
      is_dma = TRUE;
    } else {
      format = gst_structure_get_value (outs, "format");
      is_dma = FALSE;
    }
    /* should not happen */
    if (format == NULL)
      continue;

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      GST_DEBUG_OBJECT (object, "have %d formats", len);
      for (j = 0; j < len; j++) {
        const GValue *val;

        modifier = DRM_FORMAT_MOD_INVALID;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          if (is_dma) {
            fourcc =
                gst_video_dma_drm_fourcc_from_string (g_value_get_string (val),
                &modifier);
            fmt = gst_video_dma_drm_fourcc_to_format (fourcc);
          } else {
            fmt = gst_video_format_from_string (g_value_get_string (val));
          }
          if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
            continue;

          GstStructure *outs_copy = gst_structure_copy (outs);
          gint loss =
              score_value (object, in_info, fmt, features, ins, outs_copy,
              format_loss);
          result =
              update_result_with_score (result, loss, &min_loss, fmt, outs_copy,
              features, modifier);
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      modifier = DRM_FORMAT_MOD_INVALID;

      if (is_dma) {
        fourcc =
            gst_video_dma_drm_fourcc_from_string (g_value_get_string (format),
            &modifier);
        fmt = gst_video_dma_drm_fourcc_to_format (fourcc);
      } else {
        fmt = gst_video_format_from_string (g_value_get_string (format));
      }
      if (fmt == GST_VIDEO_FORMAT_UNKNOWN)
        continue;

      GstStructure *outs_copy = gst_structure_copy (outs);
      gint loss = score_value (object, in_info, fmt, features, ins, outs_copy,
          format_loss);
      result =
          update_result_with_score (result, loss, &min_loss, fmt, outs_copy,
          features, modifier);
    }
  }

  return result;
}

static gboolean
_fixate_size (GstObject * object,
    GstVideoOrientationMethod orientation, GstPadDirection direction,
    GstCaps * caps, GstStructure * outs)
{
  GstStructure *ins;
  const GValue *from_par, *to_par;
  GValue fpar = {
    0,
  };
  GValue tpar = {
    0,
  };
  gboolean ret = TRUE;

  ins = gst_caps_get_structure (caps, 0);

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
    g_return_val_if_fail (gst_value_is_fixed (from_par), FALSE);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* if video-orientation changes */
    switch (orientation) {
      case GST_VIDEO_ORIENTATION_90R:
      case GST_VIDEO_ORIENTATION_90L:
      case GST_VIDEO_ORIENTATION_UL_LR:
      case GST_VIDEO_ORIENTATION_UR_LL:
        SWAP_INT (from_w, from_h);
        SWAP_INT (from_par_n, from_par_d);
        break;
      default:
        break;
    }

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (object, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (object, "fixating to_par to %dx%d", n, d);
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
      GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      ret = FALSE;
      goto done;
    }

    GST_DEBUG_OBJECT (object, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (object, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (object, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        ret = FALSE;
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

      GST_DEBUG_OBJECT (object, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (object, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
              ("Error calculating the output scaled size - integer overflow"));
          ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scale sized - integer overflow"));
        ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        ret = FALSE;
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
        GST_ELEMENT_ERROR (object, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        ret = FALSE;
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
  return ret;
}

static gboolean
subsampling_unchanged (GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gint i;
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

static gboolean
video_info_from_caps (GstVideoInfo * info, GstCaps * caps)
{
  GstVideoInfoDmaDrm drm_info;

  if (!gst_video_is_dma_drm_caps (caps))
    return gst_video_info_from_caps (info, caps);

  if (!gst_video_info_dma_drm_from_caps (&drm_info, caps))
    return FALSE;

  /* If we don't have a native gst format, try to find one with the same fourcc.
   * This makes _IS_YUV(), etc work, and fill default colorimetry. */
  if (GST_VIDEO_INFO_FORMAT (&drm_info.vinfo) == GST_VIDEO_FORMAT_DMA_DRM) {
    GstVideoFormat format =
        gst_video_dma_drm_fourcc_to_format (drm_info.drm_fourcc);
    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
      return gst_video_info_set_format (info, format,
          GST_VIDEO_INFO_WIDTH (&drm_info.vinfo),
          GST_VIDEO_INFO_HEIGHT (&drm_info.vinfo));
    }
  }

  if (!gst_video_info_dma_drm_to_video_info (&drm_info, info))
    return FALSE;

  return TRUE;
}

static void
transfer_colorimetry_from_input (GstObject * object, GstCaps * in_caps,
    GstCaps * out_caps, gboolean default_colorimetry)
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

    if (!video_info_from_caps (&in_info, in_caps)) {
      GST_WARNING_OBJECT (object,
          "Failed to convert sink pad caps to video info");
      return;
    }

    if (!video_info_from_caps (&out_info, out_caps)) {
      GST_WARNING_OBJECT (object,
          "Failed to convert src pad caps to video info");
      return;
    }

    if (!have_colorimetry && default_colorimetry) {
      /* Set default colorimetry instead of transfering from input */
      gchar *colorimetry_str =
          gst_video_colorimetry_to_string (&out_info.colorimetry);
      if (colorimetry_str)
        gst_caps_set_simple (out_caps, "colorimetry", G_TYPE_STRING,
            colorimetry_str, NULL);
      g_free (colorimetry_str);
    } else if (!have_colorimetry && in_colorimetry != NULL) {
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
        if (colorimetry_str)
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

static void
copy_misc_fields_from_input (GstCaps * in_caps, GstCaps * out_caps)
{
  const gchar *fields[] = { "interlace-mode", "field-order", "multiview-mode",
    "multiview-flags", "framerate"
  };
  GstStructure *out_caps_s = gst_caps_get_structure (out_caps, 0);
  GstStructure *in_caps_s = gst_caps_get_structure (in_caps, 0);
  int i;

  for (i = 0; i < G_N_ELEMENTS (fields); i++) {
    const GValue *in_field = gst_structure_get_value (in_caps_s, fields[i]);
    const GValue *out_field = gst_structure_get_value (out_caps_s, fields[i]);

    if (out_field && gst_value_is_fixed (out_field))
      continue;

    if (in_field)
      gst_structure_set_value (out_caps_s, fields[i], in_field);
  }
}

/**
 * gst_video_convertscale_fixate_caps:
 * @object: The object to use for logging
 * @direction: The #GstPadDirection of the pad
 * @orientation: The #GstVideoOrientationMethod
 * @format_loss: (nullable): A callback to estimate the conversion loss, or %NULL
 * @flags: #GstVideoConvertScaleFixateFlags
 * @caps: The input #GstCaps
 * @othercaps: The output #GstCaps
 *
 * Fixates the output video format and resolution given the input caps and the pad direction.
 * This uses heuristics to minimize the loss when converting the video format and/or resolution.
 *
 * When fixating the format, also transfer colorimetry and chroma-site from the input if possible.
 *
 * @format_loss callback is used to estimate the conversion loss between input and output formats.
 * If %NULL, all format conversions are considered supported.
 *
 * Returns: (transfer full)(nullable): The fixated output caps or %NULL if fixating failed.
 * Since: 1.30
 */
GstCaps *
gst_video_convertscale_fixate_caps (GstObject * object,
    GstPadDirection direction, GstVideoOrientationMethod orientation,
    GstVideoConvertScaleFormatLossFunc format_loss,
    GstVideoConvertScaleFixateFlags flags, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  ensure_debug_category ();

  GST_DEBUG_OBJECT (object,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  if (flags & GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT) {
    /* will iterate in all structures to find one with "best color" */
    result = _fixate_format (object, format_loss, caps, othercaps);
    if (!result)
      return NULL;
  } else {
    result = gst_caps_copy (othercaps);
  }

  if (flags & GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_SIZE) {
    /* Search closest resolution from input */
    gint w, h;
    GstStructure *s = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (s, "width", &w);
    gst_structure_get_int (s, "height", &h);
    gint from_pixels = w * h;
    gint best_i = -1;
    gint best_pixels_diff = G_MAXINT;
    gint capssize = gst_caps_get_size (result);
    for (gint i = 0; i < capssize; i++) {
      s = gst_caps_get_structure (result, i);
      if (!_fixate_size (object, orientation, direction, caps, s))
        continue;
      gst_structure_get_int (s, "width", &w);
      gst_structure_get_int (s, "height", &h);
      gint pixels = w * h;
      gint pixels_diff = ABS (pixels - from_pixels);
      if (pixels_diff < best_pixels_diff) {
        best_pixels_diff = pixels_diff;
        best_i = i;
      }
    }

    if (best_i < 0) {
      gst_clear_caps (&result);
      return NULL;
    }

    GstCaps *tmp = result;
    result = gst_caps_new_empty ();
    GstCapsFeatures *features = gst_caps_get_features (tmp, best_i);
    s = gst_caps_get_structure (tmp, best_i);
    gst_caps_append_structure_full (result, gst_structure_copy (s),
        gst_caps_features_copy (features));
    gst_clear_caps (&tmp);
  }

  /* some fields might be lost while feature caps conversion */
  copy_misc_fields_from_input (caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if ((flags & GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_FORMAT)
      && direction == GST_PAD_SINK) {
    /* transfer colorimetry and chroma-site from input if possible */
    transfer_colorimetry_from_input (object, caps, result, flags &
        GST_VIDEO_CONVERTSCALE_FIXATE_FLAG_DEFAULT_COLORIMETRY);
  }

  return result;
}
