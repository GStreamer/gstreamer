/*
 * GStreamer Wayland Library
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

#ifndef __GST_WAYLAND_H__
#define __GST_WAYLAND_H__

#include <gst/gst.h>
#include <wayland-client.h>

G_BEGIN_DECLS

/**
 * GstWaylandWindowHandle:
 *
 * Window handle structure to pass to the GstVideoOverlay set_window_handle
 * method.
 */
typedef struct _GstWaylandWindowHandle GstWaylandWindowHandle;

struct _GstWaylandWindowHandle {
  struct wl_display *display;
  struct wl_surface *surface;
  gint width;
  gint height;
};


#define GST_TYPE_WAYLAND_VIDEO \
    (gst_wayland_video_get_type ())
#define GST_WAYLAND_VIDEO(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WAYLAND_VIDEO, GstWaylandVideo))
#define GST_IS_WAYLAND_VIDEO(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WAYLAND_VIDEO))
#define GST_WAYLAND_VIDEO_GET_INTERFACE(inst) \
    (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_WAYLAND_VIDEO, GstWaylandVideoInterface))

/**
 * GstWaylandVideo:
 *
 * Opaque #GstWaylandVideo interface structure
 */
typedef struct _GstWaylandVideo GstWaylandVideo;
typedef struct _GstWaylandVideoInterface GstWaylandVideoInterface;


/**
 * GstWaylandVideoInterface:
 * @iface: parent interface type.
 *
 * #GstWaylandVideo interface
 */
struct _GstWaylandVideoInterface {
  GTypeInterface iface;

  /* virtual functions */
  void (*set_surface_size)     (GstWaylandVideo *video, gint w, gint h);
  void (*pause_rendering)      (GstWaylandVideo *video);
  void (*resume_rendering)     (GstWaylandVideo *video);
};

GType   gst_wayland_video_get_type (void);

/* virtual function wrappers */

void gst_wayland_video_set_surface_size (GstWaylandVideo * video,
        gint w, gint h);

void gst_wayland_video_pause_rendering (GstWaylandVideo * video);
void gst_wayland_video_resume_rendering (GstWaylandVideo * video);

G_END_DECLS

#endif /* __GST_WAYLAND_H__ */
