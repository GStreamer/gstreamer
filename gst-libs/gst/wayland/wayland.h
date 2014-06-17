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

#ifndef GST_USE_UNSTABLE_API
#warning "The GStreamer wayland library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <wayland-client.h>

G_BEGIN_DECLS

/* The type of GstContext used to pass the wl_display pointer
 * from the application to the sink */
#define GST_WAYLAND_DISPLAY_HANDLE_CONTEXT_TYPE "GstWaylandDisplayHandleContextType"

gboolean gst_is_wayland_display_handle_need_context_message (GstMessage * msg);
GstContext *
gst_wayland_display_handle_context_new (struct wl_display * display);
struct wl_display *
gst_wayland_display_handle_context_get_handle (GstContext * context);


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
  void (*begin_geometry_change)    (GstWaylandVideo *video);
  void (*end_geometry_change)     (GstWaylandVideo *video);
};

GType   gst_wayland_video_get_type (void);

/* virtual function wrappers */
void gst_wayland_video_begin_geometry_change (GstWaylandVideo * video);
void gst_wayland_video_end_geometry_change (GstWaylandVideo * video);

G_END_DECLS

#endif /* __GST_WAYLAND_H__ */
