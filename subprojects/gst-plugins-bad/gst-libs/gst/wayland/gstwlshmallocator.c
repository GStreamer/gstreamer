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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <drm_fourcc.h>

#define GST_CAT_DEFAULT gst_wl_shm_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

G_DEFINE_TYPE_WITH_CODE (GstWlShmAllocator, gst_wl_shm_allocator,
    GST_TYPE_FD_ALLOCATOR,
    GST_DEBUG_CATEGORY_INIT (gst_wl_shm_debug, "wlshm", 0, "wlshm library");
    );

static GstMemory *
gst_wl_shm_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstWlShmAllocator *self = GST_WL_SHM_ALLOCATOR (allocator);
  char filename[1024];
  static int init = 0;
  int fd;
  GstMemory *mem;
  GstMapInfo info;

  /* TODO: make use of the allocation params, if necessary */

#ifdef HAVE_MEMFD_CREATE
  fd = memfd_create ("gst-wayland-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd >= 0) {
    /* We can add this seal before calling posix_fallocate(), as
     * the file is currently zero-sized anyway.
     *
     * There is also no need to check for the return value, we
     * couldn't do anything with it anyway.
     */
    fcntl (fd, F_ADD_SEALS, F_SEAL_SHRINK);
  } else
#endif
  {
    /* allocate shm pool */
    snprintf (filename, 1024, "%s/%s-%d-%s", g_get_user_runtime_dir (),
        "wayland-shm", init++, "XXXXXX");

    fd = g_mkstemp (filename);
    if (fd < 0) {
      GST_ERROR_OBJECT (self, "opening temp file %s failed: %s", filename,
          strerror (errno));
      return NULL;
    }

    unlink (filename);
  }

  if (ftruncate (fd, size) < 0) {
    GST_ERROR_OBJECT (self, "ftruncate failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }

  mem = gst_fd_allocator_alloc (allocator, fd, size,
      GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (G_UNLIKELY (!mem)) {
    GST_ERROR_OBJECT (self, "GstFdMemory allocation failed");
    close (fd);
    return NULL;
  }

  /* we need to map the memory in order to unlink the file without losing it */
  if (!gst_memory_map (mem, &info, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (self, "GstFdMemory map failed");
    close (fd);
    return NULL;
  }

  /* unmap will not really munmap(), we just
   * need it to release the miniobject lock */
  gst_memory_unmap (mem, &info);

  return mem;
}

static void
gst_wl_shm_allocator_class_init (GstWlShmAllocatorClass * klass)
{
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_wl_shm_allocator_alloc);
}

static void
gst_wl_shm_allocator_init (GstWlShmAllocator * self)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (self);

  alloc->mem_type = GST_ALLOCATOR_WL_SHM;

  GST_OBJECT_FLAG_UNSET (self, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

void
gst_wl_shm_allocator_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GstAllocator *alloc;

    alloc = g_object_new (GST_TYPE_WL_SHM_ALLOCATOR, NULL);
    gst_object_ref_sink (alloc);
    gst_allocator_register (GST_ALLOCATOR_WL_SHM, alloc);

    g_once_init_leave (&_init, 1);
  }
}

GstAllocator *
gst_wl_shm_allocator_get (void)
{
  return gst_allocator_find (GST_ALLOCATOR_WL_SHM);
}

gboolean
gst_is_wl_shm_memory (GstMemory * mem)
{
  return gst_memory_is_type (mem, GST_ALLOCATOR_WL_SHM);
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
gst_wl_shm_memory_construct_wl_buffer (GstMemory * mem, GstWlDisplay * display,
    const GstVideoInfo * info)
{
  gint width, height, stride;
  gsize offset, size, memsize, maxsize;
  enum wl_shm_format format;
  struct wl_shm_pool *wl_pool;
  struct wl_buffer *wbuffer;

  if (!gst_wl_shm_validate_video_info (info)) {
    GST_DEBUG_OBJECT (display, "Unsupported strides and offsets.");
    return NULL;
  }

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  size = GST_VIDEO_INFO_SIZE (info);
  format = gst_video_format_to_wl_shm_format (GST_VIDEO_INFO_FORMAT (info));

  memsize = gst_memory_get_sizes (mem, &offset, &maxsize);
  offset += GST_VIDEO_INFO_PLANE_OFFSET (info, 0);

  g_return_val_if_fail (gst_is_fd_memory (mem), NULL);
  g_return_val_if_fail (size <= memsize, NULL);
  g_return_val_if_fail (gst_wl_display_check_format_for_shm (display, info),
      NULL);

  GST_DEBUG_OBJECT (display, "Creating wl_buffer from SHM of size %"
      G_GSSIZE_FORMAT " (%d x %d, stride %d), format %s", size, width, height,
      stride, gst_wl_shm_format_to_string (format));

  wl_pool = wl_shm_create_pool (gst_wl_display_get_shm (display),
      gst_fd_memory_get_fd (mem), memsize);
  wbuffer = wl_shm_pool_create_buffer (wl_pool, offset, width, height, stride,
      format);
  wl_shm_pool_destroy (wl_pool);

  return wbuffer;
}
