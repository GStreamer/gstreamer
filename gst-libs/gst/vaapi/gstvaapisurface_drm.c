/*
 *  gstvaapisurface_drm.c - VA surface abstraction (DRM interop)
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
#include "gstvaapisurface_drm.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapiimage_priv.h"
#include "gstvaapibufferproxy_priv.h"

static GstVaapiBufferProxy *
gst_vaapi_surface_get_drm_buf_handle (GstVaapiSurface * surface, guint type)
{
  GstVaapiBufferProxy *proxy;
  GstVaapiImage *image;

  image = gst_vaapi_surface_derive_image (surface);
  if (!image)
    goto error_derive_image;

  proxy =
      gst_vaapi_buffer_proxy_new_from_object (GST_VAAPI_OBJECT (surface),
      image->internal_image.buf, type, gst_vaapi_object_unref, image);
  if (!proxy)
    goto error_alloc_export_buffer;
  return proxy;

  /* ERRORS */
error_derive_image:
  GST_ERROR ("failed to extract image handle from surface");
  return NULL;
error_alloc_export_buffer:
  GST_ERROR ("failed to allocate export buffer proxy");
  gst_vaapi_object_unref (image);
  return NULL;
}

/**
 * gst_vaapi_surface_get_dma_buf_handle:
 * @surface: a #GstVaapiSurface
 *
 * If the underlying VA driver implementation supports it, this
 * function allows for returning a suitable dma_buf (DRM) buffer
 * handle as a #GstVaapiBufferProxy instance. The resulting buffer
 * handle is live until the last reference to the proxy gets
 * released. Besides, any further change to the parent VA @surface may
 * fail.
 *
 * Return value: the underlying buffer as a #GstVaapiBufferProxy
 * instance.
 */
GstVaapiBufferProxy *
gst_vaapi_surface_get_dma_buf_handle (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, NULL);

  return gst_vaapi_surface_get_drm_buf_handle (surface,
      GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF);
}
