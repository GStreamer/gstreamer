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
#include "gstvaapisurfaceproxy_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static void
gst_vaapi_surface_proxy_finalize(GstVaapiSurfaceProxy *proxy)
{
    if (proxy->destroy_func)
        proxy->destroy_func(proxy->destroy_data);

    if (proxy->surface) {
        if (proxy->pool)
            gst_vaapi_video_pool_put_object(proxy->pool, proxy->surface);
        g_object_unref(proxy->surface);
        proxy->surface = NULL;
    }
    g_clear_object(&proxy->pool);
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

GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new_from_pool(GstVaapiSurfacePool *pool)
{
    GstVaapiSurfaceProxy *proxy;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), NULL);

    proxy = (GstVaapiSurfaceProxy *)
        gst_vaapi_mini_object_new(gst_vaapi_surface_proxy_class());
    if (!proxy)
        return NULL;

    proxy->pool = g_object_ref(pool);
    proxy->surface = gst_vaapi_video_pool_get_object(proxy->pool);
    if (!proxy->surface)
        goto error;
    proxy->timestamp = GST_CLOCK_TIME_NONE;
    proxy->duration = GST_CLOCK_TIME_NONE;
    proxy->destroy_func = NULL;
    g_object_ref(proxy->surface);
    return proxy;

error:
    gst_vaapi_surface_proxy_unref(proxy);
    return NULL;
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

    return GST_VAAPI_SURFACE_PROXY_SURFACE(proxy);
}

/**
 * gst_vaapi_surface_proxy_get_flags:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the #GstVaapiSurfaceProxyFlags associated with this surface
 * @proxy.
 *
 * Return value: the set of #GstVaapiSurfaceProxyFlags
 */
guint
gst_vaapi_surface_proxy_get_flags(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), 0);
    
    return GST_VAAPI_SURFACE_PROXY_FLAGS(proxy);
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

    return GST_VAAPI_SURFACE_PROXY_SURFACE_ID(proxy);
}

/**
 * gst_vaapi_surface_proxy_get_timestamp:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the presentation timestamp for this surface @proxy.
 *
 * Return value: the presentation timestamp
 */
GstClockTime
gst_vaapi_surface_proxy_get_timestamp(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), 0);
    
    return GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy);
}

/**
 * gst_vaapi_surface_proxy_get_duration:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the presentation duration for this surface @proxy.
 *
 * Return value: the presentation duration
 */
GstClockTime
gst_vaapi_surface_proxy_get_duration(GstVaapiSurfaceProxy *proxy)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), 0);
    
    return GST_VAAPI_SURFACE_PROXY_DURATION(proxy);
}

/**
 * gst_vaapi_surface_proxy_set_destroy_notify:
 * @proxy: a @GstVaapiSurfaceProxy
 * @destroy_func: a #GDestroyNotify function
 * @user_data: some extra data to pass to the @destroy_func function
 *
 * Sets @destroy_func as the function to call when the surface @proxy
 * was released. At this point, the proxy object is considered
 * released, i.e. the underlying data storage is no longer valid and
 * the callback function shall not expect anything from that.
 */
void
gst_vaapi_surface_proxy_set_destroy_notify(GstVaapiSurfaceProxy *proxy,
    GDestroyNotify destroy_func, gpointer user_data)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));

    proxy->destroy_func = destroy_func;
    proxy->destroy_data = user_data;
}
