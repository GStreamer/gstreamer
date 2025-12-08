/* GStreamer udmabuf allocator
 *
 * Copyright (C) 2025 Collabora Ltd.
 * Author: Robert Mader <robert.mader@collabora.com>
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

/**
 * SECTION:gstudmabufallocator
 * @title: GstUdmabufAllocator
 * @short_description: Allocator for virtual memory backed dmabufs
 * @see_also: #GstMemory and #GstDmaBufAllocator
 *
 * This is a subclass of #GstDmaBufAllocator that implements the
 * gst_allocator_alloc() method using `memfd_create()` and `UDMABUF_CREATE`.
 * Platforms not supporting that (most non-Linux) will always return %NULL.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstudmabufallocator.h"

#if defined(HAVE_MEMFD_CREATE) && defined(HAVE_LINUX_UDMABUF_H)
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <linux/udmabuf.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif

struct _GstUdmabufAllocator
{
  GstDmaBufAllocator parent;

  int udmabuf_dev_fd;
};

GST_DEBUG_CATEGORY_STATIC (gst_udmabuf_allocator_debug);
GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);
#define GST_CAT_DEFAULT gst_udmabuf_allocator_debug

#define parent_class gst_udmabuf_allocator_parent_class
G_DEFINE_TYPE_WITH_CODE (GstUdmabufAllocator, gst_udmabuf_allocator,
    GST_TYPE_DMABUF_ALLOCATOR,
    GST_DEBUG_CATEGORY_INIT (gst_udmabuf_allocator_debug, "udmabuf-allocator",
        0, "udmabuf allocator");
    GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");
    );

static GstMemory *
gst_udmabuf_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
#if defined(HAVE_MEMFD_CREATE) && defined(HAVE_LINUX_UDMABUF_H)
  GstUdmabufAllocator *self = GST_UDMABUF_ALLOCATOR (allocator);
  struct udmabuf_create create;
  GstMemory *mem;
  gsize maxsize;
  int fd, ufd;

  maxsize = size;
  if (!g_size_checked_add (&maxsize, maxsize, params->prefix) ||
      !g_size_checked_add (&maxsize, maxsize, params->padding) ||
      !g_size_checked_add (&maxsize, maxsize, params->align)) {
    GST_ERROR_OBJECT (self, "Requested buffer size too big");
    return NULL;
  }
  maxsize &= ~params->align;

#ifdef HAVE_GETPAGESIZE
  gsize pagesizemask = getpagesize () - 1;
  if (!g_size_checked_add (&maxsize, maxsize, pagesizemask)) {
    GST_ERROR_OBJECT (self, "Requested buffer size too big");
    return NULL;
  }
  maxsize &= ~pagesizemask;
#endif

  fd = memfd_create ("gst-udmabuf", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    GST_ERROR_OBJECT (self, "memfd_create() failed: %s", strerror (errno));
    return NULL;
  }

  if (ftruncate (fd, maxsize) < 0) {
    GST_ERROR_OBJECT (self, "ftruncate failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }

  if (fcntl (fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) {
    GST_ERROR_OBJECT (self, "adding seals failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }

  create.memfd = fd;
  create.flags = UDMABUF_FLAGS_CLOEXEC;
  create.offset = 0;
  create.size = maxsize;

  ufd = ioctl (self->udmabuf_dev_fd, UDMABUF_CREATE, &create);
  if (ufd < 0) {
    GST_ERROR_OBJECT (self, "creating udmabuf failed: %s", strerror (errno));
    close (fd);
    return NULL;
  }
  /* The underlying memfd is kept as as a reference in the kernel. */
  close (fd);

  mem = gst_dmabuf_allocator_alloc_with_flags (allocator, ufd, maxsize,
      GST_FD_MEMORY_FLAG_KEEP_MAPPED);
  if (G_UNLIKELY (!mem)) {
    GST_ERROR_OBJECT (self, "allocation failed");
    close (ufd);
    return NULL;
  }

  mem->align = params->align;
  mem->maxsize = maxsize;
  mem->offset = params->prefix;
  mem->size = size;

  if (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED ||
      params->flags & GST_MEMORY_FLAG_ZERO_PADDED) {
    guint8 *data;
    gsize padding;

    data = mem->allocator->mem_map (mem, mem->maxsize, GST_MAP_WRITE);
    if (!data) {
      GST_ERROR_OBJECT (self, "map failed");
      gst_memory_unref (mem);
      return NULL;
    }

    if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
      memset (data, 0, params->prefix);

    padding = maxsize - (params->prefix + size);
    if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
      memset (data + params->prefix + size, 0, padding);

    mem->allocator->mem_unmap (mem);
  }

  GST_CAT_DEBUG (GST_CAT_PERFORMANCE,
      "alloc %" G_GSIZE_FORMAT " memory %p", mem->size, mem);

  return mem;
#else
  return NULL;
#endif
}

static void
gst_udmabuf_allocator_finalize (GObject * obj)
{
#if defined(HAVE_MEMFD_CREATE) && defined(HAVE_LINUX_UDMABUF_H)
  GstUdmabufAllocator *self = GST_UDMABUF_ALLOCATOR (obj);

  if (self->udmabuf_dev_fd != -1) {
    close (self->udmabuf_dev_fd);
    self->udmabuf_dev_fd = -1;
  }
#endif

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_udmabuf_allocator_class_init (GstUdmabufAllocatorClass * klass)
{
  GstAllocatorClass *alloc_class = (GstAllocatorClass *) klass;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  alloc_class->alloc = GST_DEBUG_FUNCPTR (gst_udmabuf_allocator_alloc);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_udmabuf_allocator_finalize);
}

static void
gst_udmabuf_allocator_init (GstUdmabufAllocator * self)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (self);

  alloc->mem_type = GST_ALLOCATOR_UDMABUF;

#if defined(HAVE_MEMFD_CREATE) && defined(HAVE_LINUX_UDMABUF_H)
  self->udmabuf_dev_fd = open ("/dev/udmabuf", O_RDWR | O_CLOEXEC, 0);
  if (self->udmabuf_dev_fd == -1)
    GST_WARNING_OBJECT (self,
        "Udmabuf allocator not available, can't open /dev/udmabuf: %s",
        strerror (errno));
#endif

  /* Inherited from GstFdAllocator. Unset as we implement alloc(). */
  GST_OBJECT_FLAG_UNSET (alloc, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

/**
 * gst_udmabuf_allocator_init_once:
 *
 * Register a #GstUdmabufAllocator using gst_allocator_register() with the name
 * %GST_ALLOCATOR_UDMABUF. This is no-op after the first call.
 *
 * Since: 1.28
 */
void
gst_udmabuf_allocator_init_once (void)
{
#if defined(HAVE_MEMFD_CREATE) && defined(HAVE_LINUX_UDMABUF_H)
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GstUdmabufAllocator *alloc;

    alloc =
        (GstUdmabufAllocator *) g_object_new (GST_TYPE_UDMABUF_ALLOCATOR, NULL);
    gst_object_ref_sink (alloc);

    if (alloc->udmabuf_dev_fd != -1)
      gst_allocator_register (GST_ALLOCATOR_UDMABUF, GST_ALLOCATOR (alloc));
    else
      gst_object_unref (alloc);

    g_once_init_leave (&_init, 1);
  }
#endif
}

/**
 * gst_udmabuf_allocator_get:
 *
 * Get the #GstUdmabufAllocator singleton if available.
 *
 * Returns: (transfer full) (nullable): a #GstAllocator or %NULL if
 * gst_udmabuf_allocator_init_once() did not register the allocator.
 *
 * Since: 1.28
 */
GstAllocator *
gst_udmabuf_allocator_get (void)
{
  gst_udmabuf_allocator_init_once ();
  return gst_allocator_find (GST_ALLOCATOR_UDMABUF);
}
