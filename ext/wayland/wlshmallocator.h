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

#ifndef __GST_WL_SHM_ALLOCATOR_H__
#define __GST_WL_SHM_ALLOCATOR_H__

#include <gst/video/video.h>
#include <gst/allocators/allocators.h>
#include <wayland-client-protocol.h>
#include "wldisplay.h"

G_BEGIN_DECLS

#define GST_TYPE_WL_SHM_ALLOCATOR                  (gst_wl_shm_allocator_get_type ())
#define GST_WL_SHM_ALLOCATOR(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WL_SHM_ALLOCATOR, GstWlShmAllocator))
#define GST_IS_WL_SHM_ALLOCATOR(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WL_SHM_ALLOCATOR))
#define GST_WL_SHM_ALLOCATOR_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WL_SHM_ALLOCATOR, GstWlShmAllocatorClass))
#define GST_IS_WL_SHM_ALLOCATOR_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_WL_SHM_ALLOCATOR))
#define GST_WL_SHM_ALLOCATOR_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_WL_SHM_ALLOCATOR, GstWlShmAllocatorClass))

#define GST_ALLOCATOR_WL_SHM "wl_shm"

typedef struct _GstWlShmAllocator GstWlShmAllocator;
typedef struct _GstWlShmAllocatorClass GstWlShmAllocatorClass;

struct _GstWlShmAllocator
{
  GstFdAllocator parent_instance;
};

struct _GstWlShmAllocatorClass
{
  GstFdAllocatorClass parent_class;
};

GType gst_wl_shm_allocator_get_type (void);

void gst_wl_shm_allocator_register (void);
GstAllocator * gst_wl_shm_allocator_get (void);

gboolean gst_is_wl_shm_memory (GstMemory * mem);
struct wl_buffer * gst_wl_shm_memory_construct_wl_buffer (GstMemory * mem,
    GstWlDisplay * display, const GstVideoInfo * info);

G_END_DECLS

#endif /* __GST_WL_SHM_ALLOCATOR_H__ */
