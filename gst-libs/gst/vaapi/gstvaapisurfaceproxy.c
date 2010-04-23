/*
 *  gstvaapisurfaceproxy.c - VA surface proxy
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

/**
 * SECTION:gstvaapisurfaceproxy
 * @short_description: VA surface proxy
 */

#include "config.h"
#include "gstvaapisurfaceproxy.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSurfaceProxy, gst_vaapi_surface_proxy, G_TYPE_OBJECT);

enum {
    PROP_0,

    PROP_CONTEXT,
    PROP_SURFACE
};

static void
gst_vaapi_surface_proxy_finalize(GObject *object)
{
    GstVaapiSurfaceProxy * const proxy = GST_VAAPI_SURFACE_PROXY(object);

    if (proxy->surface) {
        if (proxy->context)
            gst_vaapi_context_put_surface(proxy->context, proxy->surface);
        g_object_unref(proxy->surface);
        proxy->surface = NULL;
    }

    if (proxy->context) {
        g_object_unref(proxy->context);
        proxy->context = NULL;
    }

    G_OBJECT_CLASS(gst_vaapi_surface_proxy_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_proxy_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSurfaceProxy * const proxy = GST_VAAPI_SURFACE_PROXY(object);

    switch (prop_id) {
    case PROP_CONTEXT:
        gst_vaapi_surface_proxy_set_context(proxy, g_value_get_pointer(value));
        break;
    case PROP_SURFACE:
        gst_vaapi_surface_proxy_set_surface(proxy, g_value_get_pointer(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_proxy_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSurfaceProxy * const proxy = GST_VAAPI_SURFACE_PROXY(object);

    switch (prop_id) {
    case PROP_CONTEXT:
        g_value_set_pointer(value, gst_vaapi_surface_proxy_get_context(proxy));
        break;
    case PROP_SURFACE:
        g_value_set_pointer(value, gst_vaapi_surface_proxy_get_surface(proxy));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_proxy_class_init(GstVaapiSurfaceProxyClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    object_class->finalize     = gst_vaapi_surface_proxy_finalize;
    object_class->set_property = gst_vaapi_surface_proxy_set_property;
    object_class->get_property = gst_vaapi_surface_proxy_get_property;

    g_object_class_install_property
        (object_class,
         PROP_CONTEXT,
         g_param_spec_pointer("context",
                              "Context",
                              "The context stored in the proxy",
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_SURFACE,
         g_param_spec_pointer("surface",
                              "Surface",
                              "The surface stored in the proxy",
                              G_PARAM_READWRITE));
}

static void
gst_vaapi_surface_proxy_init(GstVaapiSurfaceProxy *proxy)
{
    proxy->context = NULL;
    proxy->surface = NULL;
}

/**
 * gst_vaapi_surface_proxy_new:
 * @context: a #GstVaapiContext
 * @surface: a #GstVaapiSurface
 *
 * Creates a new #GstVaapiSurfaceProxy with the specified context and
 * surface.
 *
 * Return value: the newly allocated #GstVaapiSurfaceProxy object
 */
GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new(GstVaapiContext *context, GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    return g_object_new(GST_VAAPI_TYPE_SURFACE_PROXY,
                        "context", context,
                        "surface", surface,
                        NULL);
}

/**
 * gst_vaapi_surface_proxy_get_context:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the #GstVaapiContext stored in the @proxy.
 *
 * Return value: the #GstVaapiContext
 */
GstVaapiContext *
gst_vaapi_surface_proxy_get_context(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), NULL);

    return proxy->context;
}

/**
 * gst_vaapi_surface_proxy_set_context:
 * @proxy: a #GstVaapiSurfaceProxy
 * @context: the new #GstVaapiContext to be stored in @proxy
 *
 * Stores a new @context into the @proxy. The proxy releases the
 * previous reference, if any, and then holds a reference to the new
 * @context.
 */
void
gst_vaapi_surface_proxy_set_context(
    GstVaapiSurfaceProxy *proxy,
    GstVaapiContext      *context
)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));
    g_return_if_fail(GST_VAAPI_IS_CONTEXT(context));

    if (proxy->context) {
        g_object_unref(proxy->context);
        proxy->context = NULL;
    }

    if (context)
        proxy->context = g_object_ref(context);
}

/**
 * gst_vaapi_surface_proxy_get_surface:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the #GstVaapiSurface stored in the @proxy.
 *
 * Return value: the #GstVaapiSurface
 */
GstVaapiSurface *
gst_vaapi_surface_proxy_get_surface(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), NULL);

    return proxy->surface;
}

/**
 * gst_vaapi_surface_proxy_set_surface:
 * @proxy: a #GstVaapiSurfaceProxy
 * @surface: the new #GstVaapiSurface to be stored in @proxy
 *
 * Stores a new @surface into the @proxy. The proxy releases the
 * previous reference, if any, and then holds a reference to the new
 * @surface.
 */
void
gst_vaapi_surface_proxy_set_surface(
    GstVaapiSurfaceProxy *proxy,
    GstVaapiSurface      *surface
)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    if (proxy->surface) {
        g_object_unref(proxy->surface);
        proxy->surface = NULL;
    }

    if (surface)
        proxy->surface = g_object_ref(surface);
}
