/*
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define GST_USE_UNSTABLE_API
#include <gst/allocators/gstahardwarebuffer.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include "gstamcaimagememory.h"

typedef struct _GstAmcAImageMemory GstAmcAImageMemory;
typedef struct _GstAmcAImageAllocator GstAmcAImageAllocator;
typedef struct _GstAmcAImageAllocatorClass GstAmcAImageAllocatorClass;

struct _GstAmcAImageMemory
{
  GstMemory parent;

  GstAmcAImageReader *reader;
  GstAmcAImage *image;
  AHardwareBuffer *buffer;
  gint acquire_fence_fd;
};

struct _GstAmcAImageAllocator
{
  GstAllocator parent;
};

struct _GstAmcAImageAllocatorClass
{
  GstAllocatorClass parent_class;
};

#define GST_TYPE_AMC_AIMAGE_ALLOCATOR (gst_amc_aimage_allocator_get_type ())

GType gst_amc_aimage_allocator_get_type (void);

G_DEFINE_TYPE (GstAmcAImageAllocator, gst_amc_aimage_allocator,
    GST_TYPE_ALLOCATOR);

static GstAllocator *gst_amc_aimage_allocator;

/* This is a temporary synchronous wait before publishing the AHardwareBuffer.
 * Keep it bounded so an unexpected fence problem does not stall the decoder
 * indefinitely, while still allowing for a few delayed frames. */
#define GST_AMC_AIMAGE_ACQUIRE_FENCE_TIMEOUT_MS 100

static gboolean
gst_amc_aimage_memory_query (GstMemory * gmem, AHardwareBuffer ** buffer)
{
  GstAmcAImageMemory *mem = (GstAmcAImageMemory *) gmem;

  if (buffer)
    *buffer = mem->buffer;

  return mem->buffer != NULL;
}

static void
gst_amc_aimage_memory_init_once (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once)) {
    gst_amc_aimage_allocator =
        g_object_new (GST_TYPE_AMC_AIMAGE_ALLOCATOR, NULL);
    gst_object_ref_sink (gst_amc_aimage_allocator);
    gst_ahardware_buffer_memory_register_query_function
        (GST_TYPE_AMC_AIMAGE_ALLOCATOR, gst_amc_aimage_memory_query);
    g_once_init_leave (&once, 1);
  }
}

static gboolean
gst_amc_aimage_memory_wait_acquire_fence (gint acquire_fence_fd)
{
  struct pollfd pollfd = {
    .fd = acquire_fence_fd,
    .events = POLLIN,
  };
  gint64 end_time;

  if (acquire_fence_fd < 0)
    return TRUE;

  end_time = g_get_monotonic_time () +
      GST_AMC_AIMAGE_ACQUIRE_FENCE_TIMEOUT_MS * G_TIME_SPAN_MILLISECOND;

  while (TRUE) {
    gint64 now = g_get_monotonic_time ();
    gint timeout;
    gint ret;

    if (now >= end_time)
      break;

    timeout = gst_util_uint64_scale_int_ceil (end_time - now, 1,
        G_TIME_SPAN_MILLISECOND);

    pollfd.revents = 0;
    ret = poll (&pollfd, 1, timeout);
    if (ret > 0) {
      if (pollfd.revents & POLLNVAL) {
        GST_WARNING ("Invalid AImage acquire fence fd");
        return FALSE;
      }
      return TRUE;
    }

    if (ret == 0)
      break;

    if (errno == EINTR)
      continue;

    GST_WARNING ("Failed to wait for AImage acquire fence: %s",
        g_strerror (errno));
    return FALSE;
  }

  GST_WARNING ("Timed out waiting %d ms for AImage acquire fence",
      GST_AMC_AIMAGE_ACQUIRE_FENCE_TIMEOUT_MS);

  return FALSE;
}

GstMemory *
gst_amc_aimage_memory_new (GstAmcAImageReader * reader, GstAmcAImage * image,
    AHardwareBuffer * buffer, gint acquire_fence_fd, gsize size)
{
  GstAmcAImageMemory *mem;

  g_return_val_if_fail (reader != NULL, NULL);
  g_return_val_if_fail (image != NULL, NULL);
  g_return_val_if_fail (buffer != NULL, NULL);

  gst_amc_aimage_memory_init_once ();

  /* Temporary conservative synchronization: block before publishing the AHB.
   * A later implementation should import or propagate fences instead. */
  if (!gst_amc_aimage_memory_wait_acquire_fence (acquire_fence_fd))
    return NULL;

  if (acquire_fence_fd >= 0) {
    close (acquire_fence_fd);
    acquire_fence_fd = -1;
  }

  mem = g_new0 (GstAmcAImageMemory, 1);
  mem->reader = gst_amc_image_reader_ref (reader);
  mem->image = image;
  mem->buffer = buffer;
  mem->acquire_fence_fd = acquire_fence_fd;

  gst_memory_init (GST_MEMORY_CAST (mem),
      GST_MEMORY_FLAG_READONLY | GST_MEMORY_FLAG_NOT_MAPPABLE,
      gst_amc_aimage_allocator, NULL, size, 0, 0, size);

  return GST_MEMORY_CAST (mem);
}

void
gst_amc_aimage_memory_release_image (GstAmcAImage * image,
    gint acquire_fence_fd)
{
  if (acquire_fence_fd >= 0)
    close (acquire_fence_fd);
  if (image)
    gst_amc_image_delete_async (image, -1);
}

static void
gst_amc_aimage_allocator_free (GstAllocator * allocator, GstMemory * gmem)
{
  GstAmcAImageMemory *mem = (GstAmcAImageMemory *) gmem;

  g_return_if_fail (GST_IS_ALLOCATOR (allocator));
  g_return_if_fail (gmem != NULL);
  g_return_if_fail (gmem->allocator == allocator);

  gst_amc_aimage_memory_release_image (mem->image, mem->acquire_fence_fd);
  gst_amc_image_reader_notify_image_released (mem->reader);
  gst_amc_image_reader_unref (mem->reader);

  g_free (mem);
}

static void
gst_amc_aimage_allocator_class_init (GstAmcAImageAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->alloc = NULL;
  allocator_class->free = gst_amc_aimage_allocator_free;
}

static void
gst_amc_aimage_allocator_init (GstAmcAImageAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = "GstAmcAImageMemory";
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}
