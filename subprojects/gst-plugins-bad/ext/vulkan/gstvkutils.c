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

/**
 * gst_vulkan_buffer_get_plane_dimensions:
 * @buffer: a #GstBuffer
 * @info: a #GstVideoInfo
 * @plane: the plane to get it dimensions
 * @width: (out) (not nullable): width in texels of @plane
 * @height: (out) (not nullable): height in texels of @plane
 * @row_length: (out) (not nullable): strides in texels of @plane
 * @img_height: (out) (not nullable): height plus paddings of @plane
 *
 * This functions returns the values required for VkBufferImageCopy. In that
 * structure, bufferRowLength and bufferImageHeight are the stride and height of
 * the image in texels, then this function calculates the number of texels
 * (pixels) given the stride (in bytes) and the pixel stride (in bytes too) of
 * the component. For that, we have to find the component that maps to the
 * specified @plane.
 */
void
gst_vulkan_buffer_get_plane_dimensions (GstBuffer * buffer,
    const GstVideoInfo * info, gint plane, guint32 * width, guint32 * height,
    guint32 * row_length, guint32 * img_height)
{
  gint comp[GST_VIDEO_MAX_COMPONENTS], pixel_stride;
  GstVideoMeta *meta;

  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (info && plane >= 0 && plane <= GST_VIDEO_MAX_PLANES);
  g_return_if_fail (width && height && row_length && img_height);

  gst_video_format_info_component (info->finfo, plane, comp);

  *width = GST_VIDEO_INFO_COMP_WIDTH (info, comp[0]);
  *height = GST_VIDEO_INFO_COMP_HEIGHT (info, comp[0]);

  pixel_stride = GST_VIDEO_INFO_COMP_PSTRIDE (info, comp[0]);
  /* FIXME: complex formats like v210, UYVP and IYU1 that have pstride == 0
   * color formats which we don't currently support in GStreamer Vulkan */
  g_assert (pixel_stride > 0);

  meta = gst_buffer_get_video_meta (buffer);
  if (meta) {
    *row_length = meta->stride[plane] + meta->alignment.padding_left
        + meta->alignment.padding_right;
    *img_height = *height + meta->alignment.padding_top
        + meta->alignment.padding_bottom;
  } else {
    *row_length = GST_VIDEO_INFO_COMP_STRIDE (info, comp[0]);
    *img_height = *height;
  }

  g_assert (*row_length % pixel_stride == 0);

  /* Convert row length from bytes to texels for Vulkan's bufferRowLength */
  *row_length /= pixel_stride;
}
