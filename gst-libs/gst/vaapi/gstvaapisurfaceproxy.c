/*
 *  gstvaapisurfaceproxy.c - VA surface proxy
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

/**
 * SECTION:gstvaapisurfaceproxy
 * @short_description: VA surface proxy
 */

#include "sysdeps.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapiminiobject.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_SURFACE_PROXY(obj) \
    ((GstVaapiSurfaceProxy *)(obj))

#define GST_VAAPI_IS_SURFACE_PROXY(obj) \
    (GST_VAAPI_SURFACE_PROXY(obj) != NULL)

struct _GstVaapiSurfaceProxy {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    GstVaapiContext    *context;
    GstVaapiSurface    *surface;
};

static void
gst_vaapi_surface_proxy_finalize(GstVaapiSurfaceProxy *proxy)
{
    gst_vaapi_surface_proxy_set_surface(proxy, NULL);
    gst_vaapi_surface_proxy_set_context(proxy, NULL);
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_surface_proxy_class(void)
{
    static const GstVaapiMiniObjectClass GstVaapiSurfaceProxyClass = {
        sizeof(GstVaapiSurfaceProxy),
        (GDestroyNotify)gst_vaapi_surface_proxy_finalize
    };
    return &GstVaapiSurfaceProxyClass;
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
    GstVaapiSurfaceProxy *proxy;

    g_return_val_if_fail(GST_VAAPI_IS_CONTEXT(context), NULL);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    proxy = (GstVaapiSurfaceProxy *)
        gst_vaapi_mini_object_new(gst_vaapi_surface_proxy_class());
    if (!proxy)
        return NULL;

    proxy->context = g_object_ref(context);
    proxy->surface = g_object_ref(surface);
    return proxy;
}

/**
 * gst_vaapi_surface_proxy_ref:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Atomically increases the reference count of the given @proxy by one.
 *
 * Returns: The same @proxy argument
 */
GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_ref(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), NULL);

    return GST_VAAPI_SURFACE_PROXY(gst_vaapi_mini_object_ref(
                                       GST_VAAPI_MINI_OBJECT(proxy)));
}

/**
 * gst_vaapi_surface_proxy_unref:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Atomically decreases the reference count of the @proxy by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_surface_proxy_unref(GstVaapiSurfaceProxy *proxy)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));

    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(proxy));
}

/**
 * gst_vaapi_surface_proxy_replace:
 * @old_proxy_ptr: a pointer to a #GstVaapiSurfaceProxy
 * @new_proxy: a #GstVaapiSurfaceProxy
 *
 * Atomically replaces the proxy object held in @old_proxy_ptr with
 * @new_proxy. This means that @old_proxy_ptr shall reference a valid
 * object. However, @new_proxy can be NULL.
 */
void
gst_vaapi_surface_proxy_replace(GstVaapiSurfaceProxy **old_proxy_ptr,
    GstVaapiSurfaceProxy *new_proxy)
{
    g_return_if_fail(old_proxy_ptr != NULL);

    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)old_proxy_ptr,
        GST_VAAPI_MINI_OBJECT(new_proxy));
}

/**
 * gst_vaapi_surface_proxy_get_user_data:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Gets user-provided data set on the object via a previous call to
 * gst_vaapi_surface_proxy_set_user_data().
 *
 * Returns: (transfer none): The previously set user_data
 */
gpointer
gst_vaapi_surface_proxy_get_user_data(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), NULL);

    return gst_vaapi_mini_object_get_user_data(GST_VAAPI_MINI_OBJECT(proxy));
}

/**
 * gst_vaapi_surface_proxy_set_user_data:
 * @proxy: a #GstVaapiSurfaceProxy
 * @user_data: user-provided data
 * @destroy_notify: (closure user_data): a #GDestroyNotify
 *
 * Sets @user_data on the object and the #GDestroyNotify that will be
 * called when the data is freed.
 *
 * If some @user_data was previously set, then the former @destroy_notify
 * function will be called before the @user_data is replaced.
 */
void
gst_vaapi_surface_proxy_set_user_data(GstVaapiSurfaceProxy *proxy,
    gpointer user_data, GDestroyNotify destroy_notify)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));

    gst_vaapi_mini_object_set_user_data(GST_VAAPI_MINI_OBJECT(proxy),
        user_data, destroy_notify);
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

    g_clear_object(&proxy->context);

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
 * gst_vaapi_surface_proxy_get_surface_id:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the VA surface ID stored in the @proxy.
 *
 * Return value: the #GstVaapiID
 */
GstVaapiID
gst_vaapi_surface_proxy_get_surface_id(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), GST_VAAPI_ID_NONE);
    g_return_val_if_fail(proxy->surface != NULL, GST_VAAPI_ID_NONE);

    return GST_VAAPI_OBJECT_ID(proxy->surface);
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

    if (proxy->surface) {
        if (proxy->context)
            gst_vaapi_context_put_surface(proxy->context, proxy->surface);
        g_object_unref(proxy->surface);
        proxy->surface = NULL;
    }

    if (surface)
        proxy->surface = g_object_ref(surface);
}
