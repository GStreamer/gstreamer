/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2002> Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdio.h>

#include "video-frame.h"
#include "gstvideometa.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

/**
 * gst_video_frame_map_id:
 * @frame: pointer to #GstVideoFrame
 * @info: a #GstVideoInfo
 * @buffer: the buffer to map
 * @id: the frame id to map
 * @flags: #GstMapFlags
 *
 * Use @info and @buffer to fill in the values of @frame with the video frame
 * information of frame @id.
 *
 * When @id is -1, the default frame is mapped. When @id != -1, this function
 * will return %FALSE when there is no GstVideoMeta with that id.
 *
 * All video planes of @buffer will be mapped and the pointers will be set in
 * @frame->data.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_video_frame_map_id (GstVideoFrame * frame, GstVideoInfo * info,
    GstBuffer * buffer, gint id, GstMapFlags flags)
{
  GstVideoMeta *meta;
  gint i;

  g_return_val_if_fail (frame != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  if (id == -1)
    meta = gst_buffer_get_video_meta (buffer);
  else
    meta = gst_buffer_get_video_meta_id (buffer, id);

  /* copy the info */
  frame->info = *info;

  if (meta) {
    frame->info.finfo = gst_video_format_get_info (meta->format);
    frame->info.width = meta->width;
    frame->info.height = meta->height;
    frame->id = meta->id;
    frame->flags = meta->flags;

    for (i = 0; i < info->finfo->n_planes; i++)
      if (!gst_video_meta_map (meta, i, &frame->map[i], &frame->data[i],
              &frame->info.stride[i], flags))
        goto frame_map_failed;
  } else {
    /* no metadata, we really need to have the metadata when the id is
     * specified. */
    if (id != -1)
      goto no_metadata;

    frame->id = id;
    frame->flags = 0;

    if (!gst_buffer_map (buffer, &frame->map[0], flags))
      goto map_failed;

    /* do some sanity checks */
    if (frame->map[0].size < info->size)
      goto invalid_size;

    /* set up pointers */
    for (i = 0; i < info->finfo->n_planes; i++) {
      frame->data[i] = frame->map[0].data + info->offset[i];
    }
  }
  frame->buffer = gst_buffer_ref (buffer);
  frame->meta = meta;

  /* buffer flags enhance the frame flags */
  if (GST_VIDEO_INFO_IS_INTERLACED (info)) {
    if (GST_VIDEO_INFO_INTERLACE_MODE (info) == GST_VIDEO_INTERLACE_MODE_MIXED) {
      if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED)) {
        frame->flags |= GST_VIDEO_FRAME_FLAG_INTERLACED;
      }
    } else
      frame->flags |= GST_VIDEO_FRAME_FLAG_INTERLACED;

    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF))
      frame->flags |= GST_VIDEO_FRAME_FLAG_TFF;
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_RFF))
      frame->flags |= GST_VIDEO_FRAME_FLAG_RFF;
    if (GST_BUFFER_FLAG_IS_SET (buffer, GST_VIDEO_BUFFER_FLAG_ONEFIELD))
      frame->flags |= GST_VIDEO_FRAME_FLAG_ONEFIELD;
  }
  return TRUE;

  /* ERRORS */
no_metadata:
  {
    GST_ERROR ("no GstVideoMeta for id %d", id);
    return FALSE;
  }
frame_map_failed:
  {
    GST_ERROR ("failed to map video frame plane %d", i);
    while (--i >= 0)
      gst_video_meta_unmap (meta, i, &frame->map[i]);
    return FALSE;
  }
map_failed:
  {
    GST_ERROR ("failed to map buffer");
    return FALSE;
  }
invalid_size:
  {
    GST_ERROR ("invalid buffer size %" G_GSIZE_FORMAT " < %" G_GSIZE_FORMAT,
        frame->map[0].size, info->size);
    gst_buffer_unmap (buffer, &frame->map[0]);
    return FALSE;
  }
}

/**
 * gst_video_frame_map:
 * @frame: pointer to #GstVideoFrame
 * @info: a #GstVideoInfo
 * @buffer: the buffer to map
 * @flags: #GstMapFlags
 *
 * Use @info and @buffer to fill in the values of @frame.
 *
 * All video planes of @buffer will be mapped and the pointers will be set in
 * @frame->data.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_video_frame_map (GstVideoFrame * frame, GstVideoInfo * info,
    GstBuffer * buffer, GstMapFlags flags)
{
  return gst_video_frame_map_id (frame, info, buffer, -1, flags);
}

/**
 * gst_video_frame_unmap:
 * @frame: a #GstVideoFrame
 *
 * Unmap the memory previously mapped with gst_video_frame_map.
 */
void
gst_video_frame_unmap (GstVideoFrame * frame)
{
  GstBuffer *buffer;
  GstVideoMeta *meta;
  gint i;

  g_return_if_fail (frame != NULL);

  buffer = frame->buffer;
  meta = frame->meta;

  if (meta) {
    for (i = 0; i < frame->info.finfo->n_planes; i++) {
      gst_video_meta_unmap (meta, i, &frame->map[i]);
    }
  } else {
    gst_buffer_unmap (buffer, &frame->map[0]);
  }
  gst_buffer_unref (buffer);
}

/**
 * gst_video_frame_copy_plane:
 * @dest: a #GstVideoFrame
 * @src: a #GstVideoFrame
 * @plane: a plane
 *
 * Copy the plane with index @plane from @src to @dest.
 *
 * Returns: TRUE if the contents could be copied.
 */
gboolean
gst_video_frame_copy_plane (GstVideoFrame * dest, const GstVideoFrame * src,
    guint plane)
{
  const GstVideoInfo *sinfo;
  GstVideoInfo *dinfo;
  guint w, h, j;
  guint8 *sp, *dp;
  gint ss, ds;

  g_return_val_if_fail (dest != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);

  sinfo = &src->info;
  dinfo = &dest->info;

  g_return_val_if_fail (dinfo->finfo->format == sinfo->finfo->format, FALSE);
  g_return_val_if_fail (dinfo->width == sinfo->width
      && dinfo->height == sinfo->height, FALSE);
  g_return_val_if_fail (dinfo->finfo->n_planes > plane, FALSE);

  sp = src->data[plane];
  dp = dest->data[plane];

  ss = sinfo->stride[plane];
  ds = dinfo->stride[plane];

  /* FIXME. assumes subsampling of component N is the same as plane N, which is
   * currently true for all formats we have but it might not be in the future. */
  w = GST_VIDEO_FRAME_COMP_WIDTH (dest,
      plane) * GST_VIDEO_FRAME_COMP_PSTRIDE (dest, plane);
  h = GST_VIDEO_FRAME_COMP_HEIGHT (dest, plane);

  GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy plane %d, w:%d h:%d ", plane, w, h);

  for (j = 0; j < h; j++) {
    memcpy (dp, sp, w);
    dp += ds;
    sp += ss;
  }
  return TRUE;
}

/**
 * gst_video_frame_copy:
 * @dest: a #GstVideoFrame
 * @src: a #GstVideoFrame
 *
 * Copy the contents from @src to @dest.
 *
 * Returns: TRUE if the contents could be copied.
 */
gboolean
gst_video_frame_copy (GstVideoFrame * dest, const GstVideoFrame * src)
{
  guint i, n_planes;
  const GstVideoInfo *sinfo;
  GstVideoInfo *dinfo;

  g_return_val_if_fail (dest != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);

  sinfo = &src->info;
  dinfo = &dest->info;

  g_return_val_if_fail (dinfo->finfo->format == sinfo->finfo->format, FALSE);
  g_return_val_if_fail (dinfo->width == sinfo->width
      && dinfo->height == sinfo->height, FALSE);

  n_planes = dinfo->finfo->n_planes;
  if (GST_VIDEO_FORMAT_INFO_HAS_PALETTE (sinfo->finfo)) {
    memcpy (dest->data[1], src->data[1], 256 * 4);
    n_planes = 1;
  }

  for (i = 0; i < n_planes; i++)
    gst_video_frame_copy_plane (dest, src, i);

  return TRUE;
}
