/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk> *
 * Copyright 2014 Advanced Micro Devices, Inc.
 *   Author: Christian König <christian.koenig@amd.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxvideo.h"

#include <math.h>

GST_DEBUG_CATEGORY (gst_omx_video_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_debug_category

/* Keep synced with GST_OMX_VIDEO_DEC_SUPPORTED_FORMATS */
GstVideoFormat
gst_omx_video_get_format_from_omx (OMX_COLOR_FORMATTYPE omx_colorformat)
{
  GstVideoFormat format;

  switch (omx_colorformat) {
    case OMX_COLOR_FormatL8:
      format = GST_VIDEO_FORMAT_GRAY8;
      break;
    case OMX_COLOR_FormatYUV420Planar:
    case OMX_COLOR_FormatYUV420PackedPlanar:
      format = GST_VIDEO_FORMAT_I420;
      break;
    case OMX_COLOR_FormatYUV420SemiPlanar:
    case OMX_COLOR_FormatYUV420PackedSemiPlanar:
      format = GST_VIDEO_FORMAT_NV12;
      break;
    case OMX_COLOR_FormatYUV422SemiPlanar:
      format = GST_VIDEO_FORMAT_NV16;
      break;
    case OMX_COLOR_FormatYCbYCr:
      format = GST_VIDEO_FORMAT_YUY2;
      break;
    case OMX_COLOR_FormatYCrYCb:
      format = GST_VIDEO_FORMAT_YVYU;
      break;
    case OMX_COLOR_FormatCbYCrY:
      format = GST_VIDEO_FORMAT_UYVY;
      break;
    case OMX_COLOR_Format32bitARGB8888:
      /* There is a mismatch in omxil specification 4.2.1 between
       * OMX_COLOR_Format32bitARGB8888 and its description
       * Follow the description */
      format = GST_VIDEO_FORMAT_ABGR;
      break;
    case OMX_COLOR_Format32bitBGRA8888:
      /* Same issue as OMX_COLOR_Format32bitARGB8888 */
      format = GST_VIDEO_FORMAT_ARGB;
      break;
    case OMX_COLOR_Format16bitRGB565:
      format = GST_VIDEO_FORMAT_RGB16;
      break;
    case OMX_COLOR_Format16bitBGR565:
      format = GST_VIDEO_FORMAT_BGR16;
      break;
    case OMX_COLOR_Format24bitBGR888:
      format = GST_VIDEO_FORMAT_BGR;
      break;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
      /* Formats defined in extensions have their own enum so disable to -Wswitch warning */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch"
    case OMX_ALG_COLOR_FormatYUV420SemiPlanar10bitPacked:
      format = GST_VIDEO_FORMAT_NV12_10LE32;
      break;
    case OMX_ALG_COLOR_FormatYUV422SemiPlanar10bitPacked:
      format = GST_VIDEO_FORMAT_NV16_10LE32;
      break;
#pragma GCC diagnostic pop
#endif
    default:
      format = GST_VIDEO_FORMAT_UNKNOWN;
      break;
  }

  return format;
}

GList *
gst_omx_video_get_supported_colorformats (GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXComponent *comp = port->comp;
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GList *negotiation_map = NULL;
  gint old_index;
  GstOMXVideoNegotiationMap *m;
  GstVideoFormat f;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  param.xFramerate =
      state ? gst_omx_video_calculate_framerate_q16 (&state->info) : 0;

  old_index = -1;
  do {
    err =
        gst_omx_component_get_parameter (comp,
        OMX_IndexParamVideoPortFormat, &param);

    /* FIXME: Workaround for Bellagio that simply always
     * returns the same value regardless of nIndex and
     * never returns OMX_ErrorNoMore
     */
    if (old_index == param.nIndex)
      break;

    if (err == OMX_ErrorNone || err == OMX_ErrorNoMore) {
      f = gst_omx_video_get_format_from_omx (param.eColorFormat);

      if (f != GST_VIDEO_FORMAT_UNKNOWN) {
        m = g_slice_new (GstOMXVideoNegotiationMap);
        m->format = f;
        m->type = param.eColorFormat;
        negotiation_map = g_list_append (negotiation_map, m);
        GST_DEBUG_OBJECT (comp->parent,
            "Component port %d supports %s (%d) at index %u", port->index,
            gst_video_format_to_string (f), param.eColorFormat,
            (guint) param.nIndex);
      } else {
        GST_DEBUG_OBJECT (comp->parent,
            "Component port %d supports unsupported color format %d at index %u",
            port->index, param.eColorFormat, (guint) param.nIndex);
      }
    }
    old_index = param.nIndex++;
  } while (err == OMX_ErrorNone);

  return negotiation_map;
}

GstCaps *
gst_omx_video_get_caps_for_map (GList * map)
{
  GstCaps *caps = gst_caps_new_empty ();
  GList *l;

  for (l = map; l; l = l->next) {
    GstOMXVideoNegotiationMap *entry = l->data;

    gst_caps_append_structure (caps,
        gst_structure_new ("video/x-raw",
            "format", G_TYPE_STRING,
            gst_video_format_to_string (entry->format), NULL));
  }
  return caps;
}

void
gst_omx_video_negotiation_map_free (GstOMXVideoNegotiationMap * m)
{
  g_slice_free (GstOMXVideoNegotiationMap, m);
}

GstVideoCodecFrame *
gst_omx_video_find_nearest_frame (GstElement * element, GstOMXBuffer * buf,
    GList * frames)
{
  GstVideoCodecFrame *best = NULL;
  GstClockTimeDiff best_diff = G_MAXINT64;
  GstClockTime timestamp;
  GList *l;

  timestamp =
      gst_util_uint64_scale (GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp),
      GST_SECOND, OMX_TICKS_PER_SECOND);

  GST_LOG_OBJECT (element, "look for ts %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    GstClockTimeDiff diff = ABS (GST_CLOCK_DIFF (timestamp, tmp->pts));

    GST_LOG_OBJECT (element,
        "  frame %u diff %" G_GINT64_FORMAT " ts %" GST_TIME_FORMAT,
        tmp->system_frame_number, diff, GST_TIME_ARGS (tmp->pts));

    if (diff < best_diff) {
      best = tmp;
      best_diff = diff;

      if (diff == 0)
        break;
    }
  }

  if (best) {
    gst_video_codec_frame_ref (best);

    /* OMX timestamps are in microseconds while gst ones are in nanoseconds.
     * So if the difference between them is higher than 1 microsecond we likely
     * picked the wrong frame. */
    if (best_diff >= GST_USECOND)
      GST_WARNING_OBJECT (element,
          "Difference between ts (%" GST_TIME_FORMAT ") and frame %u (%"
          GST_TIME_FORMAT ") seems too high (%" GST_TIME_FORMAT ")",
          GST_TIME_ARGS (timestamp), best->system_frame_number,
          GST_TIME_ARGS (best->pts), GST_TIME_ARGS (best_diff));
  } else
    GST_WARNING_OBJECT (element, "No best frame has been found");

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}

OMX_U32
gst_omx_video_calculate_framerate_q16 (GstVideoInfo * info)
{
  g_assert (info);

  if (!info->fps_d)
    return 0;

  /* OMX API expects frame rate to actually be the field rate, so twice
   * the frame rate in interlace mode. */
  return gst_util_uint64_scale_int (1 << 16, GST_VIDEO_INFO_FIELD_RATE_N (info),
      info->fps_d);
}

gboolean
gst_omx_video_is_equal_framerate_q16 (OMX_U32 q16_a, OMX_U32 q16_b)
{
  /* If one of them is 0 use the classic comparison. The value 0 has a special
     meaning and is used to indicate the frame rate is unknown, variable, or
     is not needed. */
  if (!q16_a || !q16_b)
    return q16_a == q16_b;

  /* If the 'percentage change' is less than 1% then consider it equal to avoid
   * an unnecessary re-negotiation. */
  return fabs (((gdouble) q16_a) - ((gdouble) q16_b)) / (gdouble) q16_b < 0.01;
}

gboolean
gst_omx_video_get_port_padding (GstOMXPort * port, GstVideoInfo * info_orig,
    GstVideoAlignment * align)
{
  guint nstride;
  guint nslice_height;
  GstVideoInfo info;
  gsize plane_size[GST_VIDEO_MAX_PLANES];

  gst_video_alignment_reset (align);

  /* Create a copy of @info_orig without any offset/stride as we need a
   * 'standard' version to compute the paddings. */
  gst_video_info_init (&info);
  gst_video_info_set_interlaced_format (&info,
      GST_VIDEO_INFO_FORMAT (info_orig),
      GST_VIDEO_INFO_INTERLACE_MODE (info_orig),
      GST_VIDEO_INFO_WIDTH (info_orig), GST_VIDEO_INFO_HEIGHT (info_orig));

  /* Retrieve the plane sizes */
  if (!gst_video_info_align_full (&info, align, plane_size)) {
    GST_WARNING_OBJECT (port->comp->parent, "Failed to retrieve plane sizes");
    return FALSE;
  }

  nstride = port->port_def.format.video.nStride;
  nslice_height = port->port_def.format.video.nSliceHeight;

  if (nstride > GST_VIDEO_INFO_PLANE_STRIDE (&info, 0)) {
    align->padding_right = nstride - GST_VIDEO_INFO_PLANE_STRIDE (&info, 0);

    if (GST_VIDEO_FORMAT_INFO_IS_COMPLEX (info.finfo)) {
      /* Stride is in bytes while padding is in pixels so we need to do manual
       * conversions for complex formats. */
      switch (GST_VIDEO_INFO_FORMAT (&info)) {
        case GST_VIDEO_FORMAT_NV12_10LE32:
        case GST_VIDEO_FORMAT_NV16_10LE32:
          /* Need ((width + 2) / 3) 32-bits words to store one row,
           * see unpack_NV12_10LE32 in -base.
           *
           * So let's say:
           * - W = the width, in pixels
           * - S = the stride, in bytes
           * - P = the padding, in bytes
           * - Δ = the padding, in pixels
           *
           * we then have:
           * S = ((W+2)/3) * 4
           * S+P = ((W+2+Δ)/3) * 4
           *
           * By solving this system we get:
           * Δ = (3/4) * P
           */
          align->padding_right *= 0.75;
          break;
        default:
          GST_FIXME_OBJECT (port->comp->parent,
              "Stride conversion is not supported for format %s",
              GST_VIDEO_INFO_NAME (&info));
          return FALSE;
      }
    }

    GST_LOG_OBJECT (port->comp->parent,
        "OMX stride (%d) is higher than standard (%d) for port %u; right padding: %d",
        nstride, GST_VIDEO_INFO_PLANE_STRIDE (&info, 0), port->index,
        align->padding_right);
  }

  if (nslice_height > GST_VIDEO_INFO_PLANE_HEIGHT (&info, 0, plane_size)) {
    align->padding_bottom =
        nslice_height - GST_VIDEO_INFO_PLANE_HEIGHT (&info, 0, plane_size);

    if (GST_VIDEO_INFO_INTERLACE_MODE (&info) ==
        GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
      /* GstVideoAlignment defines the alignment for the full frame while
       * OMX gives us the slice height for a single field, so we have to
       * double the vertical padding. */
      GST_DEBUG_OBJECT (port->comp->parent,
          "Double bottom padding because of alternate stream");
      align->padding_bottom *= 2;
    }

    GST_LOG_OBJECT (port->comp->parent,
        "OMX slice height (%d) is higher than standard (%" G_GSIZE_FORMAT
        ") for port %u; vertical padding: %d", nslice_height,
        GST_VIDEO_INFO_PLANE_HEIGHT (&info, 0, plane_size), port->index,
        align->padding_bottom);
  }

  return TRUE;
}
