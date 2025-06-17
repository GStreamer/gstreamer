/* GStreamer
 * Copyright (C) 2022 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
 *     Author: Liu Yinhang <yinhang.liu@intel.com>
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
 * SECTION:video-info-dma-drm
 * @title: GstVideoInfoDmaDrm
 * @short_description: Structures and enumerations to describe DMA video
 *  format in DRM mode.
 */


#include "video-format.h"
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "video-info-dma.h"
#include "ext/drm_fourcc.h"

#include <gst/allocators/gstdmabuf.h>

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new ("video-info-dma-drm", 0,
        "video-info-dma-drm structure");

    g_once_init_leave (&cat_gonce, cat_done);
  }

  return (GstDebugCategory *) cat_gonce;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

static GstVideoInfoDmaDrm *
gst_video_info_dma_drm_copy (const GstVideoInfoDmaDrm * drm_info)
{
  return g_memdup2 (drm_info, sizeof (GstVideoInfoDmaDrm));
}

/**
 * gst_video_info_dma_drm_free:
 * @drm_info: a #GstVideoInfoDmaDrm
 *
 * Free a #GstVideoInfoDmaDrm structure previously allocated with
 * gst_video_info_dma_drm_new()
 *
 * Since: 1.24
 */
void
gst_video_info_dma_drm_free (GstVideoInfoDmaDrm * drm_info)
{
  g_free (drm_info);
}

G_DEFINE_BOXED_TYPE (GstVideoInfoDmaDrm, gst_video_info_dma_drm,
    (GBoxedCopyFunc) gst_video_info_dma_drm_copy,
    (GBoxedFreeFunc) gst_video_info_dma_drm_free);

/**
 * gst_video_info_dma_drm_init:
 * @drm_info: (out caller-allocates): a #GstVideoInfoDmaDrm
 *
 * Initialize @drm_info with default values.
 *
 * Since: 1.24
 */
void
gst_video_info_dma_drm_init (GstVideoInfoDmaDrm * drm_info)
{
  g_return_if_fail (drm_info != NULL);

  gst_video_info_init (&drm_info->vinfo);

  drm_info->drm_fourcc = DRM_FORMAT_INVALID;
  drm_info->drm_modifier = DRM_FORMAT_MOD_INVALID;
}

/**
 * gst_video_info_dma_drm_new:
 *
 * Allocate a new #GstVideoInfoDmaDrm that is also initialized with
 * gst_video_info_dma_drm_init().
 *
 * Returns: (transfer full): a new #GstVideoInfoDmaDrm.
 * Free it with gst_video_info_dma_drm_free().
 *
 * Since: 1.24
 */
GstVideoInfoDmaDrm *
gst_video_info_dma_drm_new (void)
{
  GstVideoInfoDmaDrm *info;

  info = g_new (GstVideoInfoDmaDrm, 1);
  gst_video_info_dma_drm_init (info);

  return info;
}

/**
 * gst_video_is_dma_drm_caps:
 * @caps: a #GstCaps
 *
 * Check whether the @caps is a dma drm kind caps. Please note that
 * the caps should be fixed.
 *
 * Returns: %TRUE if the caps is a dma drm caps.
 *
 * Since: 1.24
 */
gboolean
gst_video_is_dma_drm_caps (const GstCaps * caps)
{
  GstStructure *structure;

  g_return_val_if_fail (caps != NULL, FALSE);

  if (!gst_caps_is_fixed (caps))
    return FALSE;

  if (!gst_caps_features_contains (gst_caps_get_features (caps, 0),
          GST_CAPS_FEATURE_MEMORY_DMABUF))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (g_strcmp0 (gst_structure_get_string (structure, "format"),
          "DMA_DRM") != 0)
    return FALSE;

  return TRUE;
}

/**
 * gst_video_info_dma_drm_to_caps:
 * @drm_info: a #GstVideoInfoDmaDrm
 *
 * Convert the values of @drm_info into a #GstCaps. Please note that the
 * @caps returned will be a dma drm caps which sets format field to DMA_DRM,
 * and contains a new drm-format field. The value of drm-format field is
 * composed of a drm fourcc and a modifier, such as NV12:0x0100000000000002.
 *
 * Returns: (transfer full) (nullable): a new #GstCaps containing the
 * info in @drm_info.
 *
 * Since: 1.24
 */
GstCaps *
gst_video_info_dma_drm_to_caps (const GstVideoInfoDmaDrm * drm_info)
{
  GstCaps *caps;
  GstStructure *structure;
  gchar *str;

  g_return_val_if_fail (drm_info != NULL, NULL);
  g_return_val_if_fail (drm_info->drm_fourcc != DRM_FORMAT_INVALID, NULL);
  g_return_val_if_fail (drm_info->drm_modifier != DRM_FORMAT_MOD_INVALID, NULL);

  caps = gst_video_info_to_caps (&drm_info->vinfo);
  if (!caps) {
    GST_DEBUG ("Failed to create caps from video info");
    return NULL;
  }

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_single_static_str (GST_CAPS_FEATURE_MEMORY_DMABUF));

  str = gst_video_dma_drm_fourcc_to_string (drm_info->drm_fourcc,
      drm_info->drm_modifier);

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_set (structure, "format", G_TYPE_STRING, "DMA_DRM",
      "drm-format", G_TYPE_STRING, str, NULL);

  g_free (str);

  return caps;
}

/**
 * gst_video_info_dma_drm_from_caps:
 * @drm_info: (out caller-allocates): #GstVideoInfoDmaDrm
 * @caps: a #GstCaps
 *
 * Parse @caps and update @info. Please note that the @caps should be
 * a dma drm caps. The gst_video_is_dma_drm_caps() can be used to verify
 * it before calling this function.
 *
 * Returns: TRUE if @caps could be parsed
 *
 * Since: 1.24
 */
gboolean
gst_video_info_dma_drm_from_caps (GstVideoInfoDmaDrm * drm_info,
    const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *str;
  guint32 fourcc;
  guint64 modifier;
  GstVideoFormat format;
  GstCaps *tmp_caps = NULL;
  gboolean ret;

  g_return_val_if_fail (drm_info != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  if (!gst_video_is_dma_drm_caps (caps))
    return FALSE;

  GST_DEBUG ("parsing caps %" GST_PTR_FORMAT, caps);

  tmp_caps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (tmp_caps, 0);

  str = gst_structure_get_string (structure, "drm-format");
  if (!str) {
    GST_DEBUG ("drm caps %" GST_PTR_FORMAT "has no drm-format field", caps);
    ret = FALSE;
    goto out;
  }

  fourcc = gst_video_dma_drm_fourcc_from_string (str, &modifier);
  if (fourcc == DRM_FORMAT_INVALID) {
    GST_DEBUG ("Can not parse fourcc in caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
    goto out;
  }
  if (modifier == DRM_FORMAT_MOD_INVALID) {
    GST_DEBUG ("Can not parse modifier in caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
    goto out;
  }

  /* If the modifier is linear, set the according format in video info,
   * otherwise, just let the format to be GST_VIDEO_FORMAT_DMA_DRM. */
  format = gst_video_dma_drm_format_to_gst_format (fourcc, modifier);
  if (format != GST_VIDEO_FORMAT_UNKNOWN) {
    gst_structure_set (structure, "format", G_TYPE_STRING,
        gst_video_format_to_string (format), NULL);
  }

  gst_structure_remove_field (structure, "drm-format");

  if (!gst_video_info_from_caps (&drm_info->vinfo, tmp_caps)) {
    GST_DEBUG ("Can not parse video info for caps %" GST_PTR_FORMAT, tmp_caps);
    ret = FALSE;
    goto out;
  }

  drm_info->drm_fourcc = fourcc;
  drm_info->drm_modifier = modifier;
  ret = TRUE;

out:
  gst_clear_caps (&tmp_caps);
  return ret;
}

/**
 * gst_video_info_dma_drm_new_from_caps:
 * @caps: a #GstCaps
 *
 * Parse @caps to generate a #GstVideoInfoDmaDrm. Please note that the
 * @caps should be a dma drm caps. The gst_video_is_dma_drm_caps() can
 * be used to verify it before calling this function.
 *
 * Returns: (transfer full) (nullable): A #GstVideoInfoDmaDrm,
 *   or %NULL if @caps couldn't be parsed.
 *
 * Since: 1.24
 */
GstVideoInfoDmaDrm *
gst_video_info_dma_drm_new_from_caps (const GstCaps * caps)
{
  GstVideoInfoDmaDrm *ret;

  g_return_val_if_fail (caps != NULL, NULL);

  if (!gst_video_is_dma_drm_caps (caps))
    return NULL;

  ret = gst_video_info_dma_drm_new ();

  if (gst_video_info_dma_drm_from_caps (ret, caps)) {
    return ret;
  } else {
    gst_video_info_dma_drm_free (ret);
    return NULL;
  }
}

/**
 * gst_video_info_dma_drm_from_video_info:
 * @drm_info: (out caller-allocates): #GstVideoInfoDmaDrm
 * @info: a #GstVideoInfo
 * @modifier: the associated modifier value.
 *
 * Fills @drm_info if @info's format has a valid drm format and @modifier is also
 * valid
 *
 * Returns: %TRUE if @drm_info is filled correctly.
 *
 * Since: 1.24
 */
gboolean
gst_video_info_dma_drm_from_video_info (GstVideoInfoDmaDrm * drm_info,
    const GstVideoInfo * info, guint64 modifier)
{
  GstVideoFormat format;
  guint32 fourcc;

  g_return_val_if_fail (drm_info != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  if (modifier == DRM_FORMAT_MOD_INVALID)
    return FALSE;
  format = GST_VIDEO_INFO_FORMAT (info);
  fourcc = gst_video_dma_drm_fourcc_from_format (format);
  if (fourcc == DRM_FORMAT_INVALID)
    return FALSE;

  drm_info->vinfo = *info;
  drm_info->drm_fourcc = fourcc;
  drm_info->drm_modifier = modifier;

  /* no need to change format to GST_VIDEO_INFO_DMA_DRM since its modifier is
   * linear */
  if (modifier == DRM_FORMAT_MOD_LINEAR)
    return TRUE;

  return gst_video_info_set_interlaced_format (&drm_info->vinfo,
      GST_VIDEO_FORMAT_DMA_DRM, GST_VIDEO_INFO_INTERLACE_MODE (info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
}

/**
 * gst_video_info_dma_drm_to_video_info:
 * @drm_info: a #GstVideoInfoDmaDrm
 * @info: (out caller-allocates): #GstVideoInfo
 *
 * Convert the #GstVideoInfoDmaDrm into a traditional #GstVideoInfo with
 * recognized video format. For DMA kind memory, the non linear DMA format
 * should be recognized as #GST_VIDEO_FORMAT_DMA_DRM. This helper function
 * sets @info's video format into the default value according to @drm_info's
 * drm_fourcc field.
 *
 * Returns: %TRUE if @info is converted correctly.
 *
 * Since: 1.24
 */
gboolean
gst_video_info_dma_drm_to_video_info (const GstVideoInfoDmaDrm * drm_info,
    GstVideoInfo * info)
{
  GstVideoFormat video_format;
  GstVideoInfo tmp_info;
  guint i;

  g_return_val_if_fail (drm_info, FALSE);
  g_return_val_if_fail (info, FALSE);

  if (GST_VIDEO_INFO_FORMAT (&drm_info->vinfo) != GST_VIDEO_FORMAT_DMA_DRM) {
    *info = drm_info->vinfo;
    return TRUE;
  }

  video_format = gst_video_dma_drm_fourcc_to_format (drm_info->drm_fourcc);
  if (video_format == GST_VIDEO_FORMAT_UNKNOWN)
    return FALSE;

  if (!gst_video_info_set_format (&tmp_info, video_format,
          GST_VIDEO_INFO_WIDTH (&drm_info->vinfo),
          GST_VIDEO_INFO_HEIGHT (&drm_info->vinfo)))
    return FALSE;

  *info = drm_info->vinfo;
  info->finfo = tmp_info.finfo;
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    info->stride[i] = tmp_info.stride[i];
  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    info->offset[i] = tmp_info.offset[i];
  info->size = tmp_info.size;

  return TRUE;
}

/**
 * gst_video_dma_drm_fourcc_from_string:
 * @format_str: a drm format string
 * @modifier: (out) (optional): Return the modifier in @format or %NULL
 * to ignore.
 *
 * Convert the @format_str string into the drm fourcc value. The @modifier is
 * also parsed if we want. Please note that the @format_str should follow the
 * fourcc:modifier kind style, such as NV12:0x0100000000000002
 *
 * Returns: The drm fourcc value or DRM_FORMAT_INVALID if @format_str is
 * invalid.
 *
 * Since: 1.24
 */
guint32
gst_video_dma_drm_fourcc_from_string (const gchar * format_str,
    guint64 * modifier)
{
  const gchar *mod_str;
  guint32 fourcc = DRM_FORMAT_INVALID;
  guint64 m = DRM_FORMAT_MOD_INVALID;
  gboolean big_endian = FALSE;

  g_return_val_if_fail (format_str != NULL, 0);

  mod_str = strchr (format_str, ':');
  if (mod_str) {
    gint fmt_len = mod_str - format_str;

    /* Handle big endian (FOURCC_BE) case */
    if (fmt_len == 7 && strstr (format_str + 4, "_BE")) {
      big_endian = TRUE;
    } else if (fmt_len != 4) {
      /* fourcc always has 4 characters. */
      GST_DEBUG ("%s is not a drm string", format_str);
      return DRM_FORMAT_INVALID;
    }

    mod_str++;
    /* modifier should in hex mode. */
    if (!(mod_str[0] == '0' && (mod_str[1] == 'X' || mod_str[1] == 'x'))) {
      GST_DEBUG ("Invalid modifier string");
      return DRM_FORMAT_INVALID;
    }

    m = g_ascii_strtoull (mod_str, NULL, 16);
    if (m == DRM_FORMAT_MOD_LINEAR) {
      GST_DEBUG ("Unrecognized modifier string %s", mod_str);
      return DRM_FORMAT_INVALID;
    }
  } else {
    gint fmt_len = strlen (format_str);

    /* Handle big endian (FOURCC_BE) case */
    if (fmt_len == 7 && strstr (format_str + 4, "_BE")) {
      big_endian = TRUE;
    } else if (fmt_len != 4) {
      /* fourcc always has 4 characters. */
      GST_DEBUG ("%s is not a drm string", format_str);
      return DRM_FORMAT_INVALID;
    }

    m = DRM_FORMAT_MOD_LINEAR;
  }

  fourcc = GST_MAKE_FOURCC (format_str[0], format_str[1],
      format_str[2], format_str[3]);

  if (big_endian)
    fourcc |= DRM_FORMAT_BIG_ENDIAN;

  if (modifier)
    *modifier = m;

  return fourcc;
}

/**
 * gst_video_dma_drm_fourcc_to_string:
 * @fourcc: a drm fourcc value.
 * @modifier: the associated modifier value.
 *
 * Returns a string containing drm kind format, such as
 * NV12:0x0100000000000002, or NULL otherwise.
 *
 * Returns: (transfer full) (nullable): the drm kind string composed
 *   of to @fourcc and @modifier.
 *
 * Since: 1.24
 */
gchar *
gst_video_dma_drm_fourcc_to_string (guint32 fourcc, guint64 modifier)
{
  gboolean big_endian = FALSE;
  gchar *s;

  g_return_val_if_fail (fourcc != DRM_FORMAT_INVALID, NULL);
  g_return_val_if_fail (modifier != DRM_FORMAT_MOD_INVALID, NULL);

  if (fourcc & DRM_FORMAT_BIG_ENDIAN) {
    big_endian = TRUE;
    fourcc &= ~DRM_FORMAT_BIG_ENDIAN;
  }

  if (modifier == DRM_FORMAT_MOD_LINEAR) {
    s = g_strdup_printf ("%" GST_FOURCC_FORMAT "%s", GST_FOURCC_ARGS (fourcc),
        big_endian ? "_BE" : "");
  } else {
    s = g_strdup_printf ("%" GST_FOURCC_FORMAT "%s:0x%016" G_GINT64_MODIFIER
        "x", GST_FOURCC_ARGS (fourcc), big_endian ? "_BE" : "", modifier);
  }

  return s;
}

/* *INDENT-OFF* */
static const struct FormatMap
{
  GstVideoFormat format;
  guint32 fourcc;
  guint64 modifier;
} format_map[] = {
  {GST_VIDEO_FORMAT_YUY2, DRM_FORMAT_YUYV, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_YVYU, DRM_FORMAT_YVYU, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_UYVY, DRM_FORMAT_UYVY, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_VYUY, DRM_FORMAT_VYUY, DRM_FORMAT_MOD_LINEAR},
  /* No VUYA fourcc define, just mapping it as AYUV. */
  {GST_VIDEO_FORMAT_VUYA, DRM_FORMAT_AYUV, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV12, DRM_FORMAT_NV12, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV12_4L4, DRM_FORMAT_NV12, DRM_FORMAT_MOD_VIVANTE_TILED},
  {GST_VIDEO_FORMAT_NV12_64Z32, DRM_FORMAT_NV12, DRM_FORMAT_MOD_SAMSUNG_64_32_TILE},
  {GST_VIDEO_FORMAT_NV12_16L32S, DRM_FORMAT_NV12, DRM_FORMAT_MOD_MTK_16L_32S_TILE},
  {GST_VIDEO_FORMAT_MT2110T, DRM_FORMAT_NV15, DRM_FORMAT_MOD_MTK(MTK_FMT_MOD_TILE_16L32S | MTK_FMT_MOD_10BIT_LAYOUT_LSBTILED)},
  {GST_VIDEO_FORMAT_MT2110R, DRM_FORMAT_NV15, DRM_FORMAT_MOD_MTK(MTK_FMT_MOD_TILE_16L32S | MTK_FMT_MOD_10BIT_LAYOUT_LSBRASTER)},
  {GST_VIDEO_FORMAT_NV21, DRM_FORMAT_NV21, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV16, DRM_FORMAT_NV16, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV61, DRM_FORMAT_NV61, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV24, DRM_FORMAT_NV24, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_YUV9, DRM_FORMAT_YUV410, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_YVU9, DRM_FORMAT_YVU410, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y41B, DRM_FORMAT_YUV411, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_I420, DRM_FORMAT_YUV420, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_I420_10LE, DRM_FORMAT_S010, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_I422_10LE, DRM_FORMAT_S210, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y444_10LE, DRM_FORMAT_S410, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_I420_12LE, DRM_FORMAT_S012, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_I422_12LE, DRM_FORMAT_S212, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y444_12LE, DRM_FORMAT_S412, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y444_16LE, DRM_FORMAT_S416, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_YV12, DRM_FORMAT_YVU420, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y42B, DRM_FORMAT_YUV422, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y444, DRM_FORMAT_YUV444, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_RGB15, DRM_FORMAT_XRGB1555, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_RGB16, DRM_FORMAT_RGB565, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_BGR16, DRM_FORMAT_BGR565, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_RGB, DRM_FORMAT_BGR888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_BGR, DRM_FORMAT_RGB888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_RGBA, DRM_FORMAT_ABGR8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_RGBx, DRM_FORMAT_XBGR8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_BGRA, DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_BGRx, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_ARGB, DRM_FORMAT_BGRA8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_xRGB, DRM_FORMAT_BGRX8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_ABGR, DRM_FORMAT_RGBA8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_xBGR, DRM_FORMAT_RGBX8888, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y410, DRM_FORMAT_Y410, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y412_LE, DRM_FORMAT_Y412, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y210, DRM_FORMAT_Y210, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_Y212_LE, DRM_FORMAT_Y212, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV12_10LE40, DRM_FORMAT_NV15, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV12_10LE40_4L4, DRM_FORMAT_NV15, DRM_FORMAT_MOD_VIVANTE_TILED},
  {GST_VIDEO_FORMAT_P010_10LE, DRM_FORMAT_P010, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_P012_LE, DRM_FORMAT_P012, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_BGR10A2_LE, DRM_FORMAT_ARGB2101010, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_GRAY8, DRM_FORMAT_R8, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_GRAY16_LE, DRM_FORMAT_R16, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_GRAY16_BE, DRM_FORMAT_R16 | DRM_FORMAT_BIG_ENDIAN, DRM_FORMAT_MOD_LINEAR},
  {GST_VIDEO_FORMAT_NV16_10LE40, DRM_FORMAT_NV20, DRM_FORMAT_MOD_LINEAR},
};
/* *INDENT-ON* */

/**
 * gst_video_dma_drm_fourcc_from_format:
 * @format: a #GstVideoFormat
 *
 * Converting the video format into dma drm fourcc. If no
 * matching fourcc found, then DRM_FORMAT_INVALID is returned.
 *
 * Returns: the DRM_FORMAT_* corresponding to the @format.
 *
 * Since: 1.24
 */
guint32
gst_video_dma_drm_fourcc_from_format (GstVideoFormat format)
{
  guint64 modifier;
  guint32 fourcc;

  fourcc = gst_video_dma_drm_format_from_gst_format (format, &modifier);

  if (fourcc == DRM_FORMAT_INVALID)
    return DRM_FORMAT_INVALID;

  if (modifier != DRM_FORMAT_MOD_LINEAR)
    return DRM_FORMAT_INVALID;

  return fourcc;
}

/**
 * gst_video_dma_drm_format_from_gst_format:
 * @format: a #GstVideoFormat
 * @modifier: (nullable): return location for the modifier
 *
 * Converting the video format into dma drm fourcc/modifier pair.
 * If no matching fourcc found, then DRM_FORMAT_INVALID is returned
 * and @modifier will be set to DRM_FORMAT_MOD_INVALID.
 *
 * Returns: the DRM_FORMAT_* corresponding to @format.
 *
 * Since: 1.26
 */
guint32
gst_video_dma_drm_format_from_gst_format (GstVideoFormat format,
    guint64 * modifier)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].format == format) {
      if (modifier)
        *modifier = format_map[i].modifier;
      return format_map[i].fourcc;
    }
  }

  GST_INFO ("No supported fourcc/modifier for video format %s",
      gst_video_format_to_string (format));

  *modifier = DRM_FORMAT_MOD_INVALID;
  return DRM_FORMAT_INVALID;
}

/**
 * gst_video_dma_drm_fourcc_to_format:
 * @fourcc: the dma drm value.
 *
 * Converting a dma drm fourcc into the video format. If no matching
 * video format found, then GST_VIDEO_FORMAT_UNKNOWN is returned.
 *
 * Returns: the GST_VIDEO_FORMAT_* corresponding to the @fourcc.
 *
 * Since: 1.24
 */
GstVideoFormat
gst_video_dma_drm_fourcc_to_format (guint32 fourcc)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].fourcc == fourcc)
      return format_map[i].format;
  }

  GST_INFO ("No supported video format for fourcc %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (fourcc));
  return GST_VIDEO_FORMAT_UNKNOWN;
}

/**
 * gst_video_dma_drm_format_to_gst_format:
 * @fourcc: the dma drm fourcc value.
 * @modifier: the dma drm modifier.
 *
 * Converting a dma drm fourcc and modifier pair into a #GstVideoFormat. If
 * no matching video format is found, then GST_VIDEO_FORMAT_UNKNOWN is returned.
 *
 * Returns: the GST_VIDEO_FORMAT_* corresponding to the @fourcc and @modifier
 *          pair.
 *
 * Since: 1.26
 */
GstVideoFormat
gst_video_dma_drm_format_to_gst_format (guint32 fourcc, guint64 modifier)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (format_map); i++) {
    if (format_map[i].fourcc == fourcc && format_map[i].modifier == modifier)
      return format_map[i].format;
  }

  gchar *drm_format_str = gst_video_dma_drm_fourcc_to_string (fourcc, modifier);
  GST_INFO ("No support for DRM format %s", drm_format_str);
  g_free (drm_format_str);

  return GST_VIDEO_FORMAT_UNKNOWN;
}
