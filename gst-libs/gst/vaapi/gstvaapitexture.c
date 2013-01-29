/*
 *  gstvaapitexture.c - VA texture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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
#include "gstvaapitexture.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_glx.h"
#include "gstvaapidisplay_glx.h"
#include "gstvaapi_priv.h"
#include "gstvaapidisplay_x11_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiTexture, gst_vaapi_texture, GST_VAAPI_TYPE_OBJECT)

#define GST_VAAPI_TEXTURE_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_TEXTURE,        \
                                 GstVaapiTexturePrivate))

struct _GstVaapiTexturePrivate {
    GLenum               target;
    GLenum               format;
    guint                width;
    guint                height;
    GLContextState      *gl_context;
    void                *gl_surface;
    GLPixmapObject      *pixo;
    GLFramebufferObject *fbo;
    guint                foreign_texture : 1;
    guint                is_constructed  : 1;
};

enum {
    PROP_0,

    PROP_TARGET,
    PROP_FORMAT,
    PROP_WIDTH,
    PROP_HEIGHT
};

static void
_gst_vaapi_texture_destroy_objects(GstVaapiTexture *texture)
{
    GstVaapiTexturePrivate * const priv = texture->priv;

#if USE_VAAPI_GLX
    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (priv->gl_surface) {
        vaDestroySurfaceGLX(
            GST_VAAPI_OBJECT_VADISPLAY(texture),
            priv->gl_surface
        );
        priv->gl_surface = NULL;
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
#else
    GLContextState old_cs;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (priv->gl_context)
        gl_set_current_context(priv->gl_context, &old_cs);

    if (priv->fbo) {
        gl_destroy_framebuffer_object(priv->fbo);
        priv->fbo = NULL;
    }

    if (priv->pixo) {
        gl_destroy_pixmap_object(priv->pixo);
        priv->pixo = NULL;
    }

    if (priv->gl_context) {
        gl_set_current_context(&old_cs, NULL);
        gl_destroy_context(priv->gl_context);
        priv->gl_context = NULL;
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
#endif
}

static void
gst_vaapi_texture_destroy(GstVaapiTexture *texture)
{
    GstVaapiTexturePrivate * const priv = texture->priv;
    const GLuint texture_id = GST_VAAPI_OBJECT_ID(texture);

    _gst_vaapi_texture_destroy_objects(texture);

    if (texture_id) {
        if (!priv->foreign_texture)
            glDeleteTextures(1, &texture_id);
        GST_VAAPI_OBJECT_ID(texture) = 0;
    }
}

static gboolean
_gst_vaapi_texture_create_objects(GstVaapiTexture *texture, GLuint texture_id)
{
    GstVaapiTexturePrivate * const priv = texture->priv;
    gboolean success = FALSE;

#if USE_VAAPI_GLX
    VAStatus status;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    status = vaCreateSurfaceGLX(
        GST_VAAPI_OBJECT_VADISPLAY(texture),
        priv->target,
        texture_id,
        &priv->gl_surface
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    success = vaapi_check_status(status, "vaCreateSurfaceGLX()");
#else
    GLContextState old_cs;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    gl_get_current_context(&old_cs);
    priv->gl_context = gl_create_context(
        GST_VAAPI_OBJECT_XDISPLAY(texture),
        GST_VAAPI_OBJECT_XSCREEN(texture),
        &old_cs
    );
    if (!priv->gl_context || !gl_set_current_context(priv->gl_context, NULL))
        goto end;

    priv->pixo = gl_create_pixmap_object(
        GST_VAAPI_OBJECT_XDISPLAY(texture),
        priv->width,
        priv->height
    );
    if (!priv->pixo)
        goto end;

    priv->fbo = gl_create_framebuffer_object(
        priv->target,
        texture_id,
        priv->width,
        priv->height
    );
    if (priv->fbo)
        success = TRUE;
end:
    gl_set_current_context(&old_cs, NULL);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
#endif
    return success;
}

static gboolean
gst_vaapi_texture_create(GstVaapiTexture *texture)
{
    GstVaapiTexturePrivate * const priv = texture->priv;
    GLuint texture_id;

    if (priv->foreign_texture)
        texture_id = GST_VAAPI_OBJECT_ID(texture);
    else {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
        texture_id = gl_create_texture(
            priv->target,
            priv->format,
            priv->width,
            priv->height
        );
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
        if (!texture_id)
            return FALSE;
        GST_VAAPI_OBJECT_ID(texture) = texture_id;
    }

    return _gst_vaapi_texture_create_objects(texture, texture_id);
}

static void
gst_vaapi_texture_finalize(GObject *object)
{
    gst_vaapi_texture_destroy(GST_VAAPI_TEXTURE(object));

    G_OBJECT_CLASS(gst_vaapi_texture_parent_class)->finalize(object);
}

static void
gst_vaapi_texture_constructed(GObject *object)
{
    GstVaapiTexture * const texture = GST_VAAPI_TEXTURE(object);
    GObjectClass *parent_class;

    texture->priv->foreign_texture = GST_VAAPI_OBJECT_ID(texture) != 0;
    texture->priv->is_constructed  = gst_vaapi_texture_create(texture);

    parent_class = G_OBJECT_CLASS(gst_vaapi_texture_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_texture_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiTexture * const texture = GST_VAAPI_TEXTURE(object);

    switch (prop_id) {
    case PROP_TARGET:
        texture->priv->target = g_value_get_uint(value);
        break;
    case PROP_FORMAT:
        texture->priv->format = g_value_get_uint(value);
        break;
    case PROP_WIDTH:
        texture->priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        texture->priv->height = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_texture_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiTexture * const texture = GST_VAAPI_TEXTURE(object);

    switch (prop_id) {
    case PROP_TARGET:
        g_value_set_uint(value, gst_vaapi_texture_get_target(texture));
        break;
    case PROP_FORMAT:
        g_value_set_uint(value, gst_vaapi_texture_get_format(texture));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_texture_get_width(texture));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_texture_get_height(texture));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_texture_class_init(GstVaapiTextureClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiTexturePrivate));

    object_class->finalize     = gst_vaapi_texture_finalize;
    object_class->set_property = gst_vaapi_texture_set_property;
    object_class->get_property = gst_vaapi_texture_get_property;
    object_class->constructed  = gst_vaapi_texture_constructed;

    g_object_class_install_property
        (object_class,
         PROP_TARGET,
         g_param_spec_uint("target",
                           "Target",
                           "The texture target",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_FORMAT,
         g_param_spec_uint("format",
                           "Format",
                           "The texture format",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "width",
                           "The texture width",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "The texture height",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_texture_init(GstVaapiTexture *texture)
{
    GstVaapiTexturePrivate *priv = GST_VAAPI_TEXTURE_GET_PRIVATE(texture);

    texture->priv               = priv;
    priv->target                = GL_NONE;
    priv->format                = GL_NONE;
    priv->width                 = 0;
    priv->height                = 0;
    priv->gl_context            = NULL;
    priv->gl_surface            = NULL;
    priv->pixo                  = NULL;
    priv->fbo                   = NULL;
    priv->foreign_texture       = FALSE;
    priv->is_constructed        = FALSE;
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
 * The application shall maintain the live GL context itself. That is,
 * gst_vaapi_window_glx_make_current() must be called beforehand, or
 * any other function like glXMakeCurrent() if the context is managed
 * outside of this library.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_new(
    GstVaapiDisplay *display,
    GLenum           target,
    GLenum           format,
    guint            width,
    guint            height
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    return g_object_new(GST_VAAPI_TYPE_TEXTURE,
                        "display", display,
                        "id",      GST_VAAPI_ID(0),
                        "target",  target,
                        "format",  format,
                        "width",   width,
                        "height",  height,
                        NULL);
}

/**
 * gst_vaapi_texture_new_with_texture:
 * @display: a #GstVaapiDisplay
 * @texture: the foreign GL texture name to use
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 *
 * Creates a texture from an existing GL texture, with the specified
 * @target and @format. Note that only GL_TEXTURE_2D @target and
 * GL_RGBA or GL_BGRA formats are supported at this time. The
 * dimensions will be retrieved from the @texture.
 *
 * The application shall maintain the live GL context itself. That is,
 * gst_vaapi_window_glx_make_current() must be called beforehand, or
 * any other function like glXMakeCurrent() if the context is managed
 * outside of this library.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_new_with_texture(
    GstVaapiDisplay *display,
    GLuint           texture,
    GLenum           target,
    GLenum           format
)
{
    guint width, height, border_width;
    GLTextureState ts;
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    /* Check texture dimensions */
    GST_VAAPI_DISPLAY_LOCK(display);
    success = gl_bind_texture(&ts, target, texture);
    if (success) {
        if (!gl_get_texture_param(target, GL_TEXTURE_WIDTH,  &width)  ||
            !gl_get_texture_param(target, GL_TEXTURE_HEIGHT, &height) ||
            !gl_get_texture_param(target, GL_TEXTURE_BORDER, &border_width))
            success = FALSE;
        gl_unbind_texture(&ts);
    }
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!success)
        return NULL;

    width  -= 2 * border_width;
    height -= 2 * border_width;
    if (width == 0 || height == 0)
        return NULL;

    return g_object_new(GST_VAAPI_TYPE_TEXTURE,
                        "display", display,
                        "id",      GST_VAAPI_ID(texture),
                        "target",  target,
                        "format",  format,
                        "width",   width,
                        "height",  height,
                        NULL);
}

/**
 * gst_vaapi_texture_get_id:
 * @texture: a #GstVaapiTexture
 *
 * Returns the underlying texture id of the @texture.
 *
 * Return value: the underlying texture id of the @texture
 */
GLuint
gst_vaapi_texture_get_id(GstVaapiTexture *texture)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), 0);

    return GST_VAAPI_OBJECT_ID(texture);
}

/**
 * gst_vaapi_texture_get_target:
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture target type
 *
 * Return value: the texture target
 */
GLenum
gst_vaapi_texture_get_target(GstVaapiTexture *texture)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), GL_NONE);
    g_return_val_if_fail(texture->priv->is_constructed, GL_NONE);

    return texture->priv->target;
}

/**
 * gst_vaapi_texture_get_format
 * @texture: a #GstVaapiTexture
 *
 * Returns the @texture format
 *
 * Return value: the texture format
 */
GLenum
gst_vaapi_texture_get_format(GstVaapiTexture *texture)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), GL_NONE);
    g_return_val_if_fail(texture->priv->is_constructed, GL_NONE);

    return texture->priv->format;
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
gst_vaapi_texture_get_width(GstVaapiTexture *texture)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), 0);
    g_return_val_if_fail(texture->priv->is_constructed, 0);

    return texture->priv->width;
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
gst_vaapi_texture_get_height(GstVaapiTexture *texture)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), 0);
    g_return_val_if_fail(texture->priv->is_constructed, 0);

    return texture->priv->height;
}

/**
 * gst_vaapi_texture_get_size:
 * @texture: a #GstVaapiTexture
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiTexture.
 */
void
gst_vaapi_texture_get_size(
    GstVaapiTexture *texture,
    guint           *pwidth,
    guint           *pheight
)
{
    g_return_if_fail(GST_VAAPI_IS_TEXTURE(texture));
    g_return_if_fail(texture->priv->is_constructed);

    if (pwidth)
        *pwidth = texture->priv->width;

    if (pheight)
        *pheight = texture->priv->height;
}

/**
 * gst_vaapi_texture_put_surface:
 * @texture: a #GstVaapiTexture
 * @surface: a #GstVaapiSurface
 * @flags: postprocessing flags. See #GstVaapiTextureRenderFlags
 *
 * Renders the @surface into the àtexture. The @flags specify how
 * de-interlacing (if needed), color space conversion, scaling and
 * other postprocessing transformations are performed.
 *
 * Return value: %TRUE on success
 */
static gboolean
_gst_vaapi_texture_put_surface(
    GstVaapiTexture *texture,
    GstVaapiSurface *surface,
    guint            flags
)
{
    GstVaapiTexturePrivate * const priv = texture->priv;
    VAStatus status;

#if USE_VAAPI_GLX
    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    status = vaCopySurfaceGLX(
        GST_VAAPI_OBJECT_VADISPLAY(texture),
        priv->gl_surface,
        GST_VAAPI_OBJECT_ID(surface),
        from_GstVaapiSurfaceRenderFlags(flags)
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    if (!vaapi_check_status(status, "vaCopySurfaceGLX()"))
        return FALSE;
#else
    guint surface_width, surface_height;
    GLContextState old_cs;
    gboolean success = FALSE;

    gst_vaapi_surface_get_size(surface, &surface_width, &surface_height);

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    status = vaPutSurface(
        GST_VAAPI_OBJECT_VADISPLAY(texture),
        GST_VAAPI_OBJECT_ID(surface),
        priv->pixo->pixmap,
        0, 0, surface_width, surface_height,
        0, 0, priv->width, priv->height,
        NULL, 0,
        from_GstVaapiSurfaceRenderFlags(flags)
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    if (!vaapi_check_status(status, "vaPutSurface() [TFP]"))
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (priv->gl_context) {
        success = gl_set_current_context(priv->gl_context, &old_cs);
        if (!success)
            goto end;
    }

    success = gl_bind_framebuffer_object(priv->fbo);
    if (!success) {
        GST_DEBUG("could not bind FBO");
        goto out_reset_context;
    }

    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    success = gst_vaapi_surface_sync(surface);
    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (!success) {
        GST_DEBUG("could not render surface to pixmap");
        goto out_unbind_fbo;
    }

    success = gl_bind_pixmap_object(priv->pixo);
    if (!success) {
        GST_DEBUG("could not bind GLX pixmap");
        goto out_unbind_fbo;
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(0.0f, 0.0f); glVertex2i(0,           0           );
        glTexCoord2f(0.0f, 1.0f); glVertex2i(0,           priv->height);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(priv->width, priv->height);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(priv->width, 0           );
    }
    glEnd();

    success = gl_unbind_pixmap_object(priv->pixo);
    if (!success) {
        GST_DEBUG("could not release GLX pixmap");
        goto out_unbind_fbo;
    }

out_unbind_fbo:
    if (!gl_unbind_framebuffer_object(priv->fbo))
        success = FALSE;
out_reset_context:
    if (priv->gl_context && !gl_set_current_context(&old_cs, NULL))
        success = FALSE;
end:
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    return success;
#endif
    return TRUE;
}

gboolean
gst_vaapi_texture_put_surface(
    GstVaapiTexture *texture,
    GstVaapiSurface *surface,
    guint            flags
)
{
    g_return_val_if_fail(GST_VAAPI_IS_TEXTURE(texture), FALSE);
    g_return_val_if_fail(texture->priv->is_constructed, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    return _gst_vaapi_texture_put_surface(texture, surface, flags);
}
