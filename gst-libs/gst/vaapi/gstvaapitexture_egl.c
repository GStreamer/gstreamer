/*
 *  gstvaapitexture_egl.c - VA/EGL texture abstraction
 *
 *  Copyright (C) 2014 Intel Corporation
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
 * SECTION:gstvaapitexture_egl
 * @short_description: VA/EGL texture abstraction
 */

#include "sysdeps.h"
#include "gstvaapitexture.h"
#include "gstvaapitexture_egl.h"
#include "gstvaapitexture_priv.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_egl.h"
#include "gstvaapidisplay_egl.h"
#include "gstvaapidisplay_egl_priv.h"
#include "gstvaapisurface_egl.h"
#include "gstvaapifilter.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiTextureEGLPrivate GstVaapiTextureEGLPrivate;

/**
 * GstVaapiTextureEGLPrivate:
 *
 * EGL texture specific fields.
 */
struct _GstVaapiTextureEGLPrivate
{
  /*< private > */
  GstVaapiTexture *texture;
  EglContext *egl_context;
  EGLImageKHR egl_image;
  GstVaapiSurface *surface;
  GstVaapiFilter *filter;
};

typedef struct
{
  GstVaapiTexture *texture;
  gboolean success;             /* result */
} CreateTextureArgs;

typedef struct
{
  GstVaapiTexture *texture;
  GstVaapiSurface *surface;
  const GstVaapiRectangle *crop_rect;
  guint flags;
  gboolean success;             /* result */
} UploadSurfaceArgs;

static gboolean
create_objects (GstVaapiTexture * texture, GLuint texture_id)
{
  GstVaapiTextureEGLPrivate *const texture_egl =
      gst_vaapi_texture_get_private (texture);
  EglContext *const ctx = texture_egl->egl_context;
  EglVTable *const vtable = egl_context_get_vtable (ctx, FALSE);
  GLint attribs[3] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
  guint mem_types;

  texture_egl->filter =
      gst_vaapi_filter_new (GST_VAAPI_TEXTURE_DISPLAY (texture));
  if (!texture_egl->filter)
    goto error_create_filter;

  mem_types = gst_vaapi_filter_get_memory_types (texture_egl->filter);

  texture_egl->egl_image =
      vtable->eglCreateImageKHR (ctx->display->base.handle.p,
      ctx->base.handle.p, EGL_GL_TEXTURE_2D_KHR,
      (EGLClientBuffer) GSIZE_TO_POINTER (texture_id), attribs);
  if (!texture_egl->egl_image)
    goto error_create_image;

  texture_egl->surface =
      gst_vaapi_surface_new_with_egl_image (GST_VAAPI_TEXTURE_DISPLAY (texture),
      texture_egl->egl_image, GST_VIDEO_FORMAT_RGBA, texture->width,
      texture->height, mem_types);
  if (!texture_egl->surface)
    goto error_create_surface;

  return TRUE;

  /* ERRORS */
error_create_image:
  {
    GST_ERROR ("failed to create EGL image from 2D texture %u", texture_id);
    return FALSE;
  }
error_create_surface:
  {
    GST_ERROR ("failed to create VA surface from 2D texture %u", texture_id);
    return FALSE;
  }
error_create_filter:
  {
    GST_ERROR ("failed to create VPP filter for color conversion");
    return FALSE;
  }
}

static gboolean
do_create_texture_unlocked (GstVaapiTexture * texture)
{
  GLuint texture_id;
  GstVaapiTextureEGLPrivate *texture_egl =
      gst_vaapi_texture_get_private (texture);

  if (texture->is_wrapped)
    texture_id = GST_VAAPI_TEXTURE_ID (texture);
  else {
    texture_id = egl_create_texture (texture_egl->egl_context,
        texture->gl_target, texture->gl_format,
        texture->width, texture->height);
    if (!texture_id)
      return FALSE;
    GST_VAAPI_TEXTURE_ID (texture) = texture_id;
  }
  return create_objects (texture, texture_id);
}

static void
do_create_texture (CreateTextureArgs * args)
{
  GstVaapiTexture *const texture = args->texture;
  GstVaapiTextureEGLPrivate *texture_egl =
      gst_vaapi_texture_get_private (texture);
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  if (egl_context_set_current (texture_egl->egl_context, TRUE, &old_cs)) {
    args->success = do_create_texture_unlocked (texture);
    egl_context_set_current (texture_egl->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
}

static void
destroy_objects (GstVaapiTextureEGLPrivate * texture_egl)
{
  EglContext *const ctx = texture_egl->egl_context;
  EglVTable *const vtable = egl_context_get_vtable (ctx, FALSE);

  if (texture_egl->egl_image != EGL_NO_IMAGE_KHR) {
    vtable->eglDestroyImageKHR (ctx->display->base.handle.p,
        texture_egl->egl_image);
    texture_egl->egl_image = EGL_NO_IMAGE_KHR;
  }
  gst_mini_object_replace ((GstMiniObject **) & texture_egl->surface, NULL);
  gst_vaapi_filter_replace (&texture_egl->filter, NULL);
}

static void
do_destroy_texture_unlocked (GstVaapiTextureEGLPrivate * texture_egl)
{
  GstVaapiTexture *const base_texture = texture_egl->texture;
  const GLuint texture_id = GST_VAAPI_TEXTURE_ID (base_texture);

  destroy_objects (texture_egl);

  if (texture_id) {
    if (!base_texture->is_wrapped)
      egl_destroy_texture (texture_egl->egl_context, texture_id);
    GST_VAAPI_TEXTURE_ID (base_texture) = 0;
  }
}

static void
do_destroy_texture (GstVaapiTextureEGLPrivate * texture_egl)
{
  EglContextState old_cs;
  GstVaapiTexture *texture = texture_egl->texture;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  if (egl_context_set_current (texture_egl->egl_context, TRUE, &old_cs)) {
    do_destroy_texture_unlocked (texture_egl);
    egl_context_set_current (texture_egl->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  egl_object_replace (&texture_egl->egl_context, NULL);
  g_free (texture_egl);
}

static gboolean
do_upload_surface_unlocked (GstVaapiTextureEGLPrivate * texture_egl,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  GstVaapiFilterStatus status;

  if (!gst_vaapi_filter_set_cropping_rectangle (texture_egl->filter, crop_rect))
    return FALSE;

  status = gst_vaapi_filter_process (texture_egl->filter, surface,
      texture_egl->surface, flags);
  if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    return FALSE;
  return TRUE;
}

static void
do_upload_surface (UploadSurfaceArgs * args)
{
  GstVaapiTexture *const texture = args->texture;
  GstVaapiTextureEGLPrivate *texture_egl =
      gst_vaapi_texture_get_private (texture);
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_DISPLAY_LOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
  if (egl_context_set_current (texture_egl->egl_context, TRUE, &old_cs)) {
    args->success = do_upload_surface_unlocked (texture_egl, args->surface,
        args->crop_rect, args->flags);
    egl_context_set_current (texture_egl->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_DISPLAY_UNLOCK (GST_VAAPI_TEXTURE_DISPLAY (texture));
}

static gboolean
gst_vaapi_texture_egl_create (GstVaapiTexture * texture)
{
  CreateTextureArgs args = { texture };
  GstVaapiDisplayEGL *display =
      GST_VAAPI_DISPLAY_EGL (GST_VAAPI_TEXTURE_DISPLAY (texture));
  GstVaapiTextureEGLPrivate *texture_egl =
      gst_vaapi_texture_get_private (texture);

  if (GST_VAAPI_TEXTURE (texture)->is_wrapped) {
    if (!gst_vaapi_display_egl_set_current_display (display))
      return FALSE;
  }

  egl_object_replace (&texture_egl->egl_context,
      GST_VAAPI_DISPLAY_EGL_CONTEXT (display));

  return egl_context_run (texture_egl->egl_context,
      (EglContextRunFunc) do_create_texture, &args) && args.success;
}

static void
gst_vaapi_texture_egl_destroy (GstVaapiTextureEGLPrivate * texture_egl)
{
  egl_context_run (texture_egl->egl_context,
      (EglContextRunFunc) do_destroy_texture, texture_egl);
}

static gboolean
gst_vaapi_texture_egl_put_surface (GstVaapiTexture * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  UploadSurfaceArgs args = { texture, surface, crop_rect, flags };
  GstVaapiTextureEGLPrivate *texture_egl =
      gst_vaapi_texture_get_private (texture);

  return egl_context_run (texture_egl->egl_context,
      (EglContextRunFunc) do_upload_surface, &args) && args.success;
}

static GstVaapiTexture *
gst_vaapi_texture_egl_new_internal (GstVaapiTexture * texture)
{
  GstVaapiTextureEGLPrivate *texture_egl;

  texture->put_surface = gst_vaapi_texture_egl_put_surface;

  texture_egl = g_malloc0 (sizeof (GstVaapiTextureEGLPrivate));
  if (!texture_egl) {
    gst_mini_object_unref (GST_MINI_OBJECT_CAST (texture));
    return NULL;
  }
  texture_egl->texture = texture;
  gst_vaapi_texture_set_private (texture, texture_egl,
      (GDestroyNotify) gst_vaapi_texture_egl_destroy);

  if (!gst_vaapi_texture_egl_create (texture)) {
    gst_mini_object_unref (GST_MINI_OBJECT_CAST (texture));
    return NULL;
  }

  return texture;
}

/**
 * gst_vaapi_texture_egl_new:
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
 * The application shall maintain the live EGL context itself. That
 * is, gst_vaapi_window_egl_make_current() must be called beforehand,
 * or any other function like eglMakeCurrent() if the context is
 * managed outside of this library.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_egl_new (GstVaapiDisplay * display, guint target,
    guint format, guint width, guint height)
{
  GstVaapiTexture *texture;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), NULL);

  texture = gst_vaapi_texture_new_internal (display, GST_VAAPI_ID_INVALID,
      target, format, width, height);
  if (!texture)
    return NULL;

  return gst_vaapi_texture_egl_new_internal (texture);
}

/**
 * gst_vaapi_texture_egl_new_wrapped:
 * @display: a #GstVaapiDisplay
 * @texture_id: the foreign GL texture name to use
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the texture width, in pixels
 * @height: the texture height, in pixels
 *
 * Creates a texture from an existing GL texture, with the specified
 * @target and @format. Note that only GL_TEXTURE_2D @target and
 * GL_RGBA or GL_BGRA formats are supported at this time.
 *
 * The application shall maintain the live EGL context itself. That
 * is, gst_vaapi_window_egl_make_current() must be called beforehand,
 * or any other function like eglMakeCurrent() if the context is
 * managed outside of this library.
 *
 * Return value: the newly created #GstVaapiTexture object
 */
GstVaapiTexture *
gst_vaapi_texture_egl_new_wrapped (GstVaapiDisplay * display,
    guint texture_id, guint target, GLenum format, guint width, guint height)
{
  GstVaapiTexture *texture;

  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), NULL);
  g_return_val_if_fail (texture_id != GL_NONE, NULL);

  texture = gst_vaapi_texture_new_internal (display, texture_id,
      target, format, width, height);
  if (!texture)
    return texture;

  return gst_vaapi_texture_egl_new_internal (texture);
}
