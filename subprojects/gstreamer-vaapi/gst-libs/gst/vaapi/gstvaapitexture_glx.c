/*
 *  gstvaapitexture_glx.c - VA/GLX texture abstraction
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

/**
 * SECTION:gstvaapitexture_glx
 * @short_description: VA/GLX texture abstraction
 */

#include "sysdeps.h"
#include "gstvaapitexture.h"
#include "gstvaapitexture_glx.h"
#include "gstvaapitexture_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_glx.h"
#include "gstvaapidisplay_glx.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapidisplay_glx_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_TEXTURE_GLX(texture) \
  ((GstVaapiTextureGLX *)(texture))

typedef struct _GstVaapiTextureGLXPrivate GstVaapiTextureGLXPrivate;

/**
 * GstVaapiTextureGLXPrivate:
 *
 * GLX texture specific fields.
 */
struct _GstVaapiTextureGLXPrivate
{
  /*< private > */
  GstVaapiTexture *texture;
  GLContextState *gl_context;
  GLPixmapObject *pixo;
  GLFramebufferObject *fbo;
};

static gboolean
gst_vaapi_texture_glx_put_surface (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect,
    guint flags);

static void
destroy_objects (GstVaapiTextureGLXPrivate * texture)
{
  GLContextState old_cs;

  if (texture->gl_context)
    gl_set_current_context (texture->gl_context, &old_cs);

  if (texture->fbo) {
    gl_destroy_framebuffer_object (texture->fbo);
    texture->fbo = NULL;
  }

  if (texture->pixo) {
    gl_destroy_pixmap_object (texture->pixo);
    texture->pixo = NULL;
  }

  if (texture->gl_context) {
    gl_set_current_context (&old_cs, NULL);
    gl_destroy_context (texture->gl_context);
    texture->gl_context = NULL;
  }
}

static void
destroy_texture_unlocked (GstVaapiTextureGLXPrivate * texture_glx)
{
  GstVaapiTexture *texture = texture_glx->texture;
  const guint texture_id = GST_VAAPI_TEXTURE_ID (texture);

  destroy_objects (texture_glx);

  if (texture_id) {
    if (!texture->is_wrapped)
      glDeleteTextures (1, &texture_id);
    GST_VAAPI_TEXTURE_ID (texture) = 0;
  }
}

static void
gst_vaapi_texture_glx_destroy (GstVaapiTextureGLXPrivate * texture_glx)
{
  GstVaapiTexture *texture = texture_glx->texture;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  destroy_texture_unlocked (texture_glx);
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));

  g_free (texture_glx);
}

static gboolean
create_objects (GstVaapiTexture * texture, guint texture_id)
{
  GstVaapiTextureGLXPrivate *texture_glx =
      gst_vaapi_texture_get_private (texture);
  Display *const dpy =
      GST_VAAPI_DISPLAY_NATIVE (GST_VAAPI_TEXTURE_DISPLAY (texture));
  GLContextState old_cs;
  gboolean success = FALSE;

  gl_get_current_context (&old_cs);

  texture_glx->gl_context =
      gl_create_context (dpy, DefaultScreen (dpy), &old_cs);
  if (!texture_glx->gl_context
      || !gl_set_current_context (texture_glx->gl_context, NULL))
    return FALSE;

  texture_glx->pixo = gl_create_pixmap_object (dpy,
      texture->width, texture->height);
  if (!texture_glx->pixo) {
    GST_ERROR ("failed to create GLX pixmap");
    goto out_reset_context;
  }

  texture_glx->fbo = gl_create_framebuffer_object (texture->gl_target,
      texture_id, texture->width, texture->height);
  if (!texture_glx->fbo) {
    GST_ERROR ("failed to create FBO");
    goto out_reset_context;
  }
  success = TRUE;

out_reset_context:
  gl_set_current_context (&old_cs, NULL);
  return success;
}

static gboolean
create_texture_unlocked (GstVaapiTexture * texture)
{
  guint texture_id;

  if (texture->is_wrapped)
    texture_id = GST_VAAPI_TEXTURE_ID (texture);
  else {
    texture_id = gl_create_texture (texture->gl_target, texture->gl_format,
        texture->width, texture->height);
    if (!texture_id)
      return FALSE;
    GST_VAAPI_TEXTURE_ID (texture) = texture_id;
  }
  return create_objects (texture, texture_id);
}

static gboolean
gst_vaapi_texture_glx_create (GstVaapiTexture * texture)
{
  gboolean success;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  success = create_texture_unlocked (texture);
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  return success;
}


static GstVaapiTexture *
gst_vaapi_texture_glx_new_internal (GstVaapiTexture * texture)
{
  GstVaapiTextureGLXPrivate *texture_glx;

  texture->put_surface = gst_vaapi_texture_glx_put_surface;

  texture_glx = g_malloc0 (sizeof (GstVaapiTextureGLXPrivate));
  if (!texture_glx) {
    gst_mini_object_unref (GST_MINI_OBJECT_CAST (texture));
    return NULL;
  }
  texture_glx->texture = texture;
  gst_vaapi_texture_set_private (texture, texture_glx,
      (GDestroyNotify) gst_vaapi_texture_glx_destroy);

  if (!gst_vaapi_texture_glx_create (texture)) {
    gst_mini_object_unref (GST_MINI_OBJECT_CAST (texture));
    return NULL;
  }

  return texture;
}

/**
 * gst_vaapi_texture_glx_new:
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
gst_vaapi_texture_glx_new (GstVaapiDisplay * display, guint target,
    guint format, guint width, guint height)
{
  GstVaapiTexture *texture;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_GLX (display), NULL);

  texture = gst_vaapi_texture_new_internal (display, GST_VAAPI_ID_INVALID,
      target, format, width, height);
  if (!texture)
    return NULL;

  return gst_vaapi_texture_glx_new_internal (texture);
}

/* Can we assume that the vsink/app context API won't change ever? */
GstVaapiGLApi
gl_get_curent_api_once ()
{
  static GstVaapiGLApi cur_api = GST_VAAPI_GL_API_NONE;
  static gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    cur_api = gl_get_current_api (NULL, NULL);
    g_once_init_leave (&_init, 1);
  }

  return cur_api;
}

/**
 * gst_vaapi_texture_glx_new_wrapped:
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
gst_vaapi_texture_glx_new_wrapped (GstVaapiDisplay * display,
    guint texture_id, guint target, guint format)
{
  guint width, height, border_width = 0;
  GLTextureState ts = { 0, };
  gboolean success;
  GstVaapiGLApi gl_api;
  GstVaapiTexture *texture;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_GLX (display), NULL);
  g_return_val_if_fail (texture_id != GL_NONE, NULL);
  g_return_val_if_fail (target == GL_TEXTURE_2D, NULL);
  g_return_val_if_fail (format == GL_RGBA || format == GL_BGRA, NULL);

  gl_api = gl_get_curent_api_once ();
  if (gl_api != GST_VAAPI_GL_API_OPENGL && gl_api != GST_VAAPI_GL_API_OPENGL3)
    return NULL;

  /* Check texture dimensions */
  GST_VAAPI_DISPLAY_LOCK (display);
  if (gl_api == GST_VAAPI_GL_API_OPENGL)
    success = gl_bind_texture (&ts, target, texture_id);
  else
    success = gl3_bind_texture_2d (&ts, target, texture_id);

  if (success) {
    if (!gl_get_texture_param (target, GL_TEXTURE_WIDTH, &width) ||
        !gl_get_texture_param (target, GL_TEXTURE_HEIGHT, &height))
      success = FALSE;
    if (success && gl_api == GST_VAAPI_GL_API_OPENGL)
      success = gl_get_texture_param (target, GL_TEXTURE_BORDER, &border_width);
    gl_unbind_texture (&ts);
  }
  GST_VAAPI_DISPLAY_UNLOCK (display);
  if (!success)
    return NULL;

  width -= 2 * border_width;
  height -= 2 * border_width;
  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  texture = gst_vaapi_texture_new_internal (display, texture_id, target,
      format, width, height);
  if (!texture)
    return NULL;

  return gst_vaapi_texture_glx_new_internal (texture);
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
gst_vaapi_texture_glx_put_surface_unlocked (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  GstVaapiTextureGLXPrivate *texture_glx =
      gst_vaapi_texture_get_private (texture);
  VAStatus status;
  GLContextState old_cs;
  gboolean success = FALSE;

  const GLfloat *txc, *tyc;
  static const GLfloat g_texcoords[2][2] = {
    {0.0f, 1.0f},
    {1.0f, 0.0f},
  };

  status = vaPutSurface (GST_VAAPI_DISPLAY_VADISPLAY (GST_VAAPI_TEXTURE_DISPLAY
          (texture)), GST_VAAPI_SURFACE_ID (surface), texture_glx->pixo->pixmap,
      crop_rect->x, crop_rect->y, crop_rect->width, crop_rect->height, 0, 0,
      texture->width, texture->height, NULL, 0,
      from_GstVaapiSurfaceRenderFlags (flags));
  if (!vaapi_check_status (status, "vaPutSurface() [TFP]"))
    return FALSE;

  if (texture_glx->gl_context &&
      !gl_set_current_context (texture_glx->gl_context, &old_cs))
    return FALSE;

  if (!gl_bind_framebuffer_object (texture_glx->fbo)) {
    GST_ERROR ("failed to bind FBO");
    goto out_reset_context;
  }

  if (!gst_vaapi_surface_sync (surface)) {
    GST_ERROR ("failed to render surface to pixmap");
    goto out_unbind_fbo;
  }

  if (!gl_bind_pixmap_object (texture_glx->pixo)) {
    GST_ERROR ("could not bind GLX pixmap");
    goto out_unbind_fbo;
  }

  flags = GST_MINI_OBJECT_FLAGS (texture);
  txc = g_texcoords[!!(flags & GST_VAAPI_TEXTURE_ORIENTATION_FLAG_X_INVERTED)];
  tyc = g_texcoords[!!(flags & GST_VAAPI_TEXTURE_ORIENTATION_FLAG_Y_INVERTED)];

  glColor4f (1.0f, 1.0f, 1.0f, 1.0f);
  glBegin (GL_QUADS);
  {
    glTexCoord2f (txc[0], tyc[0]);
    glVertex2i (0, 0);
    glTexCoord2f (txc[0], tyc[1]);
    glVertex2i (0, texture->height);
    glTexCoord2f (txc[1], tyc[1]);
    glVertex2i (texture->width, texture->height);
    glTexCoord2f (txc[1], tyc[0]);
    glVertex2i (texture->width, 0);
  }
  glEnd ();

  if (!gl_unbind_pixmap_object (texture_glx->pixo)) {
    GST_ERROR ("failed to release GLX pixmap");
    goto out_unbind_fbo;
  }
  success = TRUE;

out_unbind_fbo:
  if (!gl_unbind_framebuffer_object (texture_glx->fbo))
    success = FALSE;
out_reset_context:
  if (texture_glx->gl_context && !gl_set_current_context (&old_cs, NULL))
    success = FALSE;
  return success;
}

static gboolean
gst_vaapi_texture_glx_put_surface (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  gboolean success;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  success = gst_vaapi_texture_glx_put_surface_unlocked (texture, surface,
      crop_rect, flags);
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  return success;
}
