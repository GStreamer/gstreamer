/*
 *  gstvaapidisplay_wayland_priv.h - Internal VA/Wayland interface
 *
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DISPLAY_WAYLAND_PRIV_H
#define GST_VAAPI_DISPLAY_WAYLAND_PRIV_H

#include <gst/vaapi/gstvaapidisplay_wayland.h>

G_BEGIN_DECLS

#define GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                                 \
                                 GST_VAAPI_TYPE_DISPLAY_WAYLAND,        \
                                 GstVaapiDisplayWaylandPrivate))

#define GST_VAAPI_DISPLAY_WAYLAND_CAST(display) \
    ((GstVaapiDisplayWayland *)(display))

/**
 * GST_VAAPI_DISPLAY_WL_DISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying Wayland #wl_display object
 * of @display
 */
#undef  GST_VAAPI_DISPLAY_WL_DISPLAY
#define GST_VAAPI_DISPLAY_WL_DISPLAY(display) \
    GST_VAAPI_DISPLAY_WAYLAND_CAST(display)->priv->wl_display

struct _GstVaapiDisplayWaylandPrivate {
    gchar                      *display_name;
    struct wl_display          *wl_display;
    struct wl_compositor       *compositor;
    struct wl_shell            *shell;
    struct wl_output           *output;
    struct wl_registry         *registry;
    guint                       width;
    guint                       height;
    guint                       phys_width;
    guint                       phys_height;
    gint                        event_fd;
    guint                       create_display  : 1;
};

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_WAYLAND_PRIV_H */
