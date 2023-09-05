/*
 *  gstvaapidisplay_wayland_priv.h - Internal VA/Wayland interface
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

#include <gst/vaapi/gstvaapidisplay_wayland.h>
#include "gstvaapidisplay_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_IS_DISPLAY_WAYLAND(display) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((display), GST_TYPE_VAAPI_DISPLAY_WAYLAND))

#define GST_VAAPI_DISPLAY_WAYLAND_CAST(display) \
    ((GstVaapiDisplayWayland *)(display))

#define GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(display) \
    (GST_VAAPI_DISPLAY_WAYLAND_CAST(display)->priv)

#define GST_VAAPI_DISPLAY_WAYLAND_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DISPLAY_WAYLAND, GstVaapiDisplayWaylandClass))

typedef struct _GstVaapiDisplayWaylandPrivate   GstVaapiDisplayWaylandPrivate;
typedef struct _GstVaapiDisplayWaylandClass     GstVaapiDisplayWaylandClass;

/**
 * GST_VAAPI_DISPLAY_WL_DISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying Wayland #wl_display object
 * of @display
 */
#undef  GST_VAAPI_DISPLAY_WL_DISPLAY
#define GST_VAAPI_DISPLAY_WL_DISPLAY(display) \
    GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(display)->wl_display

typedef struct _GstDRMFormat GstDRMFormat;

struct _GstDRMFormat {
  guint format;
  guint64 modifier;
};

struct _GstVaapiDisplayWaylandPrivate
{
  gchar *display_name;
  struct wl_display *wl_display;
  struct wl_compositor *compositor;
  struct wl_shell *wl_shell;
  struct xdg_wm_base *xdg_wm_base;
  struct wl_subcompositor *subcompositor;
  struct wl_output *output;
  struct zwp_linux_dmabuf_v1 *dmabuf;
  struct wl_registry *registry;
  GArray *dmabuf_formats;
  GMutex dmabuf_formats_lock;
  guint width;
  guint height;
  guint phys_width;
  guint phys_height;
  gint event_fd;
  guint use_foreign_display:1;
};

/**
 * GstVaapiDisplayWayland:
 *
 * VA/Wayland display wrapper.
 */
struct _GstVaapiDisplayWayland
{
  /*< private >*/
  GstVaapiDisplay parent_instance;

  GstVaapiDisplayWaylandPrivate *priv;
};

/**
 * GstVaapiDisplayWaylandClass:
 *
 * VA/Wayland display wrapper clas.
 */
struct _GstVaapiDisplayWaylandClass
{
  /*< private >*/
  GstVaapiDisplayClass parent_class;
};

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_WAYLAND_PRIV_H */
