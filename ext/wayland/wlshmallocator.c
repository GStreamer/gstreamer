/* GStreamer Wayland video sink
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

#include "wlshmallocator.h"
#include "wlvideoformat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

GST_DEBUG_CATEGORY_EXTERN (gstwayland_debug);
#define GST_CAT_DEFAULT gstwayland_debug

G_DEFINE_TYPE (GstWlShmAllocator, gst_wl_shm_allocator, GST_TYPE_ALLOCATOR);

static GstMemory *
gst_wl_shm_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GstWlShmAllocator *self = GST_WL_SHM_ALLOCATOR (allocator);
  char filename[1024];
  static int init = 0;
  int fd;
  gpointer data;
  GstWlShmMemory *mem;

  /* TODO: make use of the allocation params, if necessary */

  /* allocate shm pool */
  snprintf (filename, 1024, "%s/%s-%d-%s", g_get_user_runtime_dir (),
      "wayland-shm", init++, "XXXXXX");

  fd = g_mkstemp (filename);
  if (fd < 0) {
    GST_ERROR_OBJECT (self, "opening temp file %s failed: %s", filename,
        strerror (errno));
    return NULL;
  }
  if (ftruncate (fd, size) < 0) {
    GST_ERROR_OBJECT (self, "ftruncate failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }

  data = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    GST_ERROR_OBJECT (self, "mmap failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }

  unlink (filename);

  mem = g_slice_new0 (GstWlShmMemory);
  gst_memory_init ((GstMemory *) mem, GST_MEMORY_FLAG_NO_SHARE, allocator, NULL,
      size, 0, 0, size);
  mem->data = data;
  mem->fd = fd;

  return (GstMemory *) mem;
}

static void
gst_wl_shm_allocator_free (GstAllocator * allocator, GstMemory * memory)
{
  GstWlShmMemory *shm_mem = (GstWlShmMemory *) memory;

  if (shm_mem->fd != -1)
    close (shm_mem->fd);
  munmap (shm_mem->data, memory->maxsize);

  g_slice_free (GstWlShmMemory, shm_mem);
}

static gpointer
gst_wl_shm_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  return ((GstWlShmMemory *) mem)->data;
}

static void
gst_wl_shm_mem_unmap (GstMemory * mem)
{
}

static void
gst_wl_shm_allocator_class_init (GstWlShmAllocatorClass * klass)
{
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_wl_shm_allocator_alloc);
  alloc_class->free = GST_DEBUG_FUNCPTR (gst_wl_shm_allocator_free);
}

static void
gst_wl_shm_allocator_init (GstWlShmAllocator * self)
{
  self->parent_instance.mem_type = GST_ALLOCATOR_WL_SHM;
  self->parent_instance.mem_map = gst_wl_shm_mem_map;
  self->parent_instance.mem_unmap = gst_wl_shm_mem_unmap;

  GST_OBJECT_FLAG_SET (self, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

void
gst_wl_shm_allocator_register (void)
{
  gst_allocator_register (GST_ALLOCATOR_WL_SHM,
      g_object_new (GST_TYPE_WL_SHM_ALLOCATOR, NULL));
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

struct wl_buffer *
gst_wl_shm_memory_construct_wl_buffer (GstMemory * mem, GstWlDisplay * display,
    const GstVideoInfo * info)
{
  GstWlShmMemory *shm_mem = (GstWlShmMemory *) mem;
  gint width, height, stride;
  gsize size;
  enum wl_shm_format format;
  struct wl_shm_pool *wl_pool;
  struct wl_buffer *wbuffer;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);
  stride = GST_VIDEO_INFO_PLANE_STRIDE (info, 0);
  size = GST_VIDEO_INFO_SIZE (info);
  format = gst_video_format_to_wl_shm_format (GST_VIDEO_INFO_FORMAT (info));

  g_return_val_if_fail (gst_is_wl_shm_memory (mem), NULL);
  g_return_val_if_fail (size <= mem->size, NULL);
  g_return_val_if_fail (shm_mem->fd != -1, NULL);

  GST_DEBUG_OBJECT (mem->allocator, "Creating wl_buffer of size %"
      G_GSSIZE_FORMAT " (%d x %d, stride %d), format %s", size, width, height,
      stride, gst_wl_shm_format_to_string (format));

  wl_pool = wl_shm_create_pool (display->shm, shm_mem->fd, mem->size);
  wbuffer = wl_shm_pool_create_buffer (wl_pool, 0, width, height, stride,
      format);

  close (shm_mem->fd);
  shm_mem->fd = -1;
  wl_shm_pool_destroy (wl_pool);

  return wbuffer;
}
