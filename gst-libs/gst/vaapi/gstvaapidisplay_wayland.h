/*
 *  gstvaapidisplay_wayland.h - VA/Wayland display abstraction
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

#ifndef GST_VAAPI_DISPLAY_WAYLAND_H
#define GST_VAAPI_DISPLAY_WAYLAND_H

#include <va/va_wayland.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_DISPLAY_WAYLAND          (gst_vaapi_display_wayland_get_type ())
#define GST_VAAPI_DISPLAY_WAYLAND(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_DISPLAY_WAYLAND, GstVaapiDisplayWayland))

typedef struct _GstVaapiDisplayWayland          GstVaapiDisplayWayland;

GstVaapiDisplay *
gst_vaapi_display_wayland_new (const gchar * display_name);

GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_display (struct wl_display * wl_display);

GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_va_display (VADisplay va_display,
    struct wl_display * wl_display);

struct wl_display *
gst_vaapi_display_wayland_get_display (GstVaapiDisplayWayland * display);

GType
gst_vaapi_display_wayland_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiDisplayWayland, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_WAYLAND_H */
