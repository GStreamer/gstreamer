/*
 * GStreamer Wayland video sink
 * Copyright (C) 2011 Intel Corporation
 * Copyright (C) 2011 Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#ifndef __GST_WAYLAND_VIDEO_SINK_H__
#define __GST_WAYLAND_VIDEO_SINK_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>

#include <wayland-client.h>

#define GST_TYPE_WAYLAND_SINK \
	    (gst_wayland_sink_get_type())
#define GST_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAYLAND_SINK,GstWaylandSink))
#define GST_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAYLAND_SINK,GstWaylandSinkClass))
#define GST_IS_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAYLAND_SINK))
#define GST_IS_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAYLAND_SINK))
#define GST_WAYLAND_SINK_GET_CLASS(inst) \
        (G_TYPE_INSTANCE_GET_CLASS ((inst), GST_TYPE_WAYLAND_SINK, GstWaylandSinkClass))

struct  display
{
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct wl_shell *shell;
  struct wl_shm *shm;
  uint32_t formats;
};

struct window
{
  struct display *display;
  int width, height;
  struct wl_surface *surface;
  struct wl_shell_surface *shell_surface;
  struct wl_buffer *buffer;
  struct wl_callback *callback;
  guint redraw_pending :1;

};

struct shm_pool {
  struct wl_shm_pool *pool;
  size_t size;
  size_t used;
  void *data;
};

typedef struct _GstWaylandSink GstWaylandSink;
typedef struct _GstWaylandSinkClass GstWaylandSinkClass;

#include "waylandpool.h"

struct _GstWaylandSink
{
  GstVideoSink parent;

  struct display *display;
  struct window *window;
  struct shm_pool *shm_pool;

  GstBufferPool *pool;

  GMutex wayland_lock;

  gint video_width;
  gint video_height;
  uint32_t format;
};

struct _GstWaylandSinkClass
{
  GstVideoSinkClass parent;
};

GType gst_wayland_sink_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __GST_WAYLAND_VIDEO_SINK_H__ */
