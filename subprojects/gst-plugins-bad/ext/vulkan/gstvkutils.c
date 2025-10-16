/* GStreamer
 *
 * GStreamer Vulkan plugins utilities
 * Copyright (C) 2025 Igalia, S.L.
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

#include "gstvkutils.h"

/**
 * gst_vulkan_buffer_peek_plane_memory:
 * @buffer: a #GstBuffer
 * @vinfo: a #GstVideoInfo
 * @buffer: a #GstBuffer
 * @plane: the plane number
 * @cat: the #GstBufferCategory to log into
 *
 * Returns: (transfer none): the #GstMemory that belongs to @plane
 */
GstMemory *
_gst_vulkan_buffer_peek_plane_memory (GstBuffer * buffer,
    const GstVideoInfo * vinfo, gint plane, GstDebugCategory * cat)
{
  guint idx, len;
  gsize offset, skip;
  GstVideoMeta *vmeta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (vinfo, NULL);
  g_return_val_if_fail (plane >= 0 && plane <= GST_VIDEO_MAX_PLANES, NULL);
  g_return_val_if_fail (cat, NULL);

  vmeta = gst_buffer_get_video_meta (buffer);
  if (vmeta)
    offset = vmeta->offset[plane];
  else
    offset = GST_VIDEO_INFO_PLANE_OFFSET (vinfo, plane);

  if (!gst_buffer_find_memory (buffer, offset, 1, &idx, &len, &skip)) {
    GST_CAT_WARNING (cat,
        "Buffer's plane %u has no memory at offset %" G_GSIZE_FORMAT, plane,
        offset);
    return NULL;
  }

  return gst_buffer_peek_memory (buffer, idx);
}
