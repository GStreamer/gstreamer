/* GStreamer shared memory allocator
 *
 * Copyright (C) 2012 Intel Corporation
 * Copyright (C) 2012 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 * Copyright (C) 2023 Netflix Inc.
 *  Author: Xavier Claessens <xavier.claessens@collabora.com>
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

#include "gstshmallocator.h"

#ifdef HAVE_MMAP
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

struct _GstShmAllocator
{
  GstFdAllocator parent;
};

#define GST_CAT_DEFAULT gst_shm_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

G_DEFINE_TYPE_WITH_CODE (GstShmAllocator, gst_shm_allocator,
    GST_TYPE_FD_ALLOCATOR,
    GST_DEBUG_CATEGORY_INIT (gst_shm_debug, "shmallocator", 0,
        "Shared memory allocator");
    );

static GstMemory *
gst_shm_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
#ifdef HAVE_MMAP
  GstShmAllocator *self = GST_SHM_ALLOCATOR (allocator);
  int fd;
  GstMemory *mem;
  GstMapInfo info;

  /* TODO: make use of the allocation params, if necessary */

#ifdef HAVE_MEMFD_CREATE
  fd = memfd_create ("gst-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
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
    char filename[1024];
    static int init = 0;

    /* allocate shm pool */
    snprintf (filename, 1024, "%s/%s-%d-%s", g_get_user_runtime_dir (),
        "gst-shm", init++, "XXXXXX");

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
#else
  return NULL;
#endif
}

static void
gst_shm_allocator_class_init (GstShmAllocatorClass * klass)
{
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_shm_allocator_alloc);
}

static void
gst_shm_allocator_init (GstShmAllocator * self)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (self);

  alloc->mem_type = GST_ALLOCATOR_SHM;

  GST_OBJECT_FLAG_SET (self, GST_ALLOCATOR_FLAG_NO_COPY);
}

void
gst_shm_allocator_init_once (void)
{
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GstAllocator *alloc;

    alloc = (GstAllocator *) g_object_new (GST_TYPE_SHM_ALLOCATOR, NULL);
    gst_object_ref_sink (alloc);
    gst_allocator_register (GST_ALLOCATOR_SHM, alloc);

    g_once_init_leave (&_init, 1);
  }
}

GstAllocator *
gst_shm_allocator_get (void)
{
  return gst_allocator_find (GST_ALLOCATOR_SHM);
}
