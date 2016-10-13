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

#define GST_VAAPI_TEXTURE_EGL(texture) \
  ((GstVaapiTextureEGL *) (texture))

typedef struct _GstVaapiTextureEGL GstVaapiTextureEGL;
typedef struct _GstVaapiTextureEGLClass GstVaapiTextureEGLClass;

/**
 * GstVaapiTextureEGL:
 *
 * Base object for EGL texture wrapper.
 */
struct _GstVaapiTextureEGL
{
  /*< private > */
  GstVaapiTexture parent_instance;

  EglContext *egl_context;
  EGLImageKHR egl_image;
  GstVaapiSurface *surface;
  GstVaapiFilter *filter;
};

/**
 * GstVaapiTextureEGLClass:
 *
 * Base class for EGL texture wrapper.
 */
struct _GstVaapiTextureEGLClass
{
  /*< private > */
  GstVaapiTextureClass parent_class;
};

typedef struct
{
  GstVaapiTextureEGL *texture;
  gboolean success;             /* result */
} CreateTextureArgs;

typedef struct
{
  GstVaapiTextureEGL *texture;
  GstVaapiSurface *surface;
  const GstVaapiRectangle *crop_rect;
  guint flags;
  gboolean success;             /* result */
} UploadSurfaceArgs;

static gboolean
create_objects (GstVaapiTextureEGL * texture, GLuint texture_id)
{
  GstVaapiTexture *const base_texture = GST_VAAPI_TEXTURE (texture);
  EglContext *const ctx = texture->egl_context;
  EglVTable *const vtable = egl_context_get_vtable (ctx, FALSE);
  GLint attribs[3], *attrib;

  attrib = attribs;
  *attrib++ = EGL_IMAGE_PRESERVED_KHR;
  *attrib++ = EGL_TRUE;
  *attrib++ = EGL_NONE;
  texture->egl_image = vtable->eglCreateImageKHR (ctx->display->base.handle.p,
      ctx->base.handle.p, EGL_GL_TEXTURE_2D_KHR,
      (EGLClientBuffer) GSIZE_TO_POINTER (texture_id), attribs);
  if (!texture->egl_image)
    goto error_create_image;

  texture->surface =
      gst_vaapi_surface_new_with_egl_image (GST_VAAPI_OBJECT_DISPLAY (texture),
      texture->egl_image, GST_VIDEO_FORMAT_RGBA, base_texture->width,
      base_texture->height);
  if (!texture->surface)
    goto error_create_surface;

  texture->filter = gst_vaapi_filter_new (GST_VAAPI_OBJECT_DISPLAY (texture));
  if (!texture->filter)
    goto error_create_filter;
  return TRUE;

  /* ERRORS */
error_create_image:
  GST_ERROR ("failed to create EGL image from 2D texture %u", texture_id);
  return FALSE;
error_create_surface:
  GST_ERROR ("failed to create VA surface from 2D texture %u", texture_id);
  return FALSE;
error_create_filter:
  GST_ERROR ("failed to create VPP filter for color conversion");
  return FALSE;
}

static gboolean
do_create_texture_unlocked (GstVaapiTextureEGL * texture)
{
  GstVaapiTexture *const base_texture = GST_VAAPI_TEXTURE (texture);
  GLuint texture_id;

  if (base_texture->is_wrapped)
    texture_id = GST_VAAPI_TEXTURE_ID (texture);
  else {
    texture_id = egl_create_texture (texture->egl_context,
        base_texture->gl_target, base_texture->gl_format,
        base_texture->width, base_texture->height);
    if (!texture_id)
      return FALSE;
    GST_VAAPI_TEXTURE_ID (texture) = texture_id;
  }
  return create_objects (texture, texture_id);
}

static void
do_create_texture (CreateTextureArgs * args)
{
  GstVaapiTextureEGL *const texture = args->texture;
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (texture);
  if (egl_context_set_current (texture->egl_context, TRUE, &old_cs)) {
    args->success = do_create_texture_unlocked (texture);
    egl_context_set_current (texture->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (texture);
}

static void
destroy_objects (GstVaapiTextureEGL * texture)
{
  EglContext *const ctx = texture->egl_context;
  EglVTable *const vtable = egl_context_get_vtable (ctx, FALSE);

  if (texture->egl_image != EGL_NO_IMAGE_KHR) {
    vtable->eglDestroyImageKHR (ctx->display->base.handle.p,
        texture->egl_image);
    texture->egl_image = EGL_NO_IMAGE_KHR;
  }
  gst_vaapi_object_replace (&texture->surface, NULL);
  gst_vaapi_filter_replace (&texture->filter, NULL);
}

static void
do_destroy_texture_unlocked (GstVaapiTextureEGL * texture)
{
  GstVaapiTexture *const base_texture = GST_VAAPI_TEXTURE (texture);
  const GLuint texture_id = GST_VAAPI_TEXTURE_ID (texture);

  destroy_objects (texture);

  if (texture_id) {
    if (!base_texture->is_wrapped)
      egl_destroy_texture (texture->egl_context, texture_id);
    GST_VAAPI_TEXTURE_ID (texture) = 0;
  }
}

static void
do_destroy_texture (GstVaapiTextureEGL * texture)
{
  EglContextState old_cs;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (texture);
  if (egl_context_set_current (texture->egl_context, TRUE, &old_cs)) {
    do_destroy_texture_unlocked (texture);
    egl_context_set_current (texture->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (texture);
  egl_object_replace (&texture->egl_context, NULL);
}

static gboolean
do_upload_surface_unlocked (GstVaapiTextureEGL * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  GstVaapiFilterStatus status;

  if (!gst_vaapi_filter_set_cropping_rectangle (texture->filter, crop_rect))
    return FALSE;

  status = gst_vaapi_filter_process (texture->filter, surface, texture->surface,
      flags);
  if (status != GST_VAAPI_FILTER_STATUS_SUCCESS)
    return FALSE;
  return TRUE;
}

static void
do_upload_surface (UploadSurfaceArgs * args)
{
  GstVaapiTextureEGL *const texture = args->texture;
  EglContextState old_cs;

  args->success = FALSE;

  GST_VAAPI_OBJECT_LOCK_DISPLAY (texture);
  if (egl_context_set_current (texture->egl_context, TRUE, &old_cs)) {
    args->success = do_upload_surface_unlocked (texture, args->surface,
        args->crop_rect, args->flags);
    egl_context_set_current (texture->egl_context, FALSE, &old_cs);
  }
  GST_VAAPI_OBJECT_UNLOCK_DISPLAY (texture);
}

static gboolean
gst_vaapi_texture_egl_create (GstVaapiTextureEGL * texture)
{
  CreateTextureArgs args = { texture };

  egl_object_replace (&texture->egl_context,
      GST_VAAPI_DISPLAY_EGL_CONTEXT (GST_VAAPI_OBJECT_DISPLAY (texture)));

  return egl_context_run (texture->egl_context,
      (EglContextRunFunc) do_create_texture, &args) && args.success;
}

static void
gst_vaapi_texture_egl_destroy (GstVaapiTextureEGL * texture)
{
  egl_context_run (texture->egl_context,
      (EglContextRunFunc) do_destroy_texture, texture);
}

static gboolean
gst_vaapi_texture_egl_put_surface (GstVaapiTextureEGL * texture,
    GstVaapiSurface * surface, const GstVaapiRectangle * crop_rect, guint flags)
{
  UploadSurfaceArgs args = { texture, surface, crop_rect, flags };

  return egl_context_run (texture->egl_context,
      (EglContextRunFunc) do_upload_surface, &args) && args.success;
}

static void
gst_vaapi_texture_egl_class_init (GstVaapiTextureEGLClass * klass)
{
  GstVaapiObjectClass *const object_class = GST_VAAPI_OBJECT_CLASS (klass);
  GstVaapiTextureClass *const texture_class = GST_VAAPI_TEXTURE_CLASS (klass);

  object_class->finalize = (GstVaapiObjectFinalizeFunc)
      gst_vaapi_texture_egl_destroy;
  texture_class->allocate = (GstVaapiTextureAllocateFunc)
      gst_vaapi_texture_egl_create;
  texture_class->put_surface = (GstVaapiTexturePutSurfaceFunc)
      gst_vaapi_texture_egl_put_surface;
}

#define gst_vaapi_texture_egl_finalize gst_vaapi_texture_egl_destroy
GST_VAAPI_OBJECT_DEFINE_CLASS_WITH_CODE (GstVaapiTextureEGL,
    gst_vaapi_texture_egl, gst_vaapi_texture_egl_class_init (&g_class));

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
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), NULL);

  return gst_vaapi_texture_new_internal (GST_VAAPI_TEXTURE_CLASS
      (gst_vaapi_texture_egl_class ()), display, GST_VAAPI_ID_INVALID, target,
      format, width, height);
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
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), NULL);
  g_return_val_if_fail (texture_id != GL_NONE, NULL);

  return gst_vaapi_texture_new_internal (GST_VAAPI_TEXTURE_CLASS
      (gst_vaapi_texture_egl_class ()), display, texture_id, target, format,
      width, height);
}
