/* GStreamer Wayland buffer pool
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

#ifndef __GST_WAYLAND_BUFFER_POOL_H__
#define __GST_WAYLAND_BUFFER_POOL_H__

#include <gst/video/video.h>

#include "wldisplay.h"

G_BEGIN_DECLS

#define GST_TYPE_WAYLAND_BUFFER_POOL      (gst_wayland_buffer_pool_get_type())
#define GST_IS_WAYLAND_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WAYLAND_BUFFER_POOL))
#define GST_WAYLAND_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WAYLAND_BUFFER_POOL, GstWaylandBufferPool))
#define GST_WAYLAND_BUFFER_POOL_CAST(obj) ((GstWaylandBufferPool*)(obj))

typedef struct _GstWaylandBufferPool GstWaylandBufferPool;
typedef struct _GstWaylandBufferPoolClass GstWaylandBufferPoolClass;

/* buffer pool */
struct _GstWaylandBufferPool
{
  GstBufferPool bufferpool;
  GstWlDisplay *display;

  /* external configuration */
  GstVideoInfo info;

  /* allocation data */
  struct wl_shm_pool *wl_pool;
  size_t size;
  size_t used;
  void *data;
};

struct _GstWaylandBufferPoolClass
{
  GstBufferPoolClass parent_class;
};

GType gst_wayland_buffer_pool_get_type (void);

GstBufferPool *gst_wayland_buffer_pool_new (GstWlDisplay * display);

G_END_DECLS

#endif /*__GST_WAYLAND_BUFFER_POOL_H__*/
