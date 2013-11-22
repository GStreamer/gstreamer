/*
 *  gstvaapitexture.c - VA texture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012 Intel Corporation
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
#include "gstvaapitexture.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_glx.h"
#include "gstvaapidisplay_glx.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapidisplay_glx_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiTextureClass            GstVaapiTextureClass;

/**
 * GstVaapiTexture:
 *
 * Base object for system-dependent textures.
 */
struct _GstVaapiTexture {
    /*< private >*/
    GstVaapiObject parent_instance;

    GLenum               target;
    GLenum               format;
    guint                width;
    guint                height;
    GLContextState      *gl_context;
    void                *gl_surface;
    GLPixmapObject      *pixo;
    GLFramebufferObject *fbo;
    guint                foreign_texture : 1;
};

/**
 * GstVaapiTextureClass:
 *
 * Base class for system-dependent textures.
 */
struct _GstVaapiTextureClass {
    GstVaapiObjectClass parent_class;
};

static void
_gst_vaapi_texture_destroy_objects(GstVaapiTexture *texture)
{
#if USE_VAAPI_GLX
    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (texture->gl_surface) {
        vaDestroySurfaceGLX(
            GST_VAAPI_OBJECT_VADISPLAY(texture),
            texture->gl_surface
        );
        texture->gl_surface = NULL;
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
#else
    GLContextState old_cs;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (texture->gl_context)
        gl_set_current_context(texture->gl_context, &old_cs);

    if (texture->fbo) {
        gl_destroy_framebuffer_object(texture->fbo);
        texture->fbo = NULL;
    }

    if (texture->pixo) {
        gl_destroy_pixmap_object(texture->pixo);
        texture->pixo = NULL;
    }

    if (texture->gl_context) {
        gl_set_current_context(&old_cs, NULL);
        gl_destroy_context(texture->gl_context);
        texture->gl_context = NULL;
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
#endif
}

static void
gst_vaapi_texture_destroy(GstVaapiTexture *texture)
{
    const GLuint texture_id = GST_VAAPI_OBJECT_ID(texture);

    _gst_vaapi_texture_destroy_objects(texture);

    if (texture_id) {
        if (!texture->foreign_texture)
            glDeleteTextures(1, &texture_id);
        GST_VAAPI_OBJECT_ID(texture) = 0;
    }
}

static gboolean
_gst_vaapi_texture_create_objects(GstVaapiTexture *texture, GLuint texture_id)
{
    gboolean success = FALSE;

#if USE_VAAPI_GLX
    VAStatus status;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    status = vaCreateSurfaceGLX(
        GST_VAAPI_OBJECT_VADISPLAY(texture),
        texture->target,
        texture_id,
        &texture->gl_surface
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    success = vaapi_check_status(status, "vaCreateSurfaceGLX()");
#else
    GLContextState old_cs;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    gl_get_current_context(&old_cs);
    texture->gl_context = gl_create_context(
        GST_VAAPI_OBJECT_XDISPLAY(texture),
        GST_VAAPI_OBJECT_XSCREEN(texture),
        &old_cs
    );
    if (!texture->gl_context ||
        !gl_set_current_context(texture->gl_context, NULL))
        goto end;

    texture->pixo = gl_create_pixmap_object(
        GST_VAAPI_OBJECT_XDISPLAY(texture),
        texture->width,
        texture->height
    );
    if (!texture->pixo)
        goto end;

    texture->fbo = gl_create_framebuffer_object(
        texture->target,
        texture_id,
        texture->width,
        texture->height
    );
    if (texture->fbo)
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
    GLuint texture_id;

    if (texture->foreign_texture)
        texture_id = GST_VAAPI_OBJECT_ID(texture);
    else {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
        texture_id = gl_create_texture(
            texture->target,
            texture->format,
            texture->width,
            texture->height
        );
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
        if (!texture_id)
            return FALSE;
        GST_VAAPI_OBJECT_ID(texture) = texture_id;
    }

    return _gst_vaapi_texture_create_objects(texture, texture_id);
}

static void
gst_vaapi_texture_init(GstVaapiTexture *texture, GLuint texture_id,
    GLenum target, GLenum format, guint width, guint height)
{
    GST_VAAPI_OBJECT_ID(texture) = texture_id;
    texture->foreign_texture = texture_id != GL_NONE;

    texture->target = target;
    texture->format = format;
    texture->width  = width;
    texture->height = height;
}

#define gst_vaapi_texture_finalize gst_vaapi_texture_destroy
GST_VAAPI_OBJECT_DEFINE_CLASS(GstVaapiTexture, gst_vaapi_texture)

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
    GstVaapiTexture *texture;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_GLX(display), NULL);
    g_return_val_if_fail(target != GL_NONE, NULL);
    g_return_val_if_fail(format != GL_NONE, NULL);
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    texture = gst_vaapi_object_new(gst_vaapi_texture_class(), display);
    if (!texture)
        return NULL;

    gst_vaapi_texture_init(texture, GL_NONE, target, format, width, height);
    if (!gst_vaapi_texture_create(texture))
        goto error;
    return texture;

error:
    gst_vaapi_object_unref(texture);
    return NULL;
}

/**
 * gst_vaapi_texture_new_with_texture:
 * @display: a #GstVaapiDisplay
 * @texture_id: the foreign GL texture name to use
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 *
 * Creates a texture from an existing GL texture, with the specified
 * @target and @format. Note that only GL_TEXTURE_2D @target and
 * GL_RGBA or GL_BGRA formats are supported at this time. The
 * dimensions will be retrieved from the @texture_id.
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
    GLuint           texture_id,
    GLenum           target,
    GLenum           format
)
{
    GstVaapiTexture *texture;
    guint width, height, border_width;
    GLTextureState ts;
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_GLX(display), NULL);
    g_return_val_if_fail(target != GL_NONE, NULL);
    g_return_val_if_fail(format != GL_NONE, NULL);

    /* Check texture dimensions */
    GST_VAAPI_DISPLAY_LOCK(display);
    success = gl_bind_texture(&ts, target, texture_id);
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
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    texture = gst_vaapi_object_new(gst_vaapi_texture_class(), display);
    if (!texture)
        return NULL;

    gst_vaapi_texture_init(texture, texture_id, target, format, width, height);
    if (!gst_vaapi_texture_create(texture))
        goto error;
    return texture;

error:
    gst_vaapi_object_unref(texture);
    return NULL;
}

/**
 * gst_vaapi_texture_ref:
 * @texture: a #GstVaapiTexture
 *
 * Atomically increases the reference count of the given @texture by one.
 *
 * Returns: The same @texture argument
 */
GstVaapiTexture *
gst_vaapi_texture_ref(GstVaapiTexture *texture)
{
    return gst_vaapi_object_ref(texture);
}

/**
 * gst_vaapi_texture_unref:
 * @texture: a #GstVaapiTexture
 *
 * Atomically decreases the reference count of the @texture by one. If
 * the reference count reaches zero, the texture will be free'd.
 */
void
gst_vaapi_texture_unref(GstVaapiTexture *texture)
{
    gst_vaapi_object_unref(texture);
}

/**
 * gst_vaapi_texture_replace:
 * @old_texture_ptr: a pointer to a #GstVaapiTexture
 * @new_texture: a #GstVaapiTexture
 *
 * Atomically replaces the texture texture held in @old_texture_ptr
 * with @new_texture. This means that @old_texture_ptr shall reference
 * a valid texture. However, @new_texture can be NULL.
 */
void
gst_vaapi_texture_replace(GstVaapiTexture **old_texture_ptr,
    GstVaapiTexture *new_texture)
{
    gst_vaapi_object_replace(old_texture_ptr, new_texture);
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
    g_return_val_if_fail(texture != NULL, 0);

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
    g_return_val_if_fail(texture != NULL, GL_NONE);

    return texture->target;
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
    g_return_val_if_fail(texture != NULL, GL_NONE);

    return texture->format;
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
    g_return_val_if_fail(texture != NULL, 0);

    return texture->width;
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
    g_return_val_if_fail(texture != NULL, 0);

    return texture->height;
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
    g_return_if_fail(texture != NULL);

    if (pwidth)
        *pwidth = texture->width;

    if (pheight)
        *pheight = texture->height;
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
    VAStatus status;

#if USE_VAAPI_GLX
    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    status = vaCopySurfaceGLX(
        GST_VAAPI_OBJECT_VADISPLAY(texture),
        texture->gl_surface,
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
        texture->pixo->pixmap,
        0, 0, surface_width, surface_height,
        0, 0, texture->width, texture->height,
        NULL, 0,
        from_GstVaapiSurfaceRenderFlags(flags)
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(texture);
    if (!vaapi_check_status(status, "vaPutSurface() [TFP]"))
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(texture);
    if (texture->gl_context) {
        success = gl_set_current_context(texture->gl_context, &old_cs);
        if (!success)
            goto end;
    }

    success = gl_bind_framebuffer_object(texture->fbo);
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

    success = gl_bind_pixmap_object(texture->pixo);
    if (!success) {
        GST_DEBUG("could not bind GLX pixmap");
        goto out_unbind_fbo;
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(0.0f, 0.0f); glVertex2i(0,              0              );
        glTexCoord2f(0.0f, 1.0f); glVertex2i(0,              texture->height);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(texture->width, texture->height);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(texture->width, 0              );
    }
    glEnd();

    success = gl_unbind_pixmap_object(texture->pixo);
    if (!success) {
        GST_DEBUG("could not release GLX pixmap");
        goto out_unbind_fbo;
    }

out_unbind_fbo:
    if (!gl_unbind_framebuffer_object(texture->fbo))
        success = FALSE;
out_reset_context:
    if (texture->gl_context && !gl_set_current_context(&old_cs, NULL))
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
    g_return_val_if_fail(texture != NULL, FALSE);
    g_return_val_if_fail(surface != NULL, FALSE);

    return _gst_vaapi_texture_put_surface(texture, surface, flags);
}
