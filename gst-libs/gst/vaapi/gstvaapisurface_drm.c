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

  /* The proxy takes ownership if the image, even creation failure. */
  proxy =
      gst_vaapi_buffer_proxy_new_from_surface (GST_MINI_OBJECT_CAST (surface),
      image->internal_image.buf, type, (GDestroyNotify) gst_vaapi_image_unref,
      image);
  if (!proxy)
    goto error_alloc_export_buffer;
  return proxy;

  /* ERRORS */
error_derive_image:
  {
    GST_ERROR ("failed to extract image handle from surface");
    return NULL;
  }
error_alloc_export_buffer:
  {
    GST_ERROR ("failed to allocate export buffer proxy");
    return NULL;
  }
}

/**
 * gst_vaapi_surface_peek_dma_buf_handle:
 * @surface: a #GstVaapiSurface
 *
 * If the underlying VA driver implementation supports it, this
 * function allows for returning a suitable dma_buf (DRM) buffer
 * handle as a #GstVaapiBufferProxy instance. The returned buffer
 * proxy does not increase the ref of underlying buffer proxy.
 *
 * Return value: the underlying buffer as a #GstVaapiBufferProxy
 * instance.
 */
GstVaapiBufferProxy *
gst_vaapi_surface_peek_dma_buf_handle (GstVaapiSurface * surface)
{
  GstVaapiBufferProxy *buf_proxy;

  g_return_val_if_fail (surface != NULL, NULL);

  if (surface->extbuf_proxy)
    return surface->extbuf_proxy;

  buf_proxy = gst_vaapi_surface_get_drm_buf_handle (surface,
      GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF);

  if (buf_proxy) {
    gst_vaapi_surface_set_buffer_proxy (surface, buf_proxy);
    gst_vaapi_buffer_proxy_unref (buf_proxy);
  }

  return buf_proxy;
}

/**
 * gst_vaapi_surface_peek_gem_buf_handle:
 * @surface: a #GstVaapiSurface
 *
 * If the underlying VA driver implementation supports it, this
 * function allows for returning a suitable GEM buffer handle as a
 * #GstVaapiBufferProxy instance. The returned buffer proxy does
 * not increase the ref of underlying buffer proxy.
 *
 * Return value: the underlying buffer as a #GstVaapiBufferProxy
 * instance.
 */
GstVaapiBufferProxy *
gst_vaapi_surface_peek_gem_buf_handle (GstVaapiSurface * surface)
{
  GstVaapiBufferProxy *buf_proxy;

  g_return_val_if_fail (surface != NULL, NULL);

  if (surface->extbuf_proxy)
    return surface->extbuf_proxy;

  buf_proxy = gst_vaapi_surface_get_drm_buf_handle (surface,
      GST_VAAPI_BUFFER_MEMORY_TYPE_GEM_BUF);

  if (buf_proxy) {
    gst_vaapi_surface_set_buffer_proxy (surface, buf_proxy);
    gst_vaapi_buffer_proxy_unref (buf_proxy);
  }

  return buf_proxy;
}

static void
fill_video_info (GstVideoInfo * vip, GstVideoFormat format, guint width,
    guint height, gsize offset[GST_VIDEO_MAX_PLANES],
    gint stride[GST_VIDEO_MAX_PLANES])
{
  guint i;

  gst_video_info_set_format (vip, format, width, height);
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (vip); i++) {
    GST_VIDEO_INFO_PLANE_OFFSET (vip, i) = offset[i];
    GST_VIDEO_INFO_PLANE_STRIDE (vip, i) = stride[i];
  }
}

/**
 * gst_vaapi_surface_new_with_dma_buf_handle:
 * @display: a #GstVaapiDisplay
 * @fd: the DRM PRIME file descriptor
 * @size: the underlying DRM buffer size
 * @format: the desired surface format
 * @width: the desired surface width in pixels
 * @height: the desired surface height in pixels
 * @offset: the offsets to each plane
 * @stride: the pitches for each plane
 *
 * Creates a new #GstVaapiSurface with an external DRM PRIME file
 * descriptor. The newly created VA surfaces owns the supplied buffer
 * handle.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation from DRM PRIME fd failed, or is not supported
 */
GstVaapiSurface *
gst_vaapi_surface_new_with_dma_buf_handle (GstVaapiDisplay * display, gint fd,
    GstVideoInfo * vi)
{
  GstVaapiBufferProxy *proxy;
  GstVaapiSurface *surface;

  proxy = gst_vaapi_buffer_proxy_new ((gintptr) fd,
      GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF, GST_VIDEO_INFO_SIZE (vi), NULL,
      NULL);
  if (!proxy)
    return NULL;

  surface = gst_vaapi_surface_new_from_buffer_proxy (display, proxy, vi);
  /* Surface holds proxy's reference */
  gst_vaapi_buffer_proxy_unref (proxy);
  return surface;
}

/**
 * gst_vaapi_surface_new_with_gem_buf_handle:
 * @display: a #GstVaapiDisplay
 * @name: the DRM GEM buffer name
 * @size: the underlying DRM buffer size
 * @format: the desired surface format
 * @width: the desired surface width in pixels
 * @height: the desired surface height in pixels
 * @offset: the offsets to each plane
 * @stride: the pitches for each plane
 *
 * Creates a new #GstVaapiSurface with an external DRM GEM buffer
 * name. The newly created VA surfaces owns the supplied buffer
 * handle.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation from GEM @name failed, or is not supported
 */
GstVaapiSurface *
gst_vaapi_surface_new_with_gem_buf_handle (GstVaapiDisplay * display,
    guint32 name, guint size, GstVideoFormat format, guint width, guint height,
    gsize offset[GST_VIDEO_MAX_PLANES], gint stride[GST_VIDEO_MAX_PLANES])
{
  GstVaapiBufferProxy *proxy;
  GstVaapiSurface *surface;
  GstVideoInfo vi;

  proxy = gst_vaapi_buffer_proxy_new ((guintptr) name,
      GST_VAAPI_BUFFER_MEMORY_TYPE_GEM_BUF, size, NULL, NULL);
  if (!proxy)
    return NULL;

  fill_video_info (&vi, format, width, height, offset, stride);
  surface = gst_vaapi_surface_new_from_buffer_proxy (display, proxy, &vi);
  /* Surface holds proxy's reference */
  gst_vaapi_buffer_proxy_unref (proxy);
  return surface;
}
