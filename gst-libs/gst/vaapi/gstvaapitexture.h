/*
 *  gstvaapitexture.h - VA texture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_TEXTURE_H
#define GST_VAAPI_TEXTURE_H

#include <GL/gl.h>
#include <gst/vaapi/gstvaapitypes.h>
#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_TEXTURE \
    (gst_vaapi_texture_get_type())

#define GST_VAAPI_TEXTURE(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_TEXTURE, \
                                GstVaapiTexture))

#define GST_VAAPI_TEXTURE_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_TEXTURE,    \
                             GstVaapiTextureClass))

#define GST_VAAPI_IS_TEXTURE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_TEXTURE))

#define GST_VAAPI_IS_TEXTURE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_TEXTURE))

#define GST_VAAPI_TEXTURE_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_TEXTURE,  \
                               GstVaapiTextureClass))

typedef struct _GstVaapiTexture                 GstVaapiTexture;
typedef struct _GstVaapiTexturePrivate          GstVaapiTexturePrivate;
typedef struct _GstVaapiTextureClass            GstVaapiTextureClass;

/**
 * GstVaapiTexture:
 *
 * Base class for system-dependent textures.
 */
struct _GstVaapiTexture {
    /*< private >*/
    GstVaapiObject parent_instance;

    GstVaapiTexturePrivate *priv;
};

/**
 * GstVaapiTextureClass:
 *
 * Base class for system-dependent textures.
 */
struct _GstVaapiTextureClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;
};

GType
gst_vaapi_texture_get_type(void) G_GNUC_CONST;

GstVaapiTexture *
gst_vaapi_texture_new(
    GstVaapiDisplay *display,
    GLenum           target,
    GLenum           format,
    guint            width,
    guint            height
);

GstVaapiTexture *
gst_vaapi_texture_new_with_texture(
    GstVaapiDisplay *display,
    GLuint           texture,
    GLenum           target,
    GLenum           format
);

GLuint
gst_vaapi_texture_get_id(GstVaapiTexture *texture);

GLenum
gst_vaapi_texture_get_target(GstVaapiTexture *texture);

GLenum
gst_vaapi_texture_get_format(GstVaapiTexture *texture);

guint
gst_vaapi_texture_get_width(GstVaapiTexture *texture);

guint
gst_vaapi_texture_get_height(GstVaapiTexture *texture);

void
gst_vaapi_texture_get_size(
    GstVaapiTexture *texture,
    guint           *pwidth,
    guint           *pheight
);

gboolean
gst_vaapi_texture_put_surface(
    GstVaapiTexture *texture,
    GstVaapiSurface *surface,
    guint            flags
);

G_END_DECLS

#endif /* GST_VAAPI_TEXTURE_H */
