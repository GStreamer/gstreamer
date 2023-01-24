/*
 *  gstvaapisubpicture.c - VA subpicture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
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
 * SECTION:gstvaapisubpicture
 * @short_description: VA subpicture abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisubpicture.h"
#include "gstvaapiimage_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * GstVaapiSubpicture:
 *
 * A VA subpicture wrapper
 */
struct _GstVaapiSubpicture
{
  /*< private > */
  GstMiniObject mini_object;
  GstVaapiDisplay *display;
  GstVaapiID object_id;

  GstVaapiImage *image;
  guint flags;
  gfloat global_alpha;
};

static void
gst_vaapi_subpicture_free_image (GstVaapiSubpicture * subpicture)
{
  GstVaapiDisplay *const display = subpicture->display;
  VASubpictureID subpicture_id;
  VAStatus status;

  subpicture_id = subpicture->object_id;
  GST_DEBUG ("subpicture %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (subpicture_id));

  if (subpicture_id != VA_INVALID_ID) {
    GST_VAAPI_DISPLAY_LOCK (display);
    status = vaDestroySubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
        subpicture_id);
    GST_VAAPI_DISPLAY_UNLOCK (display);
    if (!vaapi_check_status (status, "vaDestroySubpicture()"))
      GST_WARNING ("failed to destroy subpicture %" GST_VAAPI_ID_FORMAT,
          GST_VAAPI_ID_ARGS (subpicture_id));
    subpicture->object_id = VA_INVALID_ID;
  }

  if (subpicture->image)
    gst_mini_object_replace ((GstMiniObject **) & subpicture->image, NULL);
}

static void
gst_vaapi_subpicture_free (GstVaapiSubpicture * subpicture)
{
  gst_vaapi_subpicture_free_image (subpicture);
  gst_vaapi_display_replace (&subpicture->display, NULL);
  g_free (subpicture);
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiSubpicture, gst_vaapi_subpicture);

static gboolean
gst_vaapi_subpicture_bind_image (GstVaapiSubpicture * subpicture,
    GstVaapiImage * image)
{
  GstVaapiDisplay *const display = subpicture->display;
  VASubpictureID subpicture_id;
  VAStatus status;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateSubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_IMAGE_ID (image), &subpicture_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateSubpicture()"))
    return FALSE;

  GST_DEBUG ("subpicture %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (subpicture_id));
  subpicture->object_id = subpicture_id;
  subpicture->image =
      (GstVaapiImage *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (image));
  return TRUE;
}

/**
 * gst_vaapi_subpicture_new:
 * @image: a #GstVaapiImage
 * @flags: #GstVaapiSubpictureFlags, or zero
 *
 * Creates a new #GstVaapiSubpicture with @image as source pixels. The
 * newly created object holds a reference on @image.
 *
 * Return value: the newly allocated #GstVaapiSubpicture object
 */
GstVaapiSubpicture *
gst_vaapi_subpicture_new (GstVaapiImage * image, guint flags)
{
  GstVaapiSubpicture *subpicture;
  GstVaapiDisplay *display;
  GstVideoFormat format;
  guint va_flags;

  g_return_val_if_fail (image != NULL, NULL);

  GST_DEBUG ("create from image %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (GST_VAAPI_IMAGE_ID (image)));

  display = GST_VAAPI_IMAGE_DISPLAY (image);
  format = GST_VAAPI_IMAGE_FORMAT (image);
  if (!gst_vaapi_display_has_subpicture_format (display, format, &va_flags))
    return NULL;
  if (flags & ~va_flags)
    return NULL;

  subpicture = g_new (GstVaapiSubpicture, 1);
  if (!subpicture)
    return NULL;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (subpicture), 0,
      GST_TYPE_VAAPI_SUBPICTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vaapi_subpicture_free);
  subpicture->display = gst_object_ref (display);
  subpicture->object_id = VA_INVALID_ID;
  subpicture->flags = flags;
  subpicture->global_alpha = 1.0f;

  if (!gst_vaapi_subpicture_bind_image (subpicture, image))
    goto error;
  return subpicture;

  /* ERRORS */
error:
  {
    gst_vaapi_subpicture_unref (subpicture);
    return NULL;
  }
}

/**
 * gst_vaapi_subpicture_new_from_overlay_rectangle:
 * @display: a #GstVaapiDisplay
 * @rect: a #GstVideoOverlayRectangle
 *
 * Helper function that creates a new #GstVaapiSubpicture from a
 * #GstVideoOverlayRectangle. A new #GstVaapiImage is also created
 * along the way and attached to the resulting subpicture. The
 * subpicture holds a unique reference to the underlying image.
 *
 * Return value: the newly allocated #GstVaapiSubpicture object
 */
GstVaapiSubpicture *
gst_vaapi_subpicture_new_from_overlay_rectangle (GstVaapiDisplay * display,
    GstVideoOverlayRectangle * rect)
{
  GstVaapiSubpicture *subpicture;
  GstVideoFormat format;
  GstVaapiImage *image;
  GstVaapiImageRaw raw_image;
  GstBuffer *buffer;
  guint8 *data;
  gfloat global_alpha;
  guint width, height, stride;
  guint hw_flags, flags;
  GstVideoMeta *vmeta;
  GstMapInfo map_info;

  g_return_val_if_fail (GST_IS_VIDEO_OVERLAY_RECTANGLE (rect), NULL);

  format = GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB;

  if (!gst_vaapi_display_has_subpicture_format (display, format, &hw_flags))
    return NULL;

  flags =
      hw_flags &
      from_GstVideoOverlayFormatFlags (gst_video_overlay_rectangle_get_flags
      (rect));

  buffer = gst_video_overlay_rectangle_get_pixels_unscaled_argb (rect,
      to_GstVideoOverlayFormatFlags (flags));
  if (!buffer)
    return NULL;

  vmeta = gst_buffer_get_video_meta (buffer);
  if (!vmeta)
    return NULL;
  width = vmeta->width;
  height = vmeta->height;

  if (!gst_video_meta_map (vmeta, 0, &map_info, (gpointer *) & data,
          (gint *) & stride, GST_MAP_READ))
    return NULL;

  image = gst_vaapi_image_new (display, format, width, height);
  if (!image)
    return NULL;

  raw_image.format = format;
  raw_image.width = width;
  raw_image.height = height;
  raw_image.num_planes = 1;
  raw_image.pixels[0] = data;
  raw_image.stride[0] = stride;
  if (!gst_vaapi_image_update_from_raw (image, &raw_image, NULL)) {
    GST_WARNING ("could not update VA image with subtitle data");
    gst_vaapi_image_unref (image);
    return NULL;
  }

  subpicture = gst_vaapi_subpicture_new (image, flags);
  gst_vaapi_image_unref (image);
  gst_video_meta_unmap (vmeta, 0, &map_info);
  if (!subpicture)
    return NULL;

  if (flags & GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA) {
    global_alpha = gst_video_overlay_rectangle_get_global_alpha (rect);
    if (!gst_vaapi_subpicture_set_global_alpha (subpicture, global_alpha))
      return NULL;
  }
  return subpicture;
}

/**
 * gst_vaapi_subpicture_get_id:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the underlying VASubpictureID of the @subpicture.
 *
 * Return value: the underlying VA subpicture id
 */
GstVaapiID
gst_vaapi_subpicture_get_id (GstVaapiSubpicture * subpicture)
{
  g_return_val_if_fail (subpicture != NULL, VA_INVALID_ID);

  return subpicture->object_id;
}

/**
 * gst_vaapi_subpicture_get_flags:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the @subpicture flags.
 *
 * Return value: the @subpicture flags
 */
guint
gst_vaapi_subpicture_get_flags (GstVaapiSubpicture * subpicture)
{
  g_return_val_if_fail (subpicture != NULL, 0);

  return subpicture->flags;
}

/**
 * gst_vaapi_subpicture_get_image:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the #GstVaapiImage this @subpicture is bound to.
 *
 * Return value: the #GstVaapiImage this @subpicture is bound to
 */
GstVaapiImage *
gst_vaapi_subpicture_get_image (GstVaapiSubpicture * subpicture)
{
  g_return_val_if_fail (subpicture != NULL, NULL);

  return subpicture->image;
}

/**
 * gst_vaapi_subpicture_set_image:
 * @subpicture: a #GstVaapiSubpicture
 * @image: a #GstVaapiImage
 *
 * Binds a new #GstVaapiImage to the @subpicture. The reference to the
 * previous image is released and a new one is acquired on @image.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_subpicture_set_image (GstVaapiSubpicture * subpicture,
    GstVaapiImage * image)
{
  g_return_val_if_fail (subpicture != NULL, FALSE);
  g_return_val_if_fail (image != NULL, FALSE);

  gst_vaapi_subpicture_free_image (subpicture);
  return gst_vaapi_subpicture_bind_image (subpicture, image);
}

/**
 * gst_vaapi_subpicture_get_global_alpha:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the value of global_alpha, set for this @subpicture.
 *
 * Return value: the global_alpha value of this @subpicture
 */
gfloat
gst_vaapi_subpicture_get_global_alpha (GstVaapiSubpicture * subpicture)
{
  g_return_val_if_fail (subpicture != NULL, 1.0);

  return subpicture->global_alpha;
}

/**
 * gst_vaapi_subpicture_set_global_alpha:
 * @subpicture: a #GstVaapiSubpicture
 * @global_alpha: value for global-alpha (range: 0.0 to 1.0, inclusive)
 *
 * Sets the global_alpha value of @subpicture. This function calls
 * vaSetSubpictureGlobalAlpha() if the format of @subpicture, i.e.
 * the current VA driver supports it.
 *
 * Return value: %TRUE if global_alpha could be set, %FALSE otherwise
 */
gboolean
gst_vaapi_subpicture_set_global_alpha (GstVaapiSubpicture * subpicture,
    gfloat global_alpha)
{
  GstVaapiDisplay *display;
  VAStatus status;

  g_return_val_if_fail (subpicture != NULL, FALSE);

  if (!(subpicture->flags & GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA))
    return FALSE;

  if (subpicture->global_alpha == global_alpha)
    return TRUE;

  display = subpicture->display;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaSetSubpictureGlobalAlpha (GST_VAAPI_DISPLAY_VADISPLAY (display),
      subpicture->object_id, global_alpha);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaSetSubpictureGlobalAlpha()"))
    return FALSE;

  subpicture->global_alpha = global_alpha;
  return TRUE;
}
