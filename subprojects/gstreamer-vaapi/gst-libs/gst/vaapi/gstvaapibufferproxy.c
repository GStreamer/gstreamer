/*
 *  gstvaapibufferproxy.c - Buffer proxy abstraction
 *
 *  Copyright (C) 2014 Intel Corporation
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
#include "gstvaapicompat.h"
#include "gstvaapibufferproxy.h"
#include "gstvaapibufferproxy_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static gboolean
gst_vaapi_buffer_proxy_acquire_handle (GstVaapiBufferProxy * proxy)
{
  GstVaapiDisplay *display;
  const guint mem_type = proxy->va_info.mem_type;
  VAStatus va_status;

  if (proxy->va_info.handle)
    return TRUE;

  if (!proxy->surface || proxy->va_buf == VA_INVALID_ID)
    return FALSE;

  display = GST_VAAPI_SURFACE_DISPLAY (GST_VAAPI_SURFACE (proxy->surface));

  GST_VAAPI_DISPLAY_LOCK (display);
  va_status = vaAcquireBufferHandle (GST_VAAPI_DISPLAY_VADISPLAY (display),
      proxy->va_buf, &proxy->va_info);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (va_status, "vaAcquireBufferHandle()"))
    return FALSE;
  if (proxy->va_info.mem_type != mem_type)
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapi_buffer_proxy_release_handle (GstVaapiBufferProxy * proxy)
{
  GstVaapiDisplay *display;
  VAStatus va_status;

  if (!proxy->va_info.handle)
    return TRUE;

  if (!proxy->surface || proxy->va_buf == VA_INVALID_ID)
    return FALSE;

  display = GST_VAAPI_SURFACE_DISPLAY (GST_VAAPI_SURFACE (proxy->surface));

  GST_VAAPI_DISPLAY_LOCK (display);
  va_status = vaReleaseBufferHandle (GST_VAAPI_DISPLAY_VADISPLAY (display),
      proxy->va_buf);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (va_status, "vaReleaseBufferHandle()"))
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_buffer_proxy_finalize (GstVaapiBufferProxy * proxy)
{
  gst_vaapi_buffer_proxy_release_handle (proxy);

  /* Notify the user function that the object is now destroyed */
  if (proxy->destroy_func)
    proxy->destroy_func (proxy->destroy_data);

  proxy->surface = NULL;
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_buffer_proxy_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiBufferProxyClass = {
    sizeof (GstVaapiBufferProxy),
    (GDestroyNotify) gst_vaapi_buffer_proxy_finalize
  };
  return &GstVaapiBufferProxyClass;
}

GstVaapiBufferProxy *
gst_vaapi_buffer_proxy_new (guintptr handle, guint type, gsize size,
    GDestroyNotify destroy_func, gpointer user_data)
{
  GstVaapiBufferProxy *proxy;

  g_return_val_if_fail (handle != 0, NULL);
  g_return_val_if_fail (size > 0, NULL);

  proxy = (GstVaapiBufferProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_buffer_proxy_class ());
  if (!proxy)
    return NULL;

  proxy->surface = NULL;
  proxy->destroy_func = destroy_func;
  proxy->destroy_data = user_data;
  proxy->type = type;
  proxy->va_buf = VA_INVALID_ID;
  proxy->va_info.handle = handle;
  proxy->va_info.type = VAImageBufferType;
  proxy->va_info.mem_type = from_GstVaapiBufferMemoryType (proxy->type);
  proxy->va_info.mem_size = size;
  if (!proxy->va_info.mem_type)
    goto error_unsupported_mem_type;
  return proxy;

  /* ERRORS */
error_unsupported_mem_type:
  {
    GST_ERROR ("unsupported buffer type (%d)", proxy->type);
    gst_vaapi_buffer_proxy_unref (proxy);
    return NULL;
  }
}

GstVaapiBufferProxy *
gst_vaapi_buffer_proxy_new_from_surface (GstMiniObject * surface,
    VABufferID buf_id, guint type, GDestroyNotify destroy_func, gpointer data)
{
  GstVaapiBufferProxy *proxy;

  g_return_val_if_fail (surface != NULL, NULL);

  proxy = (GstVaapiBufferProxy *)
      gst_vaapi_mini_object_new (gst_vaapi_buffer_proxy_class ());
  if (!proxy)
    return NULL;

  proxy->surface = surface;
  proxy->destroy_func = destroy_func;
  proxy->destroy_data = data;
  proxy->type = type;
  proxy->va_buf = buf_id;
  memset (&proxy->va_info, 0, sizeof (proxy->va_info));
  proxy->va_info.mem_type = from_GstVaapiBufferMemoryType (proxy->type);
  if (!proxy->va_info.mem_type)
    goto error_unsupported_mem_type;
  if (!gst_vaapi_buffer_proxy_acquire_handle (proxy))
    goto error_acquire_handle;
  return proxy;

  /* ERRORS */
error_unsupported_mem_type:
  {
    GST_ERROR ("unsupported buffer type (%d)", proxy->type);
    gst_vaapi_buffer_proxy_unref (proxy);
    return NULL;
  }
error_acquire_handle:
  {
    GST_ERROR ("failed to acquire the underlying VA buffer handle");
    gst_vaapi_buffer_proxy_unref (proxy);
    return NULL;
  }
}

/**
 * gst_vaapi_buffer_proxy_ref:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Atomically increases the reference count of the given @proxy by one.
 *
 * Returns: The same @proxy argument
 */
GstVaapiBufferProxy *
gst_vaapi_buffer_proxy_ref (GstVaapiBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, NULL);

  return (GstVaapiBufferProxy *)
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (proxy));
}

/**
 * gst_vaapi_buffer_proxy_unref:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Atomically decreases the reference count of the @proxy by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_buffer_proxy_unref (GstVaapiBufferProxy * proxy)
{
  g_return_if_fail (proxy != NULL);

  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy));
}

/**
 * gst_vaapi_buffer_proxy_replace:
 * @old_proxy_ptr: a pointer to a #GstVaapiBufferProxy
 * @new_proxy: a #GstVaapiBufferProxy
 *
 * Atomically replaces the proxy object held in @old_proxy_ptr with
 * @new_proxy. This means that @old_proxy_ptr shall reference a valid
 * object. However, @new_proxy can be NULL.
 */
void
gst_vaapi_buffer_proxy_replace (GstVaapiBufferProxy ** old_proxy_ptr,
    GstVaapiBufferProxy * new_proxy)
{
  g_return_if_fail (old_proxy_ptr != NULL);

  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (old_proxy_ptr),
      GST_VAAPI_MINI_OBJECT (new_proxy));
}

/**
 * gst_vaapi_buffer_proxy_get_type:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Returns the underlying VA buffer memory type.
 *
 * Return value: the buffer memory type
 */
guint
gst_vaapi_buffer_proxy_get_type (GstVaapiBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_BUFFER_PROXY_TYPE (proxy);
}

/**
 * gst_vaapi_buffer_proxy_get_handle:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Returns the underlying VA buffer handle stored in the @proxy.
 *
 * Return value: the buffer handle
 */
guintptr
gst_vaapi_buffer_proxy_get_handle (GstVaapiBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_BUFFER_PROXY_HANDLE (proxy);
}

/**
 * gst_vaapi_buffer_proxy_get_size:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Returns the underlying VA buffer memory size in bytes.
 *
 * Return value: the buffer size in bytes
 */
gsize
gst_vaapi_buffer_proxy_get_size (GstVaapiBufferProxy * proxy)
{
  g_return_val_if_fail (proxy != NULL, 0);

  return GST_VAAPI_BUFFER_PROXY_SIZE (proxy);
}

/**
 * gst_vaapi_buffer_proxy_release_data:
 * @proxy: a #GstVaapiBufferProxy
 *
 * Notifies the user to destroy the user's data, though the @proxy is
 * not going to be destroyed.
 **/
void
gst_vaapi_buffer_proxy_release_data (GstVaapiBufferProxy * proxy)
{
  g_return_if_fail (proxy != NULL);

  if (proxy->destroy_func) {
    proxy->destroy_func (proxy->destroy_data);
    proxy->destroy_func = NULL;
    proxy->destroy_data = NULL;
  }
}
