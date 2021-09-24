/*
 *  gstvaapitexture_priv.h - VA texture abstraction (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2014 Intel Corporation
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

#ifndef GST_VAAPI_TEXTURE_PRIV_H
#define GST_VAAPI_TEXTURE_PRIV_H

G_BEGIN_DECLS

/**
 * GST_VAAPI_TEXTURE_DISPLAY:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the @texture's display.
 */
#undef GST_VAAPI_TEXTURE_DISPLAY
#define GST_VAAPI_TEXTURE_DISPLAY(texture) \
  (GST_VAAPI_TEXTURE (texture)->display)

/**
 * GST_VAAPI_TEXTURE_ID:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture id associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_ID
#define GST_VAAPI_TEXTURE_ID(texture) \
  (GST_VAAPI_TEXTURE (texture)->object_id)

/**
 * GST_VAAPI_TEXTURE_TARGET:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture target associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_TARGET
#define GST_VAAPI_TEXTURE_TARGET(texture) \
  (GST_VAAPI_TEXTURE (texture)->gl_target)

/**
 * GST_VAAPI_TEXTURE_FORMAT:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture format associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_FORMAT
#define GST_VAAPI_TEXTURE_FORMAT(texture) \
  (GST_VAAPI_TEXTURE (texture)->gl_format)

/**
 * GST_VAAPI_TEXTURE_WIDTH:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture width associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_WIDTH
#define GST_VAAPI_TEXTURE_WIDTH(texture) \
  (GST_VAAPI_TEXTURE (texture)->width)

/**
 * GST_VAAPI_TEXTURE_HEIGHT:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture height associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_HEIGHT
#define GST_VAAPI_TEXTURE_HEIGHT(texture) \
  (GST_VAAPI_TEXTURE (texture)->height)

/* GstVaapiTextureClass hooks */
typedef gboolean (*GstVaapiTexturePutSurfaceFunc) (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags);

/**
 * GstVaapiTexture:
 *
 * Base class for API-dependent textures.
 */
struct _GstVaapiTexture {
  /*< private >*/
  GstMiniObject mini_object;
  GstVaapiDisplay *display;
  GstVaapiID object_id;

  /*< protected >*/
  GstVaapiTexturePutSurfaceFunc put_surface;
  guint gl_target;
  guint gl_format;
  guint width;
  guint height;
  guint is_wrapped:1;
};

GstVaapiTexture *
gst_vaapi_texture_new_internal (GstVaapiDisplay * display, GstVaapiID id,
    guint target, guint format, guint width, guint height);

gpointer
gst_vaapi_texture_get_private (GstVaapiTexture * texture);

void
gst_vaapi_texture_set_private (GstVaapiTexture * texture, gpointer priv,
    GDestroyNotify destroy);

G_END_DECLS

#endif /* GST_VAAPI_TEXTURE_PRIV_H */
