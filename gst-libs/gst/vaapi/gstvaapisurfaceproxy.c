/*
 *  gstvaapisurfaceproxy.c - VA surface proxy
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

/**
 * SECTION:gstvaapisurfaceproxy
 * @short_description: VA surface proxy
 */

#include "sysdeps.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapisurfaceproxy_priv.h"
#include "gstvaapivideopool_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static void
gst_vaapi_surface_proxy_finalize (GstVaapiSurfaceProxy * proxy)
{
  if (proxy->surface) {
    if (proxy->pool && !proxy->parent)
      gst_vaapi_video_pool_put_object (proxy->pool, proxy->surface);
    gst_vaapi_surface_unref (proxy->surface);
    proxy->surface = NULL;
  }
  gst_vaapi_video_pool_replace (&proxy->pool, NULL);
  gst_vaapi_surface_proxy_replace (&proxy->parent, NULL);

  /* Notify the user function that the object is now destroyed */
  if (proxy->destroy_func)
    proxy->destroy_func (proxy->destroy_data);
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_surface_proxy_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiSurfaceProxyClass = {
    sizeof (GstVaapiSurfaceProxy),
    (GDestroyNotify) gst_vaapi_surface_proxy_finalize
  };
  return &GstVaapiSurfaceProxyClass;
}

static void
gst_vaapi_surface_proxy_init_properties (GstVaapiSurfaceProxy * proxy)
{
  proxy->view_id = 0;
  proxy->timestamp = GST_CLOCK_TIME_NONE;
  proxy->duration = GST_CLOCK_TIME_NONE;
  proxy->has_crop_rect = FALSE;
}

/**
 * gst_vaapi_surface_proxy_new:
 * @surface: a #GstVaapiSurface
 *
 * Creates a new #GstVaapiSurfaceProxy with the specified
 * surface. This allows for transporting additional information that
 * are not to be attached to the @surface directly.
 *
 * Return value: the newly allocated #GstVaapiSurfaceProxy object
 */
GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new (GstVaapiSurface * surface)
{
  GstVaapiSurfaceProxy *proxy;

  g_return_val_if_fail (surface != NULL, NULL);

  proxy = (GstVaapiSurfaceProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_surface_proxy_class ());
  if (!proxy)
    return NULL;

  proxy->parent = NULL;
  proxy->destroy_func = NULL;
  proxy->pool = NULL;
  proxy->surface =
      (GstVaapiSurface *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (surface));
  if (!proxy->surface)
    goto error;
  gst_vaapi_surface_proxy_init_properties (proxy);
  return proxy;

  /* ERRORS */
error:
  {
    gst_vaapi_surface_proxy_unref (proxy);
    return NULL;
  }
}

/**
 * gst_vaapi_surface_proxy_new_from_pool:
 * @pool: a #GstVaapiSurfacePool
 *
 * Allocates a new surface from the supplied surface @pool and creates
 * the wrapped surface proxy object from it. When the last reference
 * to the proxy object is released, then the underlying VA surface is
 * pushed back to its parent pool.
 *
 * Returns: The same newly allocated @proxy object, or %NULL on error
 */
GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_new_from_pool (GstVaapiSurfacePool * pool)
{
  GstVaapiSurfaceProxy *proxy;

  g_return_val_if_fail (pool != NULL, NULL);

  proxy = (GstVaapiSurfaceProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_surface_proxy_class ());
  if (!proxy)
    return NULL;

  proxy->parent = NULL;
  proxy->destroy_func = NULL;
  proxy->pool = gst_vaapi_video_pool_ref (GST_VAAPI_VIDEO_POOL (pool));
  proxy->surface = gst_vaapi_video_pool_get_object (proxy->pool);
  if (!proxy->surface)
    goto error;
  gst_mini_object_ref (GST_MINI_OBJECT_CAST (proxy->surface));
  gst_vaapi_surface_proxy_init_properties (proxy);
  return proxy;

  /* ERRORS */
error:
  {
    gst_vaapi_surface_proxy_unref (proxy);
    return NULL;
  }
}

/**
 * gst_vaapi_surface_proxy_copy:
 * @proxy: the parent #GstVaapiSurfaceProxy
 *
 * Creates are new VA surface proxy object from the supplied parent
 * @proxy object with the same initial information, e.g. timestamp,
 * duration.
 *
 * Note: the destroy notify function is not copied into the new
 * surface proxy object.
 *
 * Returns: The same newly allocated @proxy object, or %NULL on error
 */
GstVaapiSurfaceProxy *
gst_vaapi_surface_proxy_copy (GstVaapiSurfaceProxy * proxy)
{
  GstVaapiSurfaceProxy *copy;

  g_return_val_if_fail (proxy != NULL, NULL);

  copy = (GstVaapiSurfaceProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_surface_proxy_class ());
  if (!copy)
    return NULL;

  GST_VAAPI_SURFACE_PROXY_FLAGS (copy) = GST_VAAPI_SURFACE_PROXY_FLAGS (proxy);

  copy->parent = gst_vaapi_surface_proxy_ref (proxy->parent ?
      proxy->parent : proxy);
  copy->pool = proxy->pool ? gst_vaapi_video_pool_ref (proxy->pool) : NULL;
  copy->surface = (GstVaapiSurface *)
      gst_mini_object_ref (GST_MINI_OBJECT_CAST (proxy->surface));
  copy->view_id = proxy->view_id;
  copy->timestamp = proxy->timestamp;
  copy->duration = proxy->duration;
  copy->destroy_func = NULL;
  copy->has_crop_rect = proxy->has_crop_rect;
  if (copy->has_crop_rect)
    copy->crop_rect = proxy->crop_rect;

  return copy;
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
gst_vaapi_surface_proxy_ref (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return
      GST_VAAPI_SURFACE_PROXY (gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT
          (proxy)));
}

/**
 * gst_vaapi_surface_proxy_unref:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Atomically decreases the reference count of the @proxy by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_surface_proxy_unref (GstVaapiSurfaceProxy * proxy)
{
  g_return_if_fail (proxy != NULL);

  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy));
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
gst_vaapi_surface_proxy_replace (GstVaapiSurfaceProxy ** old_proxy_ptr,
    GstVaapiSurfaceProxy * new_proxy)
{
  g_return_if_fail (old_proxy_ptr != NULL);

  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_proxy_ptr,
      GST_VAAPI_MINI_OBJECT (new_proxy));
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
gst_vaapi_surface_proxy_get_surface (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return GST_VAAPI_SURFACE_PROXY_SURFACE (proxy);
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
gst_vaapi_surface_proxy_get_flags (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_SURFACE_PROXY_FLAGS (proxy);
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
gst_vaapi_surface_proxy_get_surface_id (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, VA_INVALID_ID);
  g_return_val_if_fail (proxy->surface != NULL, VA_INVALID_ID);

  return GST_VAAPI_SURFACE_PROXY_SURFACE_ID (proxy);
}

/**
 * gst_vaapi_surface_proxy_get_view_id:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the decoded view-id stored in the @proxy.
 *
 * Return value: the #GstVaapiID
 */
guintptr
gst_vaapi_surface_proxy_get_view_id (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_SURFACE_PROXY_VIEW_ID (proxy);
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
gst_vaapi_surface_proxy_get_timestamp (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_SURFACE_PROXY_TIMESTAMP (proxy);
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
gst_vaapi_surface_proxy_get_duration (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_SURFACE_PROXY_DURATION (proxy);
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
gst_vaapi_surface_proxy_set_destroy_notify (GstVaapiSurfaceProxy * proxy,
    GDestroyNotify destroy_func, gpointer user_data)
{
  g_return_if_fail (proxy != NULL);

  proxy->destroy_func = destroy_func;
  proxy->destroy_data = user_data;
}

/**
 * gst_vaapi_surface_proxy_get_crop_rect:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Returns the #GstVaapiRectangle stored in the @proxy and that
 * represents the cropping rectangle for the underlying surface to be
 * used for rendering.
 *
 * If no cropping rectangle was associated with the @proxy, then this
 * function returns %NULL.
 *
 * Return value: the #GstVaapiRectangle, or %NULL if none was
 *   associated with the surface proxy
 */
const GstVaapiRectangle *
gst_vaapi_surface_proxy_get_crop_rect (GstVaapiSurfaceProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return GST_VAAPI_SURFACE_PROXY_CROP_RECT (proxy);
}

/**
 * gst_vaapi_surface_proxy_set_crop_rect:
 * @proxy: #GstVaapiSurfaceProxy
 * @crop_rect: the #GstVaapiRectangle to be stored in @proxy
 *
 * Associates the @crop_rect with the @proxy
 */
void
gst_vaapi_surface_proxy_set_crop_rect (GstVaapiSurfaceProxy * proxy,
    const GstVaapiRectangle * crop_rect)
{
  g_return_if_fail (proxy != NULL);

  proxy->has_crop_rect = crop_rect != NULL;
  if (proxy->has_crop_rect)
    proxy->crop_rect = *crop_rect;
}
