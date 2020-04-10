/*
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

#ifndef __GST_OMX_VIDEO_H__
#define __GST_OMX_VIDEO_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideoencoder.h>

#include "gstomx.h"

G_BEGIN_DECLS

/* Keep synced with gst_omx_video_get_format_from_omx(). Sort by decreasing quality */
#define GST_OMX_VIDEO_DEC_SUPPORTED_FORMATS "{ NV16_10LE32, NV12_10LE32, " \
  "NV16, YUY2, YVYU, UYVY, NV12, I420, RGB16, BGR16, ABGR, ARGB, GRAY8 }"

#define GST_OMX_VIDEO_ENC_SUPPORTED_FORMATS "{ NV16_10LE32, NV12_10LE32, " \
  "NV16, NV12, I420, GRAY8 }"

typedef struct
{
  GstVideoFormat format;
  OMX_COLOR_FORMATTYPE type;
} GstOMXVideoNegotiationMap;

GstVideoFormat
gst_omx_video_get_format_from_omx (OMX_COLOR_FORMATTYPE omx_colorformat);

GList *
gst_omx_video_get_supported_colorformats (GstOMXPort * port,
    GstVideoCodecState * state);

GstCaps * gst_omx_video_get_caps_for_map(GList * map);

void
gst_omx_video_negotiation_map_free (GstOMXVideoNegotiationMap * m);

GstVideoCodecFrame *
gst_omx_video_find_nearest_frame (GstElement * element, GstOMXBuffer * buf, GList * frames);

OMX_U32 gst_omx_video_calculate_framerate_q16 (GstVideoInfo * info);

gboolean gst_omx_video_is_equal_framerate_q16 (OMX_U32 q16_a, OMX_U32 q16_b);

gboolean gst_omx_video_get_port_padding (GstOMXPort * port, GstVideoInfo * info_orig,
    GstVideoAlignment * align);

G_END_DECLS

#endif /* __GST_OMX_VIDEO_H__ */
