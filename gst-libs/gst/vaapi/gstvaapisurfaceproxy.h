/*
 *  gstvaapisurfaceproxy.h - VA surface proxy
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_SURFACE_PROXY_H
#define GST_VAAPI_SURFACE_PROXY_H

#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

typedef struct _GstVaapiSurfaceProxy            GstVaapiSurfaceProxy;

/**
 * GST_VAAPI_SURFACE_PROXY_SURFACE:
 * @surface: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the #GstVaapiSurface of @surface.
 */
#define GST_VAAPI_SURFACE_PROXY_SURFACE(surface) \
    gst_vaapi_surface_proxy_get_surface(surface)

GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new(GstVaapiContext *context, GstVaapiSurface *surface);

GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_ref(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_unref(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_replace(GstVaapiSurfaceProxy **old_proxy_ptr,
    GstVaapiSurfaceProxy *new_proxy);

gpointer
gst_vaapi_surface_proxy_get_user_data(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_user_data(GstVaapiSurfaceProxy *proxy,
    gpointer user_data, GDestroyNotify destroy_notify);

GstVaapiContext *
gst_vaapi_surface_proxy_get_context(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_context(
    GstVaapiSurfaceProxy *proxy,
    GstVaapiContext      *context
);

GstVaapiSurface *
gst_vaapi_surface_proxy_get_surface(GstVaapiSurfaceProxy *proxy);

GstVaapiID
gst_vaapi_surface_proxy_get_surface_id(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_surface(
    GstVaapiSurfaceProxy *proxy,
    GstVaapiSurface      *surface
);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_PROXY_H */
