/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstmetavideo.h"

/* video metadata */
const GstMetaInfo *
gst_meta_video_get_info (void)
{
  static const GstMetaInfo *meta_video_info = NULL;

  if (meta_video_info == NULL) {
    meta_video_info = gst_meta_register (GST_META_API_VIDEO, "GstMetaVideo",
        sizeof (GstMetaVideo),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL,
        (GstMetaCopyFunction) NULL, (GstMetaTransformFunction) NULL);
  }
  return meta_video_info;
}

/**
 * gst_buffer_get_meta_video_id:
 * @buffer: a #GstBuffer
 * @id: a metadata id
 *
 * Find the #GstMetaVideo on @buffer with the given @id.
 *
 * Buffers can contain multiple #GstMetaVideo metadata items when dealing with
 * multiview buffers.
 *
 * Returns: the #GstMetaVideo with @id or %NULL when there is no such metadata
 * on @buffer.
 */
GstMetaVideo *
gst_buffer_get_meta_video_id (GstBuffer * buffer, gint id)
{
  gpointer state = NULL;
  GstMeta *meta;
  const GstMetaInfo *info = GST_META_INFO_VIDEO;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      GstMetaVideo *vmeta = (GstMetaVideo *) meta;
      if (vmeta->id == id)
        return vmeta;
    }
  }
  return NULL;
}

/**
 * gst_buffer_add_meta_video:
 * @buffer: a #GstBuffer
 * @flags: #GstVideoFlags
 * @format: a #GstVideoFormat
 * @width: the width
 * @height: the height
 *
 * Attaches GstVideoMeta metadata to @buffer with the given parameters and the
 * default offsets and strides for @format and @width x @height.
 *
 * This function calculates the default offsets and strides and then calls
 * gst_buffer_add_meta_video_full() with them.
 *
 * Returns: the #GstMetaVideo on @buffer.
 */
GstMetaVideo *
gst_buffer_add_meta_video (GstBuffer * buffer, GstVideoFlags flags,
    GstVideoFormat format, guint width, guint height)
{
  GstMetaVideo *meta;
  GstVideoInfo info;

  gst_video_info_set_format (&info, format, width, height);

  meta = gst_buffer_add_meta_video_full (buffer, flags, format, width, height,
      info.finfo->n_planes, info.offset, info.stride);

  return meta;
}

/**
 * gst_buffer_add_meta_video_full:
 * @buffer: a #GstBuffer
 * @flags: #GstVideoFlags
 * @format: a #GstVideoFormat
 * @width: the width
 * @height: the height
 * @n_planes: number of planes
 * @offset: offset of each plane
 * @stride: stride of each plane
 *
 * Attaches GstVideoMeta metadata to @buffer with the given parameters.
 *
 * Returns: the #GstMetaVideo on @buffer.
 */
GstMetaVideo *
gst_buffer_add_meta_video_full (GstBuffer * buffer, GstVideoFlags flags,
    GstVideoFormat format, guint width, guint height,
    guint n_planes, gsize offset[GST_VIDEO_MAX_PLANES],
    gint stride[GST_VIDEO_MAX_PLANES])
{
  GstMetaVideo *meta;
  guint i;

  meta =
      (GstMetaVideo *) gst_buffer_add_meta (buffer, GST_META_INFO_VIDEO, NULL);

  meta->flags = flags;
  meta->format = format;
  meta->id = 0;
  meta->width = width;
  meta->height = height;
  meta->buffer = buffer;

  meta->n_planes = n_planes;
  for (i = 0; i < n_planes; i++) {
    meta->offset[i] = offset[i];
    meta->stride[i] = stride[i];
  }
  return meta;
}

static GstMemory *
find_mem_for_offset (GstBuffer * buffer, guint * offset, GstMapFlags flags)
{
  guint n, i;
  GstMemory *res = NULL;

  n = gst_buffer_n_memory (buffer);
  for (i = 0; i < n; i++) {
    GstMemory *mem = NULL;
    gsize size;

    mem = gst_buffer_peek_memory (buffer, i, flags);
    size = gst_memory_get_sizes (mem, NULL, NULL);

    if (*offset < size) {
      res = mem;
      break;
    }
    *offset -= size;
  }
  return res;
}

/**
 * gst_meta_video_map:
 * @meta: a #GstVideoMeta
 * @plane: a plane
 * @stride: result stride
 * @flags: @GstMapFlags
 *
 * Map the video plane with index @plane in @meta and return a pointer to the
 * first byte of the plane and the stride of the plane.
 *
 * Returns: a pointer to the first byte of the plane data
 */
gpointer
gst_meta_video_map (GstMetaVideo * meta, guint plane, gint * stride,
    GstMapFlags flags)
{
  guint offset;
  gboolean write;
  GstBuffer *buffer;
  GstMemory *mem;
  guint8 *base;

  g_return_val_if_fail (meta != NULL, NULL);
  g_return_val_if_fail (plane < meta->n_planes, NULL);
  g_return_val_if_fail (stride != NULL, NULL);

  buffer = meta->buffer;
  g_return_val_if_fail (buffer != NULL, NULL);

  write = (flags & GST_MAP_WRITE) != 0;
  g_return_val_if_fail (!write || gst_buffer_is_writable (buffer), NULL);

  offset = meta->offset[plane];
  *stride = meta->stride[plane];
  /* find the memory block for this plane, this is the memory block containing
   * the plane offset */
  mem = find_mem_for_offset (buffer, &offset, flags);

  base = gst_memory_map (mem, NULL, NULL, flags);

  /* move to the right offset inside the block */
  return base + offset;
}

/**
 * gst_meta_video_unmap:
 * @meta: a #GstVideoMeta
 * @plane: a plane
 * @data: the data to unmap
 *
 * Unmap previously mapped data with gst_video_meta_map().
 *
 * Returns: TRUE if the memory was successfully unmapped.
 */
gboolean
gst_meta_video_unmap (GstMetaVideo * meta, guint plane, gpointer data)
{
  guint offset;
  GstBuffer *buffer;
  GstMemory *mem;
  guint8 *base;

  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (plane < meta->n_planes, FALSE);

  buffer = meta->buffer;
  g_return_val_if_fail (buffer != NULL, FALSE);

  offset = meta->offset[plane];
  mem = find_mem_for_offset (buffer, &offset, GST_MAP_READ);
  base = data;

  gst_memory_unmap (mem, base - offset, -1);

  return TRUE;
}

const GstMetaInfo *
gst_meta_video_crop_get_info (void)
{
  static const GstMetaInfo *meta_video_crop_info = NULL;

  if (meta_video_crop_info == NULL) {
    meta_video_crop_info =
        gst_meta_register (GST_META_API_VIDEO_CROP, "GstMetaVideoCrop",
        sizeof (GstMetaVideoCrop), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaCopyFunction) NULL,
        (GstMetaTransformFunction) NULL);
  }
  return meta_video_crop_info;
}
