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

#include <glib-object.h>
#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_SURFACE_PROXY \
    (gst_vaapi_surface_proxy_get_type())

#define GST_VAAPI_SURFACE_PROXY(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_SURFACE_PROXY,   \
                                GstVaapiSurfaceProxy))

#define GST_VAAPI_SURFACE_PROXY_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_SURFACE_PROXY,      \
                             GstVaapiSurfaceProxyClass))

#define GST_VAAPI_IS_SURFACE_PROXY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SURFACE_PROXY))

#define GST_VAAPI_IS_SURFACE_PROXY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SURFACE_PROXY))

#define GST_VAAPI_SURFACE_PROXY_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_SURFACE_PROXY,    \
                               GstVaapiSurfaceProxyClass))

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

typedef struct _GstVaapiSurfaceProxy            GstVaapiSurfaceProxy;
typedef struct _GstVaapiSurfaceProxyPrivate     GstVaapiSurfaceProxyPrivate;
typedef struct _GstVaapiSurfaceProxyClass       GstVaapiSurfaceProxyClass;

/**
 * GstVaapiSurfaceProxy:
 *
 * A wrapper around a VA surface and context.
 */
struct _GstVaapiSurfaceProxy {
    /*< private >*/
    GObject parent_instance;

    GstVaapiSurfaceProxyPrivate *priv;
};

/**
 * GstVaapiSurfaceProxyClass:
 *
 * A wrapper around a VA surface and context.
 */
struct _GstVaapiSurfaceProxyClass {
    /*< private >*/
    GObjectClass parent_class;
};

GType
gst_vaapi_surface_proxy_get_type(void) G_GNUC_CONST;

GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new(GstVaapiContext *context, GstVaapiSurface *surface);

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
