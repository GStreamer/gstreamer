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
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_WAYLAND_VIDEO_SINK_H__
#define __GST_WAYLAND_VIDEO_SINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>

#include <wayland-client.h>
#include <wayland-egl.h>

#define GST_TYPE_WAYLAND_SINK \
	    (gst_wayland_sink_get_type())
#define GST_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_WAYLAND_SINK,GstWayLandSink))
#define GST_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_WAYLAND_SINK,GstWayLandSinkClass))
#define GST_IS_WAYLAND_SINK(obj) \
	    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_WAYLAND_SINK))
#define GST_IS_WAYLAND_SINK_CLASS(klass) \
	    (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_WAYLAND_SINK))
#define GST_WAYLAND_SINK_GET_CLASS(inst) \
        (G_TYPE_INSTANCE_GET_CLASS ((inst), GST_TYPE_WAYLAND_SINK, GstWayLandSinkClass))

struct  display
{
  struct wl_display *display;
  struct wl_visual *xrgb_visual;
  struct wl_compositor *compositor;
  struct wl_shell *shell;
  struct wl_shm *shm;
  uint32_t mask;
};

struct window
{
  struct display *display;
  int width, height;
  struct wl_surface *surface;
  struct wl_buffer *buffer;
  void *data;
};

typedef struct _GstWayLandSink GstWayLandSink;
typedef struct _GstWayLandSinkClass GstWayLandSinkClass;

struct _GstWayLandSink
{

  GstVideoSink parent;

  GstCaps *caps;
  
  struct display *display;
  struct window *window;

  GCond *buffer_cond;
  GMutex *buffer_lock;

  GCond *wayland_cond;
  GMutex *wayland_lock;

  gboolean unlock;

  guint width, height, depth, size;

  void *MapAddr;
  gboolean render_finish;
      
};

struct _GstWayLandSinkClass
{
  GstVideoSinkClass parent; 

};

GType
gst_wayland_sink_get_type (void)
    G_GNUC_CONST;

G_END_DECLS
#endif /* __GST_WAYLAND_VIDEO_SINK_H__ */
