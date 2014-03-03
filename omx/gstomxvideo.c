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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxvideo.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_debug_category

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

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  if (!state || state->info.fps_n == 0)
    param.xFramerate = 0;
  else
    param.xFramerate = (state->info.fps_n << 16) / (state->info.fps_d);

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
      switch (param.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedPlanar:
          m = g_slice_new (GstOMXVideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_I420;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          GST_DEBUG_OBJECT (comp->parent,
              "Component supports I420 (%d) at index %u",
              param.eColorFormat, (guint) param.nIndex);
          break;
        case OMX_COLOR_FormatYUV420SemiPlanar:
          m = g_slice_new (GstOMXVideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_NV12;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          GST_DEBUG_OBJECT (comp->parent,
              "Component supports NV12 (%d) at index %u",
              param.eColorFormat, (guint) param.nIndex);
          break;
        default:
          GST_DEBUG_OBJECT (comp->parent,
              "Component supports unsupported color format %d at index %u",
              param.eColorFormat, (guint) param.nIndex);
          break;
      }
    }
    old_index = param.nIndex++;
  } while (err == OMX_ErrorNone);

  return negotiation_map;
}

GstCaps *
gst_omx_video_get_caps_4_map (GList * map)
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
gst_omx_video_find_nearest_frame (GstOMXBuffer * buf, GList * frames)
{
  GstVideoCodecFrame *best = NULL;
  GstClockTimeDiff best_diff = G_MAXINT64;
  GstClockTime timestamp;
  GList *l;

  timestamp =
      gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
      OMX_TICKS_PER_SECOND);

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    GstClockTimeDiff diff = ABS (GST_CLOCK_DIFF (timestamp, tmp->pts));

    if (diff < best_diff) {
      best = tmp;
      best_diff = diff;

      if (diff == 0)
        break;
    }
  }

  if (best)
    gst_video_codec_frame_ref (best);

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}
