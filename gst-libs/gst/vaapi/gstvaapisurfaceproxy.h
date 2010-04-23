/*
 *  gstvaapisurfaceproxy.h - VA surface proxy
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
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

typedef struct _GstVaapiSurfaceProxy            GstVaapiSurfaceProxy;
typedef struct _GstVaapiSurfaceProxyClass       GstVaapiSurfaceProxyClass;

/**
 * GstVaapiSurfaceProxy:
 * @surface: a #GstVaapiSurface
 * @context: the #GstVaapiContext to which the @surface is bound
 *
 * A wrapper around a VA surface and context.
 */
struct _GstVaapiSurfaceProxy {
    /*< private >*/
    GObject parent_instance;

    GstVaapiContext *context;
    GstVaapiSurface *surface;
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
gst_vaapi_surface_proxy_get_type(void);

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

void
gst_vaapi_surface_proxy_set_surface(
    GstVaapiSurfaceProxy *proxy,
    GstVaapiSurface      *surface
);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_PROXY_H */
