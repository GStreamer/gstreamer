/* GStreamer
 * Copyright (C) 2020 Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#include "gstv4l2format.h"

#include "linux/drm_fourcc.h"

#define GST_CAT_DEFAULT gstv4l2codecs_debug
GST_DEBUG_CATEGORY_EXTERN (gstv4l2codecs_debug);

#ifndef V4L2_PIX_FMT_NC12
#define V4L2_PIX_FMT_NC12 v4l2_fourcc('N', 'C', '1', '2')       /* Y/CbCr 4:2:0 (128b cols) */
#endif

#ifndef V4L2_PIX_FMT_NV15
#define V4L2_PIX_FMT_NV15    v4l2_fourcc('N', 'V', '1', '5')    /* 15  Y/CbCr 4:2:0 10-bit packed */
#endif

typedef struct
{
  guint32 v4l2_pix_fmt;
  GstVideoFormat gst_fmt;
  guint32 drm_fourcc;
  guint64 drm_modifier;
  gint num_planes;
} GstV4l2FormatDesc;

/* *INDENT-OFF* */
static const GstV4l2FormatDesc gst_v4l2_descriptions[] = {
  {V4L2_PIX_FMT_MM21,             GST_VIDEO_FORMAT_NV12_16L32S,     DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_MT2110T,          GST_VIDEO_FORMAT_MT2110T,         DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_MT2110R,          GST_VIDEO_FORMAT_MT2110R,         DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_NV12,             GST_VIDEO_FORMAT_NV12,            DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_NV12_4L4,         GST_VIDEO_FORMAT_NV12_4L4,        DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_NV15_4L4,         GST_VIDEO_FORMAT_NV12_10LE40_4L4, DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_NV15,             GST_VIDEO_FORMAT_NV12_10LE40,     DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_P010,             GST_VIDEO_FORMAT_P010_10LE,       DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_SUNXI_TILED_NV12, GST_VIDEO_FORMAT_NV12_32L32,      DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_YUV420M,          GST_VIDEO_FORMAT_I420,            DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_YUYV,             GST_VIDEO_FORMAT_YUY2,            DRM_FORMAT_INVALID, DRM_FORMAT_MOD_INVALID, 0},
  {V4L2_PIX_FMT_NC12,             GST_VIDEO_FORMAT_UNKNOWN,         DRM_FORMAT_NV12,    DRM_FORMAT_MOD_BROADCOM_SAND128, 2},
};
/* *INDENT-ON* */
#define GST_V4L2_FORMAT_DESC_COUNT (G_N_ELEMENTS (gst_v4l2_descriptions))

static const GstV4l2FormatDesc *
gst_v4l2_format_get_descriptions (void)
{
  static GstV4l2FormatDesc v4l2_descs[GST_V4L2_FORMAT_DESC_COUNT];
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    for (int i = 0; i < GST_V4L2_FORMAT_DESC_COUNT; i++) {
      v4l2_descs[i].v4l2_pix_fmt = gst_v4l2_descriptions[i].v4l2_pix_fmt;
      if (gst_v4l2_descriptions[i].gst_fmt != GST_VIDEO_FORMAT_UNKNOWN) {
        const GstVideoFormatInfo *info;
        guint64 drm_modifier;

        v4l2_descs[i].gst_fmt = gst_v4l2_descriptions[i].gst_fmt;
        v4l2_descs[i].drm_fourcc =
            gst_video_dma_drm_format_from_gst_format (gst_v4l2_descriptions
            [i].gst_fmt, &drm_modifier);
        v4l2_descs[i].drm_modifier = drm_modifier;

        info = gst_video_format_get_info (gst_v4l2_descriptions[i].gst_fmt);
        v4l2_descs[i].num_planes = GST_VIDEO_FORMAT_INFO_N_PLANES (info);
      } else if (gst_v4l2_descriptions[i].drm_fourcc != DRM_FORMAT_INVALID &&
          gst_v4l2_descriptions[i].num_planes > 0) {
        v4l2_descs[i].gst_fmt = GST_VIDEO_FORMAT_DMA_DRM;
        v4l2_descs[i].drm_fourcc = gst_v4l2_descriptions[i].drm_fourcc;
        v4l2_descs[i].drm_modifier = gst_v4l2_descriptions[i].drm_modifier;
        v4l2_descs[i].num_planes = gst_v4l2_descriptions[i].num_planes;
      } else {
        g_assert_not_reached ();
      }
    }

    g_once_init_leave (&once, 1);
  }

  return v4l2_descs;
}

static const GstV4l2FormatDesc *
gst_v4l2_lookup_pix_format (guint32 pix_format)
{
  const GstV4l2FormatDesc *fmt_descs = gst_v4l2_format_get_descriptions ();

  for (int i = 0; i < GST_V4L2_FORMAT_DESC_COUNT; i++) {
    if (fmt_descs[i].v4l2_pix_fmt == pix_format)
      return &fmt_descs[i];
  }
  return NULL;
}

static const GstV4l2FormatDesc *
gst_v4l2_lookup_drm_format (guint32 drm_fourcc, guint64 drm_modifier)
{
  const GstV4l2FormatDesc *fmt_descs = gst_v4l2_format_get_descriptions ();

  if (drm_fourcc == DRM_FORMAT_INVALID)
    return NULL;

  for (int i = 0; i < GST_V4L2_FORMAT_DESC_COUNT; i++) {
    if (fmt_descs[i].drm_fourcc == drm_fourcc &&
        fmt_descs[i].drm_modifier == drm_modifier)
      return &fmt_descs[i];
  }
  return NULL;
}

static const GstV4l2FormatDesc *
gst_v4l2_loopup_video_format (GstVideoFormat gst_format)
{
  const GstV4l2FormatDesc *fmt_descs = gst_v4l2_format_get_descriptions ();

  if (gst_format == GST_VIDEO_FORMAT_UNKNOWN ||
      gst_format == GST_VIDEO_FORMAT_DMA_DRM)
    return NULL;

  for (int i = 0; i < GST_V4L2_FORMAT_DESC_COUNT; i++) {
    if (fmt_descs[i].gst_fmt == gst_format)
      return &fmt_descs[i];
  }
  return NULL;
}

static void
set_stride (GstVideoInfoDmaDrm * info, gint plane, gint stride)
{
  const GstVideoFormatInfo *finfo = info->vinfo.finfo;

  if (GST_VIDEO_FORMAT_INFO_IS_TILED (finfo)) {
    guint x_tiles, y_tiles, tile_height, padded_height;

    tile_height = GST_VIDEO_FORMAT_INFO_TILE_HEIGHT (finfo, plane);

    padded_height = GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, plane,
        info->vinfo.height);

    x_tiles = stride / GST_VIDEO_FORMAT_INFO_TILE_STRIDE (finfo, plane);
    y_tiles = (padded_height + tile_height - 1) / tile_height;
    info->vinfo.stride[plane] = GST_VIDEO_TILE_MAKE_STRIDE (x_tiles, y_tiles);
  } else {
    info->vinfo.stride[plane] = stride;
  }
}

gboolean
gst_v4l2_format_to_dma_drm_info (struct v4l2_format *fmt,
    GstVideoInfoDmaDrm * out_drm_info)
{
  struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
  struct v4l2_pix_format *pix = &fmt->fmt.pix;
  gint n_planes;
  gint plane;
  gsize offset = 0;
  gboolean extrapolate = FALSE;
  GstVideoFormat format;
  guint32 drm_fourcc;
  guint64 drm_mod;

  if (!gst_v4l2_format_to_video_format (pix_mp->pixelformat, &format) ||
      !gst_v4l2_format_to_drm_format (pix_mp->pixelformat, &drm_fourcc,
          &drm_mod))
    return FALSE;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_DMA_DRM
      || drm_fourcc != DRM_FORMAT_INVALID, FALSE);

  gst_video_info_dma_drm_init (out_drm_info);
  out_drm_info->vinfo.finfo = gst_video_format_get_info (format);

  out_drm_info->vinfo.width = pix_mp->width;
  out_drm_info->vinfo.height = pix_mp->height;
  out_drm_info->drm_fourcc = drm_fourcc;
  out_drm_info->drm_modifier = drm_mod;

  if (V4L2_TYPE_IS_MULTIPLANAR (fmt->type)) {
    out_drm_info->vinfo.size = 0;
    for (plane = 0; plane < pix_mp->num_planes; plane++)
      out_drm_info->vinfo.size += pix_mp->plane_fmt[plane].sizeimage;
    n_planes = pix_mp->num_planes;
  } else {
    out_drm_info->vinfo.size = pix->sizeimage;
    n_planes = 1;
  }

  if (drm_fourcc == DRM_FORMAT_NV12
      && drm_mod == DRM_FORMAT_MOD_BROADCOM_SAND128) {
    out_drm_info->vinfo.offset[1] = pix_mp->height * 128;
    out_drm_info->vinfo.stride[0] = pix_mp->plane_fmt[0].bytesperline;
    out_drm_info->vinfo.stride[1] = pix_mp->plane_fmt[0].bytesperline;
    return TRUE;
  }

  /*
   * When single allocation formats are used for planar formats we need to
   * extrapolate the per-plane stride. Do this check once to prevent
   * complex inner loop.
   */
  if (n_planes == 1 && gst_v4l2_format_get_n_planes (out_drm_info) != n_planes)
    extrapolate = TRUE;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_DMA_DRM
      || drm_mod == DRM_FORMAT_MOD_LINEAR || !extrapolate, FALSE);

  for (plane = 0; plane < gst_v4l2_format_get_n_planes (out_drm_info); plane++) {
    gint stride;

    if (V4L2_TYPE_IS_MULTIPLANAR (fmt->type)) {
      if (extrapolate)
        stride =
            gst_video_format_info_extrapolate_stride (out_drm_info->vinfo.finfo,
            plane, pix_mp->plane_fmt[0].bytesperline);
      else
        stride = pix_mp->plane_fmt[plane].bytesperline;
    } else {
      if (extrapolate)
        stride =
            gst_video_format_info_extrapolate_stride (out_drm_info->vinfo.finfo,
            plane, pix->bytesperline);
      else
        stride = pix->bytesperline;
    }

    set_stride (out_drm_info, plane, stride);
    out_drm_info->vinfo.offset[plane] = offset;

    if ((V4L2_TYPE_IS_MULTIPLANAR (fmt->type) && !extrapolate))
      offset += pix_mp->plane_fmt[plane].sizeimage;
    else
      offset +=
          stride *
          GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (out_drm_info->vinfo.finfo, plane,
          pix_mp->height);
  }

  /* Check that the extrapolation didn't overflow the reported sizeimage */
  if (extrapolate && offset > out_drm_info->vinfo.size) {
    GST_ERROR ("Extrapolated plane offset overflow the image size.");
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_v4l2_format_to_video_format (guint32 pix_fmt, GstVideoFormat * out_gst_fmt)
{
  const GstV4l2FormatDesc *fmt_desc;

  fmt_desc = gst_v4l2_lookup_pix_format (pix_fmt);
  if (!fmt_desc)
    return FALSE;

  if (out_gst_fmt)
    *out_gst_fmt = fmt_desc->gst_fmt;
  return TRUE;
}

gboolean
gst_v4l2_format_to_drm_format (guint32 pix_fmt, guint32 * out_drm_fourcc,
    guint64 * out_drm_mod)
{
  const GstV4l2FormatDesc *fmt_desc;

  fmt_desc = gst_v4l2_lookup_pix_format (pix_fmt);
  if (!fmt_desc)
    return FALSE;

  if (out_drm_fourcc)
    *out_drm_fourcc = fmt_desc->drm_fourcc;
  if (out_drm_mod)
    *out_drm_mod = fmt_desc->drm_modifier;
  return TRUE;
}

gboolean
gst_v4l2_format_from_video_format (GstVideoFormat format, guint32 * out_pix_fmt)
{
  const GstV4l2FormatDesc *fmt_desc;

  fmt_desc = gst_v4l2_loopup_video_format (format);
  if (!fmt_desc)
    return FALSE;

  if (out_pix_fmt)
    *out_pix_fmt = fmt_desc->v4l2_pix_fmt;
  return TRUE;
}

gboolean
gst_v4l2_format_from_drm_format (guint32 drm_fourcc, guint64 drm_mod,
    guint32 * out_pix_fmt)
{
  const GstV4l2FormatDesc *fmt_desc;

  fmt_desc = gst_v4l2_lookup_drm_format (drm_fourcc, drm_mod);
  if (!fmt_desc)
    return FALSE;

  if (out_pix_fmt)
    *out_pix_fmt = fmt_desc->v4l2_pix_fmt;
  return TRUE;
}

guint
gst_v4l2_format_get_n_planes (GstVideoInfoDmaDrm * info)
{
  const GstV4l2FormatDesc *fmt_desc;

  fmt_desc = gst_v4l2_loopup_video_format (info->vinfo.finfo->format);
  if (fmt_desc)
    return fmt_desc->num_planes;

  fmt_desc = gst_v4l2_lookup_drm_format (info->drm_fourcc, info->drm_modifier);
  if (fmt_desc)
    return fmt_desc->num_planes;

  g_warn_if_reached ();
  return 0;
}
