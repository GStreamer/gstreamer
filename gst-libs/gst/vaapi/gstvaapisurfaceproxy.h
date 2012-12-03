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

/**
 * GST_VAAPI_SURFACE_PROXY_TIMESTAMP:
 * @surface: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the @surface timestamp, or
 * %GST_CLOCK_TIME_NONE if none was set.
 */
#define GST_VAAPI_SURFACE_PROXY_TIMESTAMP(surface) \
    gst_vaapi_surface_proxy_get_timestamp(surface)

/**
 * GST_VAAPI_SURFACE_PROXY_DURATION:
 * @surface: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the amount of time the @surface should be
 * displayed, or %GST_CLOCK_TIME_NONE if none was set.
 */
#define GST_VAAPI_SURFACE_PROXY_DURATION(surface) \
    gst_vaapi_surface_proxy_get_duration(surface)

/**
 * GST_VAAPI_SURFACE_PROXY_INTERLACED:
 * @surface: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to %TRUE if the @surface is interlaced.
 */
#define GST_VAAPI_SURFACE_PROXY_INTERLACED(surface) \
    gst_vaapi_surface_proxy_get_interlaced(surface)

/**
 * GST_VAAPI_SURFACE_PROXY_TFF:
 * @surface: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the tff flag of the @surface
 */
#define GST_VAAPI_SURFACE_PROXY_TFF(surface) \
    gst_vaapi_surface_proxy_get_tff(surface)

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

GstClockTime
gst_vaapi_surface_proxy_get_timestamp(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_timestamp(
    GstVaapiSurfaceProxy *proxy,
    GstClockTime          timestamp
);

GstClockTime
gst_vaapi_surface_proxy_get_duration(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_duration(
    GstVaapiSurfaceProxy *proxy,
    GstClockTime          duration
);

gboolean
gst_vaapi_surface_proxy_get_interlaced(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_interlaced(GstVaapiSurfaceProxy *proxy, gboolean b);

gboolean
gst_vaapi_surface_proxy_get_tff(GstVaapiSurfaceProxy *proxy);

void
gst_vaapi_surface_proxy_set_tff(GstVaapiSurfaceProxy *proxy, gboolean tff);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_PROXY_H */
