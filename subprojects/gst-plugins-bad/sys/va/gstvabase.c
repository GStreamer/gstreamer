/* GStreamer
 * Copyright (C) 2023 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include "gstvabase.h"

#include <gst/video/video.h>

#include <gst/va/gstvavideoformat.h>
#include <gst/va/vasurfaceimage.h>

#define GST_CAT_DEFAULT (importer->debug_category)

static gboolean
_try_import_buffer (GstVaBufferImporter * importer, GstBuffer * inbuf)
{
  guint usage_hint;

  usage_hint = va_get_surface_usage_hint (importer->entrypoint, GST_PAD_SINK);

  return gst_va_display_import_buffer (importer->display, inbuf,
      importer->in_drm_info, usage_hint) != VA_INVALID_ID;
}

GstFlowReturn
gst_va_buffer_importer_import (GstVaBufferImporter * importer,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstBuffer *buffer = NULL;
  GstBufferPool *pool;
  GstFlowReturn ret;
  GstVideoFrame in_frame, out_frame;
  gboolean copied;

  if (_try_import_buffer (importer, inbuf)) {
    *outbuf = gst_buffer_ref (inbuf);
    return GST_FLOW_OK;
  }

  /* input buffer doesn't come from a vapool, thus it is required to
   * have a pool, grab from it a new buffer and copy the input
   * buffer to the new one */
  if (!(pool = importer->get_sinkpad_pool (importer->element,
              importer->pool_data)))
    return GST_FLOW_ERROR;

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_LOG_OBJECT (importer->element, "copying input frame");

  if (!gst_video_frame_map (&in_frame, importer->in_info, inbuf, GST_MAP_READ))
    goto invalid_buffer;

  if (!gst_video_frame_map (&out_frame, importer->sinkpad_info, buffer,
          GST_MAP_WRITE)) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied)
    goto invalid_buffer;

  if (!gst_buffer_copy_into (buffer, inbuf, GST_BUFFER_COPY_FLAGS |
          GST_BUFFER_COPY_TIMESTAMPS, 0, -1)) {
    GST_WARNING_OBJECT (importer->element,
        "Couldn't import buffer flags and timestamps");
  }

  *outbuf = buffer;

  return GST_FLOW_OK;

invalid_buffer:
  {
    GST_ELEMENT_WARNING (importer->element, STREAM, FORMAT, (NULL),
        ("invalid video buffer received"));
    if (buffer)
      gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}


gboolean
gst_va_base_convert_caps_to_va (GstCaps * caps)
{
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  /* For DMA buffer, we can only import linear buffers. Replace the drm-format
   * into format field. */
  if (gst_video_is_dma_drm_caps (caps)) {
    GstVideoInfoDmaDrm dma_info;
    GstVideoInfo info;

    if (!gst_video_info_dma_drm_from_caps (&dma_info, caps))
      return FALSE;

    if (dma_info.drm_modifier != DRM_FORMAT_MOD_LINEAR)
      return FALSE;

    if (!gst_va_dma_drm_info_to_video_info (&dma_info, &info))
      return FALSE;

    gst_caps_set_simple (caps, "format", G_TYPE_STRING,
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&info)), NULL);
    gst_structure_remove_field (gst_caps_get_structure (caps, 0), "drm-format");
  }

  gst_caps_set_features_simple (caps,
      gst_caps_features_new_single_static_str (GST_CAPS_FEATURE_MEMORY_VA));

  return TRUE;
}

/**
 * gst_va_base_propose_allocation:
 * @element: the element answering the query
 * @cat: debug category for logging
 * @display: a #GstVaDisplay
 * @entrypoint: the VA entrypoint consuming the surfaces
 * @surface_formats: (nullable): surface formats supported by @entrypoint
 * @min_buffers: buffers needed by @element
 * @query: the ALLOCATION query to answer
 *
 * Answers an upstream ALLOCATION query. If upstream wants system memory and
 * udmabuf is available, a dmabuf pool is proposed so buffers can be imported
 * without copy. Otherwise a VA pool is proposed.
 *
 * Returns: %TRUE if the query was answered.
 */
gboolean
gst_va_base_propose_allocation (GstElement * element, GstDebugCategory * cat,
    GstVaDisplay * display, VAEntrypoint entrypoint, GArray * surface_formats,
    guint min_buffers, GstQuery * query)
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstBufferPool *pool;
  GstCaps *caps;
  gboolean need_pool;
  gboolean ret = FALSE;
  guint size, usage_hint;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps)
    return FALSE;

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (!need_pool)
    return TRUE;

  /* Testing showed that we need at least 3 buffers for good performance */
  min_buffers = MAX (min_buffers, 3);

  /* Prefer udmabuf allocator if available and upstream wants system memory */
  if (gst_caps_features_contains (gst_caps_get_features (caps, 0),
          GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY)) {
    GstVideoInfo vinfo;

    if (!gst_video_info_from_caps (&vinfo, caps))
      return FALSE;

    allocator = gst_udmabuf_allocator_get ();
    if (allocator) {
      pool = gst_video_dmabuf_pool_new ();
      gst_query_add_allocation_pool (query, pool, vinfo.size, min_buffers, 0);
      GST_CAT_DEBUG_OBJECT (cat, element,
          "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
          pool, allocator);
      gst_object_unref (pool);
      gst_object_unref (allocator);
      return TRUE;
    }
  }

  if (gst_video_is_dma_drm_caps (caps))
    allocator = gst_va_dmabuf_allocator_new (display);
  else
    allocator = gst_va_allocator_new (display, surface_formats);
  if (!allocator)
    return FALSE;

  gst_allocation_params_init (&params);
  usage_hint = va_get_surface_usage_hint (entrypoint, GST_PAD_SINK);

  pool = gst_va_pool_new_with_config (caps, min_buffers, 0, usage_hint,
      GST_VA_FEATURE_AUTO, allocator, &params);
  if (!pool || !gst_va_pool_get_buffer_size (pool, &size)) {
    GST_CAT_WARNING_OBJECT (cat, element, "failed to set config");
    goto done;
  }

  gst_query_add_allocation_param (query, allocator, &params);
  gst_query_add_allocation_pool (query, pool, size, min_buffers, 0);
  ret = TRUE;

  GST_CAT_DEBUG_OBJECT (cat, element,
      "proposing %" GST_PTR_FORMAT " with allocator %" GST_PTR_FORMAT,
      pool, allocator);

done:
  gst_clear_object (&pool);
  gst_object_unref (allocator);

  return ret;
}
