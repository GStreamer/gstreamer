/* GStreamer Wayland Library
 *
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2014 Collabora Ltd.
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
#include <config.h>
#endif

#include "gstwlshmallocator.h"

GST_DEBUG_CATEGORY (gst_wl_shm_debug);
#define GST_CAT_DEFAULT gst_wl_shm_debug

void
gst_wl_shm_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_wl_shm_debug, "wl_shm", 0, "wl_shm library");
    g_once_init_leave (&_init, 1);
  }
}

static gboolean
gst_wl_shm_validate_video_info (const GstVideoInfo * vinfo)
{
  gint height = GST_VIDEO_INFO_HEIGHT (vinfo);
  gint base_stride = GST_VIDEO_INFO_PLANE_STRIDE (vinfo, 0);
  gsize base_offs = GST_VIDEO_INFO_PLANE_OFFSET (vinfo, 0);
  gint i;
  gsize offs = 0;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (vinfo); i++) {
    guint32 estride;

    /* Overwrite the video info's stride and offset using the pitch calculcated
     * by the kms driver. */
    estride = gst_video_format_info_extrapolate_stride (vinfo->finfo, i,
        base_stride);

    if (estride != GST_VIDEO_INFO_PLANE_STRIDE (vinfo, i))
      return FALSE;

    if (GST_VIDEO_INFO_PLANE_OFFSET (vinfo, i) - base_offs != offs)
      return FALSE;

    /* Note that we cannot negotiate special padding betweem each planes,
     * hence using the display height here. */
    offs +=
        estride * GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (vinfo->finfo, i, height);
  }

  if (vinfo->size < offs)
    return FALSE;

  return TRUE;
}

struct wl_buffer *
gst_wl_shm_memory_construct_wl_buffer (GstBuffer * buf,
    GstWlDisplay * display, const GstVideoInfo * info)
{
  GstMemory *mem;
  GstVideoMeta *vmeta;
  GstVideoInfo vinfo;
  gint width, height, stride;
  gsize plane_offset = 0;
  gsize mem_offset = 0;
  gsize size, memsize, maxsize;
  enum wl_shm_format format;
  struct wl_shm_pool *wl_pool;
  struct wl_buffer *wbuffer;

  /* Ensure the buffer has exactly one memory block */
  if (gst_buffer_n_memory (buf) != 1)
    return NULL;

  /* Retrieve the memory and ensure it is FD backed */
  mem = gst_buffer_peek_memory (buf, 0);
  if (!gst_is_fd_memory (mem))
    return NULL;

  /* Start from caps-derived video info */
  vinfo = *info;

  /* Override layout using GstVideoMeta if present */
  vmeta = gst_buffer_get_video_meta (buf);
  if (vmeta) {
    vinfo.width = vmeta->width;
    vinfo.height = vmeta->height;
    vinfo.stride[0] = vmeta->stride[0];
    vinfo.offset[0] = vmeta->offset[0];
  }

  /* Validate the final video layout */
  if (!gst_wl_shm_validate_video_info (&vinfo)) {
    GST_DEBUG_OBJECT (display, "Unsupported strides and offsets.");
    return NULL;
  }

  width = GST_VIDEO_INFO_WIDTH (&vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&vinfo);
  stride = GST_VIDEO_INFO_PLANE_STRIDE (&vinfo, 0);
  plane_offset = GST_VIDEO_INFO_PLANE_OFFSET (&vinfo, 0);

  /* wl_shm requires the actual memory footprint of the buffer */
  size = plane_offset + (stride * height);
  format = gst_video_format_to_wl_shm_format (GST_VIDEO_INFO_FORMAT (&vinfo));
  memsize = gst_memory_get_sizes (mem, &mem_offset, &maxsize);
  g_return_val_if_fail (size <= memsize, NULL);
  g_return_val_if_fail (gst_wl_display_check_format_for_shm (display, &vinfo),
      NULL);

  GST_DEBUG_OBJECT (display, "Creating wl_buffer from SHM of size %"
      G_GSSIZE_FORMAT " (%d x %d, stride %d), format %s", size, width, height,
      stride, gst_wl_shm_format_to_string (format));

  wl_pool = wl_shm_create_pool (gst_wl_display_get_shm (display),
      gst_fd_memory_get_fd (mem), memsize);
  wbuffer =
      wl_shm_pool_create_buffer (wl_pool, mem_offset + plane_offset, width,
      height, stride, format);
  wl_shm_pool_destroy (wl_pool);

  return wbuffer;
}
