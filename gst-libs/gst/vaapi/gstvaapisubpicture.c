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
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisubpicture.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapiimage_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiSubpictureClass GstVaapiSubpictureClass;

/**
 * GstVaapiSubpicture:
 *
 * A VA subpicture wrapper
 */
struct _GstVaapiSubpicture
{
  /*< private > */
  GstVaapiObject parent_instance;

  GstVaapiImage *image;
  guint flags;
  gfloat global_alpha;
};

/**
 * GstVaapiSubpictureClass:
 *
 * A VA subpicture wrapper class
 */
struct _GstVaapiSubpictureClass
{
  /*< private > */
  GstVaapiObjectClass parent_class;
};

static void
gst_vaapi_subpicture_destroy (GstVaapiSubpicture * subpicture)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (subpicture);
  VASubpictureID subpicture_id;
  VAStatus status;

  subpicture_id = GST_VAAPI_OBJECT_ID (subpicture);
  GST_DEBUG ("subpicture %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (subpicture_id));

  if (subpicture_id != VA_INVALID_ID) {
    if (display) {
      GST_VAAPI_DISPLAY_LOCK (display);
      status = vaDestroySubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
          subpicture_id);
      GST_VAAPI_DISPLAY_UNLOCK (display);
      if (!vaapi_check_status (status, "vaDestroySubpicture()"))
        g_warning ("failed to destroy subpicture %" GST_VAAPI_ID_FORMAT,
            GST_VAAPI_ID_ARGS (subpicture_id));
    }
    GST_VAAPI_OBJECT_ID (subpicture) = VA_INVALID_ID;
  }
  gst_vaapi_object_replace (&subpicture->image, NULL);
}

static gboolean
gst_vaapi_subpicture_create (GstVaapiSubpicture * subpicture,
    GstVaapiImage * image)
{
  GstVaapiDisplay *const display = GST_VAAPI_OBJECT_DISPLAY (subpicture);
  VASubpictureID subpicture_id;
  VAStatus status;

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaCreateSubpicture (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_OBJECT_ID (image), &subpicture_id);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaCreateSubpicture()"))
    return FALSE;

  GST_DEBUG ("subpicture %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (subpicture_id));
  GST_VAAPI_OBJECT_ID (subpicture) = subpicture_id;
  subpicture->image = gst_vaapi_object_ref (image);
  return TRUE;
}

#define gst_vaapi_subpicture_finalize gst_vaapi_subpicture_destroy
GST_VAAPI_OBJECT_DEFINE_CLASS (GstVaapiSubpicture, gst_vaapi_subpicture)

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
     GstVaapiSubpicture *gst_vaapi_subpicture_new (GstVaapiImage * image,
    guint flags)
{
  GstVaapiSubpicture *subpicture;
  GstVaapiDisplay *display;
  GstVideoFormat format;
  guint va_flags;

  g_return_val_if_fail (image != NULL, NULL);

  GST_DEBUG ("create from image %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (GST_VAAPI_OBJECT_ID (image)));

  display = GST_VAAPI_OBJECT_DISPLAY (image);
  format = GST_VAAPI_IMAGE_FORMAT (image);
  if (!gst_vaapi_display_has_subpicture_format (display, format, &va_flags))
    return NULL;
  if (flags & ~va_flags)
    return NULL;

  subpicture = gst_vaapi_object_new (gst_vaapi_subpicture_class (), display);
  if (!subpicture)
    return NULL;

  subpicture->global_alpha = 1.0f;
  if (!gst_vaapi_subpicture_set_image (subpicture, image))
    goto error;
  return subpicture;

  /* ERRORS */
error:
  {
    gst_vaapi_object_unref (subpicture);
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

  /* XXX: use gst_vaapi_image_format_from_video() */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  format = GST_VIDEO_FORMAT_BGRA;
#else
  format = GST_VIDEO_FORMAT_ARGB;
#endif
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
    gst_vaapi_object_unref (image);
    return NULL;
  }

  subpicture = gst_vaapi_subpicture_new (image, flags);
  gst_vaapi_object_unref (image);
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

  return GST_VAAPI_OBJECT_ID (subpicture);
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

  gst_vaapi_subpicture_destroy (subpicture);
  return gst_vaapi_subpicture_create (subpicture, image);
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

  display = GST_VAAPI_OBJECT_DISPLAY (subpicture);

  GST_VAAPI_DISPLAY_LOCK (display);
  status = vaSetSubpictureGlobalAlpha (GST_VAAPI_DISPLAY_VADISPLAY (display),
      GST_VAAPI_OBJECT_ID (subpicture), global_alpha);
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!vaapi_check_status (status, "vaSetSubpictureGlobalAlpha()"))
    return FALSE;

  subpicture->global_alpha = global_alpha;
  return TRUE;
}
