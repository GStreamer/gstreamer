/*
 *  gstvaapisurface.c - VA surface abstraction
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
 * SECTION:gstvaapisurface
 * @short_description: VA surface abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisurface.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapiimage.h"
#include "gstvaapiimage_priv.h"
#include "gstvaapibufferproxy_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

static gboolean
_gst_vaapi_surface_associate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture, const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect);

static gboolean
_gst_vaapi_surface_deassociate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture);

static void
destroy_subpicture_cb (gpointer subpicture, gpointer surface)
{
  _gst_vaapi_surface_deassociate_subpicture (surface, subpicture);
  gst_vaapi_subpicture_unref (subpicture);
}

static void
gst_vaapi_surface_destroy_subpictures (GstVaapiSurface * surface)
{
  if (surface->subpictures) {
    g_ptr_array_foreach (surface->subpictures, destroy_subpicture_cb, surface);
    g_clear_pointer (&surface->subpictures, g_ptr_array_unref);
  }
}

static void
gst_vaapi_surface_free (GstVaapiSurface * surface)
{
  GstVaapiDisplay *const display = GST_VAAPI_SURFACE_DISPLAY (surface);
  VASurfaceID surface_id;
  VAStatus status;

  surface_id = GST_VAAPI_SURFACE_ID (surface);
  GST_DEBUG ("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (surface_id));

  gst_vaapi_surface_destroy_subpictures (surface);

  if (surface_id != VA_INVALID_SURFACE) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroySurfaces (GST_VAAPI_DISPLAY_VADISPLAY (display),
        &surface_id, 1);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroySurfaces()"))
      GST_WARNING ("failed to destroy surface %" GST_VAAPI_ID_FORMAT,
          GST_VAAPI_ID_ARGS (surface_id));
    GST_VAAPI_SURFACE_ID (surface) = VA_INVALID_SURFACE;
  }
  gst_vaapi_buffer_proxy_replace (&surface->extbuf_proxy, NULL);
  gst_vaapi_display_replace (&GST_VAAPI_SURFACE_DISPLAY (surface), NULL);

  g_free (surface);
}

static gboolean
gst_vaapi_surface_init (GstVaapiSurface * surface,
    GstVaapiChromaType chroma_type, guint width, guint height)
{
  GstVaapiDisplay *const display = GST_VAAPI_SURFACE_DISPLAY (surface);
  VASurfaceID surface_id;
  VAStatus status;
  guint va_chroma_format;

  va_chroma_format = from_GstVaapiChromaType (chroma_type);
  if (!va_chroma_format)
    goto error_unsupported_chroma_type;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateSurfaces (GST_VAAPI_DISPLAY_VADISPLAY (display),
      width, height, va_chroma_format, 1, &surface_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateSurfaces()"))
    return FALSE;

  GST_VAAPI_SURFACE_FORMAT (surface) = GST_VIDEO_FORMAT_UNKNOWN;
  GST_VAAPI_SURFACE_CHROMA_TYPE (surface) = chroma_type;
  GST_VAAPI_SURFACE_WIDTH (surface) = width;
  GST_VAAPI_SURFACE_HEIGHT (surface) = height;

  GST_DEBUG ("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (surface_id));
  GST_VAAPI_SURFACE_ID (surface) = surface_id;
  return TRUE;

  /* ERRORS */
error_unsupported_chroma_type:
  GST_ERROR ("unsupported chroma-type %u", chroma_type);
  return FALSE;
}

static guint
get_usage_hint (guint alloc_flags)
{
  guint usage_hints = VA_SURFACE_ATTRIB_USAGE_HINT_GENERIC;

  /* XXX(victor): So far, only media-driver uses hints for encoders
   * and it doesn't test it as bitwise */
  if (alloc_flags & GST_VAAPI_SURFACE_ALLOC_FLAG_HINT_DECODER)
    usage_hints = VA_SURFACE_ATTRIB_USAGE_HINT_DECODER;
  else if (alloc_flags & GST_VAAPI_SURFACE_ALLOC_FLAG_HINT_ENCODER)
    usage_hints = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;

  return usage_hints;
}

static gboolean
gst_vaapi_surface_init_full (GstVaapiSurface * surface,
    const GstVideoInfo * vip, guint surface_allocation_flags)
{
  GstVaapiDisplay *const display = GST_VAAPI_SURFACE_DISPLAY (surface);
  const GstVideoFormat format = GST_VIDEO_INFO_FORMAT (vip);
  VASurfaceID surface_id;
  VAStatus status;
  guint chroma_type, va_chroma_format, i;
  const VAImageFormat *va_format;
  VASurfaceAttrib attribs[4], *attrib;
  VASurfaceAttribExternalBuffers extbuf = { 0, };
  gboolean extbuf_needed = FALSE;

  va_format = gst_vaapi_video_format_to_va_format (format);
  if (!va_format)
    goto error_unsupported_format;

  chroma_type = gst_vaapi_video_format_get_chroma_type (format);
  if (!chroma_type)
    goto error_unsupported_format;

  va_chroma_format = from_GstVaapiChromaType (chroma_type);
  if (!va_chroma_format)
    goto error_unsupported_format;

  extbuf.pixel_format = va_format->fourcc;
  extbuf.width = GST_VIDEO_INFO_WIDTH (vip);
  extbuf.height = GST_VIDEO_INFO_HEIGHT (vip);
  if (surface_allocation_flags & GST_VAAPI_SURFACE_ALLOC_FLAG_LINEAR_STORAGE) {
    extbuf.flags &= ~VA_SURFACE_EXTBUF_DESC_ENABLE_TILING;
    extbuf_needed = TRUE;
  }

  extbuf.num_planes = GST_VIDEO_INFO_N_PLANES (vip);
  if (surface_allocation_flags & (GST_VAAPI_SURFACE_ALLOC_FLAG_FIXED_STRIDES |
          GST_VAAPI_SURFACE_ALLOC_FLAG_FIXED_OFFSETS)) {
    for (i = 0; i < extbuf.num_planes; i++) {
      if (surface_allocation_flags & GST_VAAPI_SURFACE_ALLOC_FLAG_FIXED_STRIDES)
        extbuf.pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vip, i);
      if (surface_allocation_flags & GST_VAAPI_SURFACE_ALLOC_FLAG_FIXED_OFFSETS)
        extbuf.offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET (vip, i);
    }
    extbuf_needed = TRUE;
  }

  attrib = attribs;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->type = VASurfaceAttribPixelFormat;
  attrib->value.type = VAGenericValueTypeInteger;
  attrib->value.value.i = va_format->fourcc;
  attrib++;

  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->type = VASurfaceAttribUsageHint;
  attrib->value.type = VAGenericValueTypeInteger;
  attrib->value.value.i = get_usage_hint (surface_allocation_flags);
  attrib++;

  if (extbuf_needed) {
    attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrib->type = VASurfaceAttribMemoryType;
    attrib->value.type = VAGenericValueTypeInteger;
    attrib->value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA;
    attrib++;

    attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
    attrib->type = VASurfaceAttribExternalBufferDescriptor;
    attrib->value.type = VAGenericValueTypePointer;
    attrib->value.value.p = &extbuf;
    attrib++;
  }

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateSurfaces (GST_VAAPI_DISPLAY_VADISPLAY (display),
      va_chroma_format, extbuf.width, extbuf.height, &surface_id, 1,
      attribs, attrib - attribs);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateSurfaces()"))
    return FALSE;

  GST_VAAPI_SURFACE_FORMAT (surface) = format;
  GST_VAAPI_SURFACE_CHROMA_TYPE (surface) = chroma_type;
  GST_VAAPI_SURFACE_WIDTH (surface) = extbuf.width;
  GST_VAAPI_SURFACE_HEIGHT (surface) = extbuf.height;

  GST_DEBUG ("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (surface_id));
  GST_VAAPI_SURFACE_ID (surface) = surface_id;
  return TRUE;

  /* ERRORS */
error_unsupported_format:
  GST_ERROR ("unsupported format %s",
      gst_vaapi_video_format_to_string (format));
  return FALSE;
}

static gboolean
gst_vaapi_surface_init_from_buffer_proxy (GstVaapiSurface * surface,
    GstVaapiBufferProxy * proxy, const GstVideoInfo * vip)
{
  GstVaapiDisplay *const display = GST_VAAPI_SURFACE_DISPLAY (surface);
  GstVideoFormat format;
  VASurfaceID surface_id;
  VAStatus status;
  guint chroma_type, va_chroma_format;
  const VAImageFormat *va_format;
  VASurfaceAttrib attribs[2], *attrib;
  VASurfaceAttribExternalBuffers extbuf = { 0, };
  unsigned long extbuf_handle;
  guint i, width, height;

  format = GST_VIDEO_INFO_FORMAT (vip);
  width = GST_VIDEO_INFO_WIDTH (vip);
  height = GST_VIDEO_INFO_HEIGHT (vip);

  gst_vaapi_buffer_proxy_replace (&surface->extbuf_proxy, proxy);

  va_format = gst_vaapi_video_format_to_va_format (format);
  if (!va_format)
    goto error_unsupported_format;

  chroma_type = gst_vaapi_video_format_get_chroma_type (format);
  if (!chroma_type)
    goto error_unsupported_format;

  va_chroma_format = from_GstVaapiChromaType (chroma_type);
  if (!va_chroma_format)
    goto error_unsupported_format;

  extbuf_handle = GST_VAAPI_BUFFER_PROXY_HANDLE (proxy);
  extbuf.pixel_format = va_format->fourcc;
  extbuf.width = width;
  extbuf.height = height;
  extbuf.data_size = GST_VAAPI_BUFFER_PROXY_SIZE (proxy);
  extbuf.num_planes = GST_VIDEO_INFO_N_PLANES (vip);
  for (i = 0; i < extbuf.num_planes; i++) {
    extbuf.pitches[i] = GST_VIDEO_INFO_PLANE_STRIDE (vip, i);
    extbuf.offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET (vip, i);
  }
  extbuf.buffers = (uintptr_t *) & extbuf_handle;
  extbuf.num_buffers = 1;
  extbuf.flags = 0;
  extbuf.private_data = NULL;

  attrib = attribs;
  attrib->type = VASurfaceAttribExternalBufferDescriptor;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypePointer;
  attrib->value.value.p = &extbuf;
  attrib++;
  attrib->type = VASurfaceAttribMemoryType;
  attrib->flags = VA_SURFACE_ATTRIB_SETTABLE;
  attrib->value.type = VAGenericValueTypeInteger;
  attrib->value.value.i =
      from_GstVaapiBufferMemoryType (GST_VAAPI_BUFFER_PROXY_TYPE (proxy));
  attrib++;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateSurfaces (GST_VAAPI_DISPLAY_VADISPLAY (display),
      va_chroma_format, width, height, &surface_id, 1, attribs,
      attrib - attribs);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateSurfaces()"))
    return FALSE;

  GST_VAAPI_SURFACE_FORMAT (surface) = format;
  GST_VAAPI_SURFACE_CHROMA_TYPE (surface) = chroma_type;
  GST_VAAPI_SURFACE_WIDTH (surface) = width;
  GST_VAAPI_SURFACE_HEIGHT (surface) = height;

  GST_DEBUG ("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (surface_id));
  GST_VAAPI_SURFACE_ID (surface) = surface_id;
  return TRUE;

  /* ERRORS */
error_unsupported_format:
  GST_ERROR ("unsupported format %s",
      gst_vaapi_video_format_to_string (format));
  return FALSE;
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiSurface, gst_vaapi_surface);

static GstVaapiSurface *
gst_vaapi_surface_create (GstVaapiDisplay * display)
{
  GstVaapiSurface *surface = g_new (GstVaapiSurface, 1);
  if (!surface)
    return NULL;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (surface), 0,
      GST_TYPE_VAAPI_SURFACE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vaapi_surface_free);

  GST_VAAPI_SURFACE_DISPLAY (surface) = gst_object_ref (display);
  GST_VAAPI_SURFACE_ID (surface) = VA_INVALID_ID;
  surface->extbuf_proxy = NULL;
  surface->subpictures = NULL;

  return surface;
}

/**
 * gst_vaapi_surface_get_display:
 * @surface: a #GstVaapiSurface
 *
 * Returns the #GstVaapiDisplay this @surface is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_surface_get_display (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, NULL);
  return GST_VAAPI_SURFACE_DISPLAY (surface);
}

/**
 * gst_vaapi_surface_new:
 * @display: a #GstVaapiDisplay
 * @chroma_type: the surface chroma format
 * @width: the requested surface width
 * @height: the requested surface height
 *
 * Creates a new #GstVaapiSurface with the specified chroma format and
 * dimensions.
 *
 * NOTE: this method for creating surfaces uses deprecated VA API,
 * which is still used by drivers (v.gr. i965 for jpeg decoding).
 *
 * Return value: the newly allocated #GstVaapiSurface object
 */
GstVaapiSurface *
gst_vaapi_surface_new (GstVaapiDisplay * display,
    GstVaapiChromaType chroma_type, guint width, guint height)
{
  GstVaapiSurface *surface;

  GST_DEBUG ("size %ux%u, chroma type 0x%x", width, height, chroma_type);

  surface = gst_vaapi_surface_create (display);
  if (!surface)
    return NULL;

  if (!gst_vaapi_surface_init (surface, chroma_type, width, height))
    goto error;
  return surface;

  /* ERRORS */
error:
  {
    gst_vaapi_surface_unref (surface);
    return NULL;
  }
}

/**
 * gst_vaapi_surface_new_full:
 * @display: a #GstVaapiDisplay
 * @vip: the pointer to a #GstVideoInfo
 * @surface_allocation_flags: (optional) allocation flags
 *
 * Creates a new #GstVaapiSurface with the specified video information
 * and optional #GstVaapiSurfaceAllocFlags
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation of VA surface with explicit pixel format is not
 *   supported or failed.
 */
GstVaapiSurface *
gst_vaapi_surface_new_full (GstVaapiDisplay * display, const GstVideoInfo * vip,
    guint surface_allocation_flags)
{
  GstVaapiSurface *surface;

  GST_DEBUG ("size %ux%u, format %s, flags 0x%08x", GST_VIDEO_INFO_WIDTH (vip),
      GST_VIDEO_INFO_HEIGHT (vip),
      gst_vaapi_video_format_to_string (GST_VIDEO_INFO_FORMAT (vip)),
      surface_allocation_flags);

  surface = gst_vaapi_surface_create (display);
  if (!surface)
    return NULL;

  if (!gst_vaapi_surface_init_full (surface, vip, surface_allocation_flags))
    goto error;
  return surface;

  /* ERRORS */
error:
  {
    gst_vaapi_surface_unref (surface);
    return NULL;
  }
}

/**
 * gst_vaapi_surface_new_with_format:
 * @display: a #GstVaapiDisplay
 * @format: the surface format
 * @width: the requested surface width
 * @height: the requested surface height
 * @surface_allocation_flags: (optional) allocation flags
 *
 * Creates a new #GstVaapiSurface with the specified pixel format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation of VA surface with explicit pixel format is not
 *   supported or failed.
 */
GstVaapiSurface *
gst_vaapi_surface_new_with_format (GstVaapiDisplay * display,
    GstVideoFormat format, guint width, guint height,
    guint surface_allocation_flags)
{
  GstVideoInfo vi;

  gst_video_info_set_format (&vi, format, width, height);
  return gst_vaapi_surface_new_full (display, &vi, surface_allocation_flags);
}

/**
 * gst_vaapi_surface_new_from_buffer_proxy:
 * @display: a #GstVaapiDisplay
 * @proxy: a #GstVaapiBufferProxy
 * @info: the #GstVideoInfo structure defining the layout of the buffer
 *
 * Creates a new #GstVaapiSurface with the supplied VA buffer proxy
 * abstraction. The underlying VA buffer memory type could be anything
 * that is supported by the VA driver.
 *
 * The resulting #GstVaapiSurface object owns an extra reference to
 * the buffer @proxy, so the caller can safely release that handle as
 * early as on return of this call.
 *
 * Return value: the newly allocated #GstVaapiSurface object, or %NULL
 *   if creation of VA surface failed or is not supported
 */
GstVaapiSurface *
gst_vaapi_surface_new_from_buffer_proxy (GstVaapiDisplay * display,
    GstVaapiBufferProxy * proxy, const GstVideoInfo * info)
{
  GstVaapiSurface *surface;

  g_return_val_if_fail (proxy != NULL, NULL);
  g_return_val_if_fail (info != NULL, NULL);
  g_return_val_if_fail (!proxy->surface, NULL);

  surface = gst_vaapi_surface_create (display);
  if (!surface)
    return NULL;

  if (!gst_vaapi_surface_init_from_buffer_proxy (surface, proxy, info))
    goto error;

  proxy->surface = GST_MINI_OBJECT_CAST (surface);
  return surface;

  /* ERRORS */
error:
  {
    gst_vaapi_surface_unref (surface);
    return NULL;
  }
}

/**
 * gst_vaapi_surface_get_id:
 * @surface: a #GstVaapiSurface
 *
 * Returns the underlying VASurfaceID of the @surface.
 *
 * Return value: the underlying VA surface id
 */
GstVaapiID
gst_vaapi_surface_get_id (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, VA_INVALID_SURFACE);

  return GST_VAAPI_SURFACE_ID (surface);
}

/**
 * gst_vaapi_surface_get_chroma_type:
 * @surface: a #GstVaapiSurface
 *
 * Returns the #GstVaapiChromaType the @surface was created with.
 *
 * Return value: the #GstVaapiChromaType
 */
GstVaapiChromaType
gst_vaapi_surface_get_chroma_type (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, 0);

  return GST_VAAPI_SURFACE_CHROMA_TYPE (surface);
}

/**
 * gst_vaapi_surface_get_format:
 * @surface: a #GstVaapiSurface
 *
 * Returns the #GstVideoFormat the @surface was created with.
 *
 * Return value: the #GstVideoFormat, or %GST_VIDEO_FORMAT_ENCODED if
 *   the surface was not created with an explicit video format, or if
 *   the underlying video format could not be determined
 */
GstVideoFormat
gst_vaapi_surface_get_format (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, 0);

  /* Try to determine the underlying VA surface format */
  if (GST_VAAPI_SURFACE_FORMAT (surface) == GST_VIDEO_FORMAT_UNKNOWN) {
    GstVaapiImage *const image = gst_vaapi_surface_derive_image (surface);
    if (image) {
      GST_VAAPI_SURFACE_FORMAT (surface) = GST_VAAPI_IMAGE_FORMAT (image);
      gst_vaapi_image_unref (image);
    }
    if (GST_VAAPI_SURFACE_FORMAT (surface) == GST_VIDEO_FORMAT_UNKNOWN)
      GST_VAAPI_SURFACE_FORMAT (surface) = GST_VIDEO_FORMAT_ENCODED;
  }
  return GST_VAAPI_SURFACE_FORMAT (surface);
}

/**
 * gst_vaapi_surface_get_width:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface width.
 *
 * Return value: the surface width, in pixels
 */
guint
gst_vaapi_surface_get_width (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, 0);

  return GST_VAAPI_SURFACE_WIDTH (surface);
}

/**
 * gst_vaapi_surface_get_height:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface height.
 *
 * Return value: the surface height, in pixels.
 */
guint
gst_vaapi_surface_get_height (GstVaapiSurface * surface)
{
  g_return_val_if_fail (surface != NULL, 0);

  return GST_VAAPI_SURFACE_HEIGHT (surface);
}

/**
 * gst_vaapi_surface_get_size:
 * @surface: a #GstVaapiSurface
 * @width_ptr: return location for the width, or %NULL
 * @height_ptr: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiSurface.
 */
void
gst_vaapi_surface_get_size (GstVaapiSurface * surface,
    guint * width_ptr, guint * height_ptr)
{
  g_return_if_fail (surface != NULL);

  if (width_ptr)
    *width_ptr = GST_VAAPI_SURFACE_WIDTH (surface);

  if (height_ptr)
    *height_ptr = GST_VAAPI_SURFACE_HEIGHT (surface);
}

/**
 * gst_vaapi_surface_derive_image:
 * @surface: a #GstVaapiSurface
 *
 * Derives a #GstVaapiImage from the @surface. This image buffer can
 * then be mapped/unmapped for direct CPU access. This operation is
 * only possible if the underlying implementation supports direct
 * rendering capabilities and internal surface formats that can be
 * represented with a #GstVaapiImage.
 *
 * When the operation is not possible, the function returns %NULL and
 * the user should then fallback to using gst_vaapi_surface_get_image()
 * or gst_vaapi_surface_put_image() to accomplish the same task in an
 * indirect manner (additional copy).
 *
 * An image created with gst_vaapi_surface_derive_image() should be
 * unreferenced when it's no longer needed. The image and image buffer
 * data structures will be destroyed. However, the surface contents
 * will remain unchanged until destroyed through the last call to
 * gst_vaapi_image_unref().
 *
 * Return value: the newly allocated #GstVaapiImage object, or %NULL
 *   on failure
 */
GstVaapiImage *
gst_vaapi_surface_derive_image (GstVaapiSurface * surface)
{
  GstVaapiDisplay *display;
  VAImage va_image;
  VAStatus status;
  GstVaapiImage *image;

  g_return_val_if_fail (surface != NULL, NULL);

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  va_image.image_id = VA_INVALID_ID;
  va_image.buf = VA_INVALID_ID;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaDeriveImage (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SURFACE_ID (surface), &va_image);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaDeriveImage()"))
    return NULL;
  if (va_image.image_id == VA_INVALID_ID || va_image.buf == VA_INVALID_ID)
    return NULL;

  image = gst_vaapi_image_new_with_image (display, &va_image);
  if (!image)
    vaDestroyImage (GST_VAAPI_DISPLAY_VADISPLAY (display), va_image.image_id);
  return image;
}

/**
 * gst_vaapi_surface_get_image
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Retrieves surface data into a #GstVaapiImage. The @image must have
 * a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_get_image (GstVaapiSurface * surface, GstVaapiImage * image)
{
  GstVaapiDisplay *display;
  VAImageID image_id;
  VAStatus status;
  guint width, height;

  g_return_val_if_fail (surface != NULL, FALSE);
  g_return_val_if_fail (image != NULL, FALSE);

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  width = GST_VAAPI_IMAGE_WIDTH (image);
  height = GST_VAAPI_IMAGE_HEIGHT (image);
  if (width != GST_VAAPI_SURFACE_WIDTH (surface)
      || height != GST_VAAPI_SURFACE_HEIGHT (surface))
    return FALSE;

  image_id = GST_VAAPI_IMAGE_ID (image);
  if (image_id == VA_INVALID_ID)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaGetImage (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SURFACE_ID (surface), 0, 0, width, height, image_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaGetImage()"))
    return FALSE;

  return TRUE;
}

/**
 * gst_vaapi_surface_put_image:
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Copies data from a #GstVaapiImage into a @surface. The @image must
 * have a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_put_image (GstVaapiSurface * surface, GstVaapiImage * image)
{
  GstVaapiDisplay *display;
  VAImageID image_id;
  VAStatus status;
  guint width, height;

  g_return_val_if_fail (surface != NULL, FALSE);
  g_return_val_if_fail (image != NULL, FALSE);

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  width = GST_VAAPI_IMAGE_WIDTH (image);
  height = GST_VAAPI_IMAGE_HEIGHT (image);
  if (width != GST_VAAPI_SURFACE_WIDTH (surface)
      || height != GST_VAAPI_SURFACE_HEIGHT (surface))
    return FALSE;

  image_id = GST_VAAPI_IMAGE_ID (image);
  if (image_id == VA_INVALID_ID)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaPutImage (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SURFACE_ID (surface), image_id, 0, 0, width, height, 0, 0,
      width, height);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaPutImage()"))
    return FALSE;

  return TRUE;
}

/**
 * gst_vaapi_surface_associate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 * @src_rect: the sub-rectangle of the source subpicture
 *   image to extract and process. If %NULL, the entire image will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   surface into which the image is rendered. If %NULL, the entire
 *   surface will be used.
 *
 * Associates the @subpicture with the @surface. The @src_rect
 * coordinates and size are relative to the source image bound to
 * @subpicture. The @dst_rect coordinates and size are relative to the
 * target @surface. Note that the @surface holds an additional
 * reference to the @subpicture.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_associate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture,
    const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect)
{
  gboolean success;

  g_return_val_if_fail (surface != NULL, FALSE);
  g_return_val_if_fail (subpicture != NULL, FALSE);

  if (!surface->subpictures) {
    surface->subpictures = g_ptr_array_new ();
    if (!surface->subpictures)
      return FALSE;
  }

  if (g_ptr_array_remove_fast (surface->subpictures, subpicture)) {
    success = _gst_vaapi_surface_deassociate_subpicture (surface, subpicture);
    gst_vaapi_subpicture_unref (subpicture);
    if (!success)
      return FALSE;
  }

  success = _gst_vaapi_surface_associate_subpicture (surface,
      subpicture, src_rect, dst_rect);
  if (!success)
    return FALSE;

  g_ptr_array_add (surface->subpictures,
      gst_mini_object_ref (GST_MINI_OBJECT_CAST (subpicture)));
  return TRUE;
}

gboolean
_gst_vaapi_surface_associate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture,
    const GstVaapiRectangle * src_rect, const GstVaapiRectangle * dst_rect)
{
  GstVaapiDisplay *display;
  GstVaapiRectangle src_rect_default, dst_rect_default;
  GstVaapiImage *image;
  VASurfaceID surface_id;
  VAStatus status;

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  surface_id = GST_VAAPI_SURFACE_ID (surface);
  if (surface_id == VA_INVALID_SURFACE)
    return FALSE;

  if (!src_rect) {
    image = gst_vaapi_subpicture_get_image (subpicture);
    if (!image)
      return FALSE;
    src_rect = &src_rect_default;
    src_rect_default.x = 0;
    src_rect_default.y = 0;
    src_rect_default.width = GST_VAAPI_IMAGE_WIDTH (image);
    src_rect_default.height = GST_VAAPI_IMAGE_HEIGHT (image);
  }

  if (!dst_rect) {
    dst_rect = &dst_rect_default;
    dst_rect_default.x = 0;
    dst_rect_default.y = 0;
    dst_rect_default.width = GST_VAAPI_SURFACE_WIDTH (surface);
    dst_rect_default.height = GST_VAAPI_SURFACE_HEIGHT (surface);
  }

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaAssociateSubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SUBPICTURE_ID (subpicture), &surface_id, 1,
      src_rect->x, src_rect->y, src_rect->width, src_rect->height,
      dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
      from_GstVaapiSubpictureFlags (gst_vaapi_subpicture_get_flags
          (subpicture)));
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaAssociateSubpicture()"))
    return FALSE;

  return TRUE;
}

/**
 * gst_vaapi_surface_deassociate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 *
 * Deassociates @subpicture from @surface. Other associations are kept.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_deassociate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture)
{
  gboolean success;

  g_return_val_if_fail (surface != NULL, FALSE);
  g_return_val_if_fail (subpicture != NULL, FALSE);

  if (!surface->subpictures)
    return TRUE;

  /* First, check subpicture was really associated with this surface */
  if (!g_ptr_array_remove_fast (surface->subpictures, subpicture)) {
    GST_DEBUG ("subpicture %" GST_VAAPI_ID_FORMAT " was not bound to "
        "surface %" GST_VAAPI_ID_FORMAT,
        GST_VAAPI_ID_ARGS (GST_VAAPI_SUBPICTURE_ID (subpicture)),
        GST_VAAPI_ID_ARGS (GST_VAAPI_SURFACE_ID (surface)));
    return TRUE;
  }

  success = _gst_vaapi_surface_deassociate_subpicture (surface, subpicture);
  gst_vaapi_subpicture_unref (subpicture);
  return success;
}

gboolean
_gst_vaapi_surface_deassociate_subpicture (GstVaapiSurface * surface,
    GstVaapiSubpicture * subpicture)
{
  GstVaapiDisplay *display;
  VASurfaceID surface_id;
  VAStatus status;

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  surface_id = GST_VAAPI_SURFACE_ID (surface);
  if (surface_id == VA_INVALID_SURFACE)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaDeassociateSubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SUBPICTURE_ID (subpicture), &surface_id, 1);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaDeassociateSubpicture()"))
    return FALSE;

  return TRUE;
}

/**
 * gst_vaapi_surface_sync:
 * @surface: a #GstVaapiSurface
 *
 * Blocks until all pending operations on the @surface have been
 * completed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_sync (GstVaapiSurface * surface)
{
  GstVaapiDisplay *display;
  VAStatus status;

  g_return_val_if_fail (surface != NULL, FALSE);

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaSyncSurface (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SURFACE_ID (surface));
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaSyncSurface()"))
    return FALSE;

  return TRUE;
}

/**
 * gst_vaapi_surface_query_status:
 * @surface: a #GstVaapiSurface
 * @pstatus: return location for the #GstVaapiSurfaceStatus
 *
 * Finds out any pending operations on the @surface. The
 * #GstVaapiSurfaceStatus flags are returned into @pstatus.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_query_status (GstVaapiSurface * surface,
    GstVaapiSurfaceStatus * pstatus)
{
  GstVaapiDisplay *const display = GST_VAAPI_SURFACE_DISPLAY (surface);
  VASurfaceStatus surface_status;
  VAStatus status;

  g_return_val_if_fail (surface != NULL, FALSE);

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaQuerySurfaceStatus (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_SURFACE_ID (surface), &surface_status);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaQuerySurfaceStatus()"))
    return FALSE;

  if (pstatus)
    *pstatus = to_GstVaapiSurfaceStatus (surface_status);
  return TRUE;
}

/**
 * gst_vaapi_surface_set_subpictures_from_composition:
 * @surface: a #GstVaapiSurface
 * @compostion: a #GstVideoOverlayCompositon
 *
 * Helper to update the subpictures from #GstVideoOverlayCompositon. Sending
 * a NULL composition will clear all the current subpictures. Note that this
 * method will clear existing subpictures.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_set_subpictures_from_composition (GstVaapiSurface * surface,
    GstVideoOverlayComposition * composition)
{
  GstVaapiDisplay *display;
  guint n, nb_rectangles;

  g_return_val_if_fail (surface != NULL, FALSE);

  display = GST_VAAPI_SURFACE_DISPLAY (surface);
  if (!display)
    return FALSE;

  /* Clear current subpictures */
  gst_vaapi_surface_destroy_subpictures (surface);

  if (!composition)
    return TRUE;

  nb_rectangles = gst_video_overlay_composition_n_rectangles (composition);

  /* Overlay all the rectangles cantained in the overlay composition */
  for (n = 0; n < nb_rectangles; ++n) {
    GstVideoOverlayRectangle *rect;
    GstVaapiRectangle sub_rect;
    GstVaapiSubpicture *subpicture;

    rect = gst_video_overlay_composition_get_rectangle (composition, n);
    subpicture = gst_vaapi_subpicture_new_from_overlay_rectangle (display,
        rect);
    if (subpicture == NULL) {
      GST_WARNING ("could not create subpicture for rectangle %p", rect);
      return FALSE;
    }

    gst_video_overlay_rectangle_get_render_rectangle (rect,
        (gint *) & sub_rect.x, (gint *) & sub_rect.y,
        &sub_rect.width, &sub_rect.height);

    /* ensure that the overlay is not bigger than the surface */
    sub_rect.y = MIN (sub_rect.y, GST_VAAPI_SURFACE_HEIGHT (surface));
    sub_rect.width = MIN (sub_rect.width, GST_VAAPI_SURFACE_WIDTH (surface));

    if (!gst_vaapi_surface_associate_subpicture (surface, subpicture,
            NULL, &sub_rect)) {
      GST_WARNING ("could not render overlay rectangle %p", rect);
      gst_vaapi_subpicture_unref (subpicture);
      return FALSE;
    }
    gst_vaapi_subpicture_unref (subpicture);
  }
  return TRUE;
}

/**
 * gst_vaapi_surface_set_buffer_proxy:
 * @surface: a #GstVaapiSurface
 * @proxy: an external #GstVaapiBufferProxy
 *
 * Replaces the external buffer proxy in @surface with @proxy.
 *
 * This is useful when a dmabuf-based memory is instantiated in order
 * to relate the generated buffer @proxy with the processed @surface.
 **/
void
gst_vaapi_surface_set_buffer_proxy (GstVaapiSurface * surface,
    GstVaapiBufferProxy * proxy)
{
  gst_vaapi_buffer_proxy_replace (&surface->extbuf_proxy, proxy);
}

/**
 * gst_vaapi_surface_peek_buffer_proxy:
 * @surface: a #GstVaapiSurface
 *
 * This is useful when a dmabuf-based memory is instantiated in order
 * to relate the generated buffer @proxy with the processed @surface.
 *
 * Returns: (transfer none): the associated external
 * #GstVaapiBufferProxy
 **/
GstVaapiBufferProxy *
gst_vaapi_surface_peek_buffer_proxy (GstVaapiSurface * surface)
{
  return surface->extbuf_proxy;
}
