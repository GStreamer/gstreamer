/*
 *  gstvaapicodedbufferproxy.c - VA coded buffer proxy
 *
 *  Copyright (C) 2013 Intel Corporation
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

#include "sysdeps.h"
#include "gstvaapicodedbufferproxy.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapivideopool_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static void
coded_buffer_proxy_set_user_data (GstVaapiCodedBufferProxy * proxy,
    gpointer user_data, GDestroyNotify destroy_func)
{
  if (proxy->user_data_destroy)
    proxy->user_data_destroy (proxy->user_data);

  proxy->user_data = user_data;
  proxy->user_data_destroy = destroy_func;
}

static void
coded_buffer_proxy_finalize (GstVaapiCodedBufferProxy * proxy)
{
  if (proxy->buffer) {
    if (proxy->pool)
      gst_vaapi_video_pool_put_object (proxy->pool, proxy->buffer);
    gst_vaapi_coded_buffer_unref (proxy->buffer);
    proxy->buffer = NULL;
  }
  gst_vaapi_video_pool_replace (&proxy->pool, NULL);
  coded_buffer_proxy_set_user_data (proxy, NULL, NULL);

  /* Notify the user function that the object is now destroyed */
  if (proxy->destroy_func)
    proxy->destroy_func (proxy->destroy_data);
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_coded_buffer_proxy_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiCodedBufferProxyClass = {
    sizeof (GstVaapiCodedBufferProxy),
    (GDestroyNotify) coded_buffer_proxy_finalize
  };
  return &GstVaapiCodedBufferProxyClass;
}

/**
 * gst_vaapi_coded_buffer_proxy_new_from_pool:
 * @pool: a #GstVaapiCodedBufferPool
 *
 * Allocates a new coded buffer from the supplied @pool and creates
 * the wrapped coded buffer proxy object from it. When the last
 * reference to the proxy object is released, then the underlying VA
 * coded buffer is pushed back to its parent pool.
 *
 * Returns: The same newly allocated @proxy object, or %NULL on error
 */
GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_new_from_pool (GstVaapiCodedBufferPool * pool)
{
  GstVaapiCodedBufferProxy *proxy;

  g_return_val_if_fail (pool != NULL, NULL);
  g_return_val_if_fail (GST_VAAPI_VIDEO_POOL (pool)->object_type ==
      GST_VAAPI_VIDEO_POOL_OBJECT_TYPE_CODED_BUFFER, NULL);

  proxy = (GstVaapiCodedBufferProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_coded_buffer_proxy_class ());
  if (!proxy)
    return NULL;

  proxy->destroy_func = NULL;
  proxy->user_data_destroy = NULL;
  proxy->pool = gst_vaapi_video_pool_ref (GST_VAAPI_VIDEO_POOL (pool));
  proxy->buffer = gst_vaapi_video_pool_get_object (proxy->pool);
  if (!proxy->buffer)
    goto error;
  gst_mini_object_ref (GST_MINI_OBJECT_CAST (proxy->buffer));
  return proxy;

  /* ERRORS */
error:
  {
    gst_vaapi_coded_buffer_proxy_unref (proxy);
    return NULL;
  }
}

/**
 * gst_vaapi_coded_buffer_proxy_ref:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Atomically increases the reference count of the given @proxy by one.
 *
 * Returns: The same @proxy argument
 */
GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_ref (GstVaapiCodedBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return GST_VAAPI_CODED_BUFFER_PROXY (gst_vaapi_mini_object_ref
      (GST_VAAPI_MINI_OBJECT (proxy)));
}

/**
 * gst_vaapi_coded_buffer_proxy_unref:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Atomically decreases the reference count of the @proxy by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_coded_buffer_proxy_unref (GstVaapiCodedBufferProxy * proxy)
{
  g_return_if_fail (proxy != NULL);

  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy));
}

/**
 * gst_vaapi_coded_buffer_proxy_replace:
 * @old_proxy_ptr: a pointer to a #GstVaapiCodedBufferProxy
 * @new_proxy: a #GstVaapiCodedBufferProxy
 *
 * Atomically replaces the proxy object held in @old_proxy_ptr with
 * @new_proxy. This means that @old_proxy_ptr shall reference a valid
 * object. However, @new_proxy can be NULL.
 */
void
gst_vaapi_coded_buffer_proxy_replace (GstVaapiCodedBufferProxy ** old_proxy_ptr,
    GstVaapiCodedBufferProxy * new_proxy)
{
  g_return_if_fail (old_proxy_ptr != NULL);

  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_proxy_ptr,
      GST_VAAPI_MINI_OBJECT (new_proxy));
}

/**
 * gst_vaapi_coded_buffer_proxy_get_buffer:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Returns the #GstVaapiCodedBuffer stored in the @proxy.
 *
 * Return value: the #GstVaapiCodedBuffer, or %NULL if an error occurred
 */
GstVaapiCodedBuffer *
gst_vaapi_coded_buffer_proxy_get_buffer (GstVaapiCodedBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (proxy);
}

/**
 * gst_vaapi_coded_buffer_proxy_get_buffer_size:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Returns the size of the underlying #GstVaapiCodedBuffer object
 * stored in the @proxy.
 *
 * Return value: the underlying #GstVaapiCodedBuffer size, or -1 if an
 *   error occurred
 */
gssize
gst_vaapi_coded_buffer_proxy_get_buffer_size (GstVaapiCodedBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, -1);

  return GST_VAAPI_CODED_BUFFER_PROXY_BUFFER_SIZE (proxy);
}

/**
 * gst_vaapi_coded_buffer_proxy_set_destroy_notify:
 * @proxy: a @GstVaapiCodedBufferProxy
 * @destroy_func: a #GDestroyNotify function
 * @user_data: some extra data to pass to the @destroy_func function
 *
 * Sets @destroy_func as the function to call when the coded buffer
 * @proxy was released. At this point, the proxy object is considered
 * released, i.e. the underlying data storage is no longer valid and
 * the callback function shall not expect anything from that.
 */
void
gst_vaapi_coded_buffer_proxy_set_destroy_notify (GstVaapiCodedBufferProxy *
    proxy, GDestroyNotify destroy_func, gpointer user_data)
{
  g_return_if_fail (proxy != NULL);

  proxy->destroy_func = destroy_func;
  proxy->destroy_data = user_data;
}

/**
 * gst_vaapi_coded_buffer_proxy_get_user_data:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Gets private data previously set on the VA coded buffer proxy
 * object through the gst_vaapi_coded_buffer_proxy_set_user_data()
 * function.
 *
 * Return value: the previously set user-data
 */
gpointer
gst_vaapi_coded_buffer_proxy_get_user_data (GstVaapiCodedBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return proxy->user_data;
}

/**
 * gst_vaapi_coded_buffer_proxy_set_user_data:
 * @proxy: a #GstVaapiCodedBufferProxy
 * @user_data: user-defined data
 * @destroy_func: a #GDestroyNotify
 *
 * Sets @user_data on the VA coded buffer proxy object and the
 * #GDestroyNotify function that will be called when the coded buffer
 * proxy object is released.
 *
 * If a @user_data was previously set, then the previously set
 * @destroy_func function, if any, will be called before the
 * @user_data is replaced.
 */
void
gst_vaapi_coded_buffer_proxy_set_user_data (GstVaapiCodedBufferProxy * proxy,
    gpointer user_data, GDestroyNotify destroy_func)
{
  g_return_if_fail (proxy != NULL);

  coded_buffer_proxy_set_user_data (proxy, user_data, destroy_func);
}
