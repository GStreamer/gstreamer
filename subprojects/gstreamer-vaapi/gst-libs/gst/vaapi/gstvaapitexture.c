/*
 *  gstvaapitexture.c - VA texture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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
 * SECTION:gstvaapitexture
 * @short_description: VA/GLX texture abstraction
 */

#include "sysdeps.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapitexture.h"
#include "gstvaapitexture_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_TEXTURE_ORIENTATION_FLAGS \
  (GST_VAAPI_TEXTURE_ORIENTATION_FLAG_X_INVERTED | \
   GST_VAAPI_TEXTURE_ORIENTATION_FLAG_Y_INVERTED)

#define GST_VAAPI_TEXTURE_PRIVATE_QUARK gst_vaapi_texture_private_quark ()
static GQuark
gst_vaapi_texture_private_quark (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("GstVaapiTexturePrivate");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

gpointer
gst_vaapi_texture_get_private (GstVaapiTexture * texture)
{
  return gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (texture),
      GST_VAAPI_TEXTURE_PRIVATE_QUARK);
}

void
gst_vaapi_texture_set_private (GstVaapiTexture * texture, gpointer priv,
    GDestroyNotify destroy)
{
  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (texture),
      GST_VAAPI_TEXTURE_PRIVATE_QUARK, (gpointer) priv, destroy);
}

static void
gst_vaapi_texture_init (GstVaapiTexture * texture, GstVaapiID id,
    guint target, guint format, guint width, guint height)
{
  texture->is_wrapped = id != GST_VAAPI_ID_INVALID;
  GST_VAAPI_TEXTURE_ID (texture) = texture->is_wrapped ? id : 0;
  texture->gl_target = target;
  texture->gl_format = format;
  texture->width = width;
  texture->height = height;
}

static void
gst_vaapi_texture_free (GstVaapiTexture * texture)
{
  gst_vaapi_display_replace (&GST_VAAPI_TEXTURE_DISPLAY (texture), NULL);
  g_free (texture);
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVaapiTexture, gst_vaapi_texture);

GstVaapiTexture *
gst_vaapi_texture_new_internal (GstVaapiDisplay * display, GstVaapiID id,
    guint target, guint format, guint width, guint height)
{
  GstVaapiTexture *texture;

  g_return_val_if_fail (display, NULL);
  g_return_val_if_fail (target != 0, NULL);
  g_return_val_if_fail (format != 0, NULL);
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  texture = g_new (GstVaapiTexture, 1);
  if (!texture)
    return NULL;

  gst_mini_object_init (GST_MINI_OBJECT_CAST (texture), 0,
      GST_TYPE_VAAPI_TEXTURE, NULL, NULL,
      (GstMiniObjectFreeFunction) gst_vaapi_texture_free);

  GST_VAAPI_TEXTURE_DISPLAY (texture) = gst_object_ref (display);

  gst_vaapi_texture_init (texture, id, target, format, width, height);

  return texture;
}

/**
 * gst_vaapi_texture_new:
 * @display: a #GstVaapiDisplay
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions, @target and
 * @format. Note that only GL_TEXTURE_2D @target and GL_RGBA or
 * GL_BGRA formats are supported at this time.
 *
 * The application shall maintain the live GL context itself.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_new (GstVaapiDisplay * display, guint target, guint format,
    guint width, guint height)
{
  GstVaapiDisplayClass *dpy_class;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (gst_vaapi_display_has_opengl (display), NULL);

  dpy_class = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (G_UNLIKELY (!dpy_class->create_texture))
    return NULL;
  return dpy_class->create_texture (display, GST_VAAPI_ID_INVALID, target,
      format, width, height);
}

/**
 * gst_vaapi_texture_new_wrapped:
 * @display: a #GstVaapiDisplay
 * @texture_id: the foreign GL texture name to use
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the suggested width, in pixels
 * @height: the suggested height, in pixels
 *
 * Creates a texture with the specified dimensions, @target and
 * @format. Note that only GL_TEXTURE_2D @target and GL_RGBA or
 * GL_BGRA formats are supported at this time.
 *
 * The size arguments @width and @height are only a suggestion. Should
 * this be 0x0, then the actual size of the allocated texture storage
 * would be either inherited from the original texture storage, if any
 * and/or if possible, or derived from the VA surface in subsequent
 * gst_vaapi_texture_put_surface() calls.
 *
 * The application shall maintain the live GL context itself.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_new_wrapped (GstVaapiDisplay * display, guint id,
    guint target, guint format, guint width, guint height)
{
  GstVaapiDisplayClass *dpy_class;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (gst_vaapi_display_has_opengl (display), NULL);

  dpy_class = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (G_UNLIKELY (!dpy_class->create_texture))
    return NULL;
  return dpy_class->create_texture (display, id, target, format, width, height);
}

/**
 * gst_vaapi_texture_get_id:
 * @texture: a #GstVaapiTexture
 *
 * Returns the underlying texture id of the @texture.
 *
 * Return value: the underlying texture id of the @texture
 */
guint
gst_vaapi_texture_get_id (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_VAAPI_TEXTURE_ID (texture);
}

/**
 * gst_vaapi_texture_get_target:
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture target type
 *
 * Return value: the texture target
 */
guint
gst_vaapi_texture_get_target (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_VAAPI_TEXTURE_TARGET (texture);
}

/**
 * gst_vaapi_texture_get_format
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture format
 *
 * Return value: the texture format
 */
guint
gst_vaapi_texture_get_format (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_VAAPI_TEXTURE_FORMAT (texture);
}

/**
 * gst_vaapi_texture_get_width:
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture width.
 *
 * Return value: the texture width, in pixels
 */
guint
gst_vaapi_texture_get_width (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_VAAPI_TEXTURE_WIDTH (texture);
}

/**
 * gst_vaapi_texture_get_height:
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture height.
 *
 * Return value: the texture height, in pixels.
 */
guint
gst_vaapi_texture_get_height (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_VAAPI_TEXTURE_HEIGHT (texture);
}

/**
 * gst_vaapi_texture_get_size:
 * @texture: a #GstVaapiTexture
 * @width_ptr: return location for the width, or %NULL
 * @height_ptr: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiTexture.
 */
void
gst_vaapi_texture_get_size (GstVaapiTexture * texture,
    guint * width_ptr, guint * height_ptr)
{
  g_return_if_fail (texture != NULL);

  if (width_ptr)
    *width_ptr = GST_VAAPI_TEXTURE_WIDTH (texture);

  if (height_ptr)
    *height_ptr = GST_VAAPI_TEXTURE_HEIGHT (texture);
}

/**
 * gst_vaapi_texture_get_display:
 * @texture: a #GstVaapiTexture
 *
 * Returns the #GstVaapiDisplay this @texture is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_texture_get_display (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, NULL);
  return GST_VAAPI_TEXTURE_DISPLAY (texture);
}

/**
 * gst_vaapi_texture_get_orientation_flags:
 * @texture: a #GstVaapiTexture
 *
 * Retrieves the texture memory layout flags, i.e. orientation.
 *
 * Return value: the #GstVaapiTextureOrientationFlags.
 */
guint
gst_vaapi_texture_get_orientation_flags (GstVaapiTexture * texture)
{
  g_return_val_if_fail (texture != NULL, 0);

  return GST_MINI_OBJECT_FLAGS (texture) & GST_VAAPI_TEXTURE_ORIENTATION_FLAGS;
}

/**
 * gst_vaapi_texture_set_orientation_flags:
 * @texture: a #GstVaapiTexture
 * @flags: a bitmask of #GstVaapiTextureOrientationFlags
 *
 * Reset the texture orientation flags to the supplied set of
 * @flags. This completely replaces the previously installed
 * flags. So, should they still be needed, then they shall be
 * retrieved first with gst_vaapi_texture_get_orientation_flags().
 */
void
gst_vaapi_texture_set_orientation_flags (GstVaapiTexture * texture, guint flags)
{
  g_return_if_fail (texture != NULL);
  g_return_if_fail ((flags & ~GST_VAAPI_TEXTURE_ORIENTATION_FLAGS) == 0);

  GST_MINI_OBJECT_FLAG_UNSET (texture, GST_VAAPI_TEXTURE_ORIENTATION_FLAGS);
  GST_MINI_OBJECT_FLAG_SET (texture, flags);
}

/**
 * gst_vaapi_texture_put_surface:
 * @texture: a #GstVaapiTexture
 * @surface: a #GstVaapiSurface
 * @flags: postprocessing flags. See #GstVaapiTextureRenderFlags
 *
 * Renders the @surface into the @texture. The @flags specify how
 * de-interlacing (if needed), color space conversion, scaling and
 * other postprocessing transformations are performed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_texture_put_surface (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  GstVaapiRectangle rect;

  g_return_val_if_fail (texture != NULL, FALSE);
  g_return_val_if_fail (surface != NULL, FALSE);

  if (!texture->put_surface)
    return FALSE;

  if (!crop_rect) {
    rect.x = 0;
    rect.y = 0;
    gst_vaapi_surface_get_size (surface, &rect.width, &rect.height);
    crop_rect = &rect;
  }
  return texture->put_surface (texture, surface, crop_rect, flags);
}
