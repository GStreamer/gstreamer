/*
 *  gstvaapipixmap.c - Pixmap abstraction
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

/**
 * SECTION:gstvaapipixmap
 * @short_description: Pixmap abstraction
 */

#include "sysdeps.h"
#include "gstvaapipixmap.h"
#include "gstvaapipixmap_priv.h"
#include "gstvaapisurface_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Ensure those symbols are actually defined in the resulting libraries */
#undef gst_vaapi_pixmap_ref
#undef gst_vaapi_pixmap_unref
#undef gst_vaapi_pixmap_replace

static inline GstVaapiPixmap *
gst_vaapi_pixmap_new_internal (const GstVaapiPixmapClass * pixmap_class,
    GstVaapiDisplay * display)
{
  g_assert (pixmap_class->create != NULL);
  g_assert (pixmap_class->render != NULL);

  return gst_vaapi_object_new (GST_VAAPI_OBJECT_CLASS (pixmap_class), display);
}

GstVaapiPixmap *
gst_vaapi_pixmap_new (const GstVaapiPixmapClass * pixmap_class,
    GstVaapiDisplay * display, GstVideoFormat format, guint width, guint height)
{
  GstVaapiPixmap *pixmap;

  g_return_val_if_fail (format != GST_VIDEO_FORMAT_UNKNOWN &&
      format != GST_VIDEO_FORMAT_ENCODED, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  pixmap = gst_vaapi_pixmap_new_internal (pixmap_class, display);
  if (!pixmap)
    return NULL;

  pixmap->format = format;
  pixmap->width = width;
  pixmap->height = height;
  if (!pixmap_class->create (pixmap))
    goto error;
  return pixmap;

  /* ERRORS */
error:
  {
    gst_vaapi_pixmap_unref_internal (pixmap);
    return NULL;
  }
}

GstVaapiPixmap *
gst_vaapi_pixmap_new_from_native (const GstVaapiPixmapClass * pixmap_class,
    GstVaapiDisplay * display, gpointer native_pixmap)
{
  GstVaapiPixmap *pixmap;

  pixmap = gst_vaapi_pixmap_new_internal (pixmap_class, display);
  if (!pixmap)
    return NULL;

  GST_VAAPI_OBJECT_ID (pixmap) = GPOINTER_TO_SIZE (native_pixmap);
  pixmap->use_foreign_pixmap = TRUE;
  if (!pixmap_class->create (pixmap))
    goto error;
  return pixmap;

  /* ERRORS */
error:
  {
    gst_vaapi_pixmap_unref_internal (pixmap);
    return NULL;
  }
}

/**
 * gst_vaapi_pixmap_ref:
 * @pixmap: a #GstVaapiPixmap
 *
 * Atomically increases the reference count of the given @pixmap by one.
 *
 * Returns: The same @pixmap argument
 */
GstVaapiPixmap *
gst_vaapi_pixmap_ref (GstVaapiPixmap * pixmap)
{
  return gst_vaapi_pixmap_ref_internal (pixmap);
}

/**
 * gst_vaapi_pixmap_unref:
 * @pixmap: a #GstVaapiPixmap
 *
 * Atomically decreases the reference count of the @pixmap by one. If
 * the reference count reaches zero, the pixmap will be free'd.
 */
void
gst_vaapi_pixmap_unref (GstVaapiPixmap * pixmap)
{
  gst_vaapi_pixmap_unref_internal (pixmap);
}

/**
 * gst_vaapi_pixmap_replace:
 * @old_pixmap_ptr: a pointer to a #GstVaapiPixmap
 * @new_pixmap: a #GstVaapiPixmap
 *
 * Atomically replaces the pixmap pixmap held in @old_pixmap_ptr with
 * @new_pixmap. This means that @old_pixmap_ptr shall reference a
 * valid pixmap. However, @new_pixmap can be NULL.
 */
void
gst_vaapi_pixmap_replace (GstVaapiPixmap ** old_pixmap_ptr,
    GstVaapiPixmap * new_pixmap)
{
  gst_vaapi_pixmap_replace_internal (old_pixmap_ptr, new_pixmap);
}

/**
 * gst_vaapi_pixmap_get_display:
 * @pixmap: a #GstVaapiPixmap
 *
 * Returns the #GstVaapiDisplay this @pixmap is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_pixmap_get_display (GstVaapiPixmap * pixmap)
{
  g_return_val_if_fail (pixmap != NULL, NULL);

  return GST_VAAPI_OBJECT_DISPLAY (pixmap);
}

/**
 * gst_vaapi_pixmap_get_format:
 * @pixmap: a #GstVaapiPixmap
 *
 * Retrieves the format of a #GstVaapiPixmap.
 *
 * Return value: the format of the @pixmap
 */
GstVideoFormat
gst_vaapi_pixmap_get_format (GstVaapiPixmap * pixmap)
{
  g_return_val_if_fail (pixmap != NULL, GST_VIDEO_FORMAT_UNKNOWN);

  return GST_VAAPI_PIXMAP_FORMAT (pixmap);
}

/**
 * gst_vaapi_pixmap_get_width:
 * @pixmap: a #GstVaapiPixmap
 *
 * Retrieves the width of a #GstVaapiPixmap.
 *
 * Return value: the width of the @pixmap, in pixels
 */
guint
gst_vaapi_pixmap_get_width (GstVaapiPixmap * pixmap)
{
  g_return_val_if_fail (pixmap != NULL, 0);

  return GST_VAAPI_PIXMAP_WIDTH (pixmap);
}

/**
 * gst_vaapi_pixmap_get_height:
 * @pixmap: a #GstVaapiPixmap
 *
 * Retrieves the height of a #GstVaapiPixmap
 *
 * Return value: the height of the @pixmap, in pixels
 */
guint
gst_vaapi_pixmap_get_height (GstVaapiPixmap * pixmap)
{
  g_return_val_if_fail (pixmap != NULL, 0);

  return GST_VAAPI_PIXMAP_HEIGHT (pixmap);
}

/**
 * gst_vaapi_pixmap_get_size:
 * @pixmap: a #GstVaapiPixmap
 * @width: return location for the width, or %NULL
 * @height: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiPixmap.
 */
void
gst_vaapi_pixmap_get_size (GstVaapiPixmap * pixmap, guint * width,
    guint * height)
{
  g_return_if_fail (pixmap != NULL);

  if (width)
    *width = GST_VAAPI_PIXMAP_WIDTH (pixmap);

  if (height)
    *height = GST_VAAPI_PIXMAP_HEIGHT (pixmap);
}

/**
 * gst_vaapi_pixmap_put_surface:
 * @pixmap: a #GstVaapiPixmap
 * @surface: a #GstVaapiSurface
 * @crop_rect: the video cropping rectangle, or %NULL if the entire
 *   surface is to be used.
 * @flags: postprocessing flags. See #GstVaapiSurfaceRenderFlags
 *
 * Renders the whole @surface, or a cropped region defined with
 * @crop_rect, into the @pixmap, while scaling to fit the target
 * pixmap. The @flags specify how de-interlacing (if needed), color
 * space conversion, scaling and other postprocessing transformations
 * are performed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_pixmap_put_surface (GstVaapiPixmap * pixmap,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  GstVaapiRectangle src_rect;

  g_return_val_if_fail (pixmap != NULL, FALSE);
  g_return_val_if_fail (surface != NULL, FALSE);

  if (!crop_rect) {
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.width = GST_VAAPI_SURFACE_WIDTH (surface);
    src_rect.height = GST_VAAPI_SURFACE_HEIGHT (surface);
    crop_rect = &src_rect;
  }
  return GST_VAAPI_PIXMAP_GET_CLASS (pixmap)->render (pixmap, surface,
      crop_rect, flags);
}
