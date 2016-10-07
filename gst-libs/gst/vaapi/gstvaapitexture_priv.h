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

#include "gstvaapiobject_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_TEXTURE_CLASS(klass) \
  ((GstVaapiTextureClass *)(klass))

#define GST_VAAPI_TEXTURE_GET_CLASS(obj) \
  GST_VAAPI_TEXTURE_CLASS (GST_VAAPI_OBJECT_GET_CLASS (obj))

/**
 * GST_VAAPI_TEXTURE_ID:
 * @texture: a #GstVaapiTexture
 *
 * Macro that evaluates to the GL texture id associated with the @texture
 */
#undef  GST_VAAPI_TEXTURE_ID
#define GST_VAAPI_TEXTURE_ID(texture) \
  (GST_VAAPI_OBJECT_ID (texture))

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

#define GST_VAAPI_TEXTURE_FLAGS         GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_TEXTURE_FLAG_IS_SET   GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_TEXTURE_FLAG_SET      GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_TEXTURE_FLAG_UNSET    GST_VAAPI_MINI_OBJECT_FLAG_UNSET

/* GstVaapiTextureClass hooks */
typedef gboolean (*GstVaapiTextureAllocateFunc) (GstVaapiTexture * texture);
typedef gboolean (*GstVaapiTexturePutSurfaceFunc) (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect,
    guint flags);

typedef struct _GstVaapiTextureClass GstVaapiTextureClass;

/**
 * GstVaapiTexture:
 *
 * Base class for API-dependent textures.
 */
struct _GstVaapiTexture {
  /*< private >*/
  GstVaapiObject parent_instance;

  /*< protected >*/
  guint gl_target;
  guint gl_format;
  guint width;
  guint height;
  guint is_wrapped:1;
};

/**
 * GstVaapiTextureClass:
 * @put_surface: virtual function to render a #GstVaapiSurface into a texture
 *
 * Base class for API-dependent textures.
 */
struct _GstVaapiTextureClass {
  /*< private >*/
  GstVaapiObjectClass parent_class;

  /*< protected >*/
  GstVaapiTextureAllocateFunc allocate;
  GstVaapiTexturePutSurfaceFunc put_surface;
};

GstVaapiTexture *
gst_vaapi_texture_new_internal (const GstVaapiTextureClass * klass,
    GstVaapiDisplay * display, GstVaapiID id, guint target, guint format,
    guint width, guint height);

/* Inline reference counting for core libgstvaapi library */
#ifdef IN_LIBGSTVAAPI_CORE
#define gst_vaapi_texture_ref_internal(texture) \
  ((gpointer)gst_vaapi_object_ref (GST_VAAPI_OBJECT (texture)))

#define gst_vaapi_texture_unref_internal(texture) \
  gst_vaapi_object_unref (GST_VAAPI_OBJECT (texture))

#define gst_vaapi_texture_replace_internal(old_texture_ptr, new_texture) \
  gst_vaapi_object_replace ((GstVaapiObject **)(old_texture_ptr), \
      GST_VAAPI_OBJECT (new_texture))

#undef  gst_vaapi_texture_ref
#define gst_vaapi_texture_ref(texture) \
  gst_vaapi_texture_ref_internal ((texture))

#undef  gst_vaapi_texture_unref
#define gst_vaapi_texture_unref(texture) \
  gst_vaapi_texture_unref_internal ((texture))

#undef  gst_vaapi_texture_replace
#define gst_vaapi_texture_replace(old_texture_ptr, new_texture) \
  gst_vaapi_texture_replace_internal ((old_texture_ptr), (new_texture))
#endif

G_END_DECLS

#endif /* GST_VAAPI_TEXTURE_PRIV_H */
