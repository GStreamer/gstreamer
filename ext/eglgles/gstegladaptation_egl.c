/*
 * Copyright (C) 2012-2013 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *   @author: Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstegladaptation.h"
#include "video_platform_wrapper.h"
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gst/egl/egl.h>

#define GST_CAT_DEFAULT egladaption_debug

/* Some EGL implementations are reporting wrong
 * values for the display's EGL_PIXEL_ASPECT_RATIO.
 * They are required by the khronos specs to report
 * this value as w/h * EGL_DISPLAY_SCALING (Which is
 * a constant with value 10000) but at least the
 * Galaxy SIII (Android) is reporting just 1 when
 * w = h. We use these two to bound returned values to
 * sanity.
 */
#define EGL_SANE_DAR_MIN ((EGL_DISPLAY_SCALING)/10)
#define EGL_SANE_DAR_MAX ((EGL_DISPLAY_SCALING)*10)

/*
 * GstEglGlesRenderContext:
 * @config: Current EGL config
 * @eglcontext: Current EGL context
 * @egl_minor: EGL version (minor)
 * @egl_major: EGL version (major)
 *
 * This struct holds the sink's EGL/GLES rendering context.
 */
struct _GstEglGlesRenderContext
{
  EGLConfig config;
  EGLContext eglcontext;
  EGLSurface surface;
  EGLint egl_minor, egl_major;
};

gboolean
got_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS) {
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned 0x%04x", wtf,
        error);
    return TRUE;
  }

  return FALSE;
}

/* Prints available EGL/GLES extensions 
 * If another rendering path is implemented this is the place
 * where you want to check for the availability of its supporting
 * EGL/GLES extensions.
 */
void
gst_egl_adaptation_init_egl_exts (GstEglAdaptationContext * ctx)
{
#ifndef GST_DISABLE_GST_DEBUG
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (gst_egl_display_get (ctx->display), EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (ctx->element, "Available EGL extensions: %s\n",
      GST_STR_NULL (eglexts));
  GST_DEBUG_OBJECT (ctx->element, "Available GLES extensions: %s\n",
      GST_STR_NULL ((const char *) glexts));
#endif
  return;
}

gboolean
gst_egl_adaptation_init_egl_display (GstEglAdaptationContext * ctx)
{
  GstMessage *msg;
  EGLDisplay display;
  GST_DEBUG_OBJECT (ctx->element, "Enter EGL initial configuration");

  if (!platform_wrapper_init ()) {
    GST_ERROR_OBJECT (ctx->element, "Couldn't init EGL platform wrapper");
    goto HANDLE_ERROR;
  }

  msg =
      gst_message_new_need_context (GST_OBJECT_CAST (ctx->element),
      GST_EGL_DISPLAY_CONTEXT_TYPE);
  gst_element_post_message (GST_ELEMENT_CAST (ctx->element), msg);

  GST_OBJECT_LOCK (ctx->element);
  if (!ctx->set_display) {
    GstContext *context;

    GST_OBJECT_UNLOCK (ctx->element);

    display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
      GST_ERROR_OBJECT (ctx->element, "Could not get EGL display connection");
      goto HANDLE_ERROR;        /* No EGL error is set by eglGetDisplay() */
    }
    ctx->display = gst_egl_display_new (display, (GDestroyNotify) eglTerminate);

    context = gst_context_new_egl_display (ctx->display, FALSE);
    msg = gst_message_new_have_context (GST_OBJECT (ctx->element), context);
    gst_element_post_message (GST_ELEMENT_CAST (ctx->element), msg);
  }

  if (!eglInitialize (gst_egl_display_get (ctx->display),
          &ctx->eglglesctx->egl_major, &ctx->eglglesctx->egl_minor)) {
    got_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (ctx->element, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version
   * XXX: Need to review the version requirement in terms of the needed API
   */
  if (ctx->eglglesctx->egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (ctx->element, "EGL v%d needed, but you only have v%d.%d",
        GST_EGLGLESSINK_EGL_MIN_VERSION, ctx->eglglesctx->egl_major,
        ctx->eglglesctx->egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (ctx->element, "System reports supported EGL version v%d.%d",
      ctx->eglglesctx->egl_major, ctx->eglglesctx->egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup window/surface from handle");
  return FALSE;
}

gboolean
gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx,
    gboolean bind)
{
  g_assert (ctx->display != NULL);

  if (bind && ctx->eglglesctx->surface && ctx->eglglesctx->eglcontext) {
    EGLContext *cur_ctx = eglGetCurrentContext ();

    if (cur_ctx == ctx->eglglesctx->eglcontext) {
      GST_DEBUG_OBJECT (ctx->element,
          "Already attached the context to thread %p", g_thread_self ());
      return TRUE;
    }

    GST_DEBUG_OBJECT (ctx->element, "Attaching context to thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (gst_egl_display_get (ctx->display),
            ctx->eglglesctx->surface, ctx->eglglesctx->surface,
            ctx->eglglesctx->eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't bind context");
      return FALSE;
    }
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Detaching context from thread %p",
        g_thread_self ());
    if (!eglMakeCurrent (gst_egl_display_get (ctx->display),
            EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
      got_egl_error ("eglMakeCurrent");
      GST_ERROR_OBJECT (ctx->element, "Couldn't unbind context");
      return FALSE;
    }
  }

  return TRUE;
}

/* XXX: Lock eglgles context? */
gboolean
gst_egl_adaptation_update_surface_dimensions (GstEglAdaptationContext * ctx)
{
  gint width, height;

  /* Save surface dims */
  eglQuerySurface (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface, EGL_WIDTH, &width);
  eglQuerySurface (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface, EGL_HEIGHT, &height);

  if (width != ctx->surface_width || height != ctx->surface_height) {
    ctx->surface_width = width;
    ctx->surface_height = height;
    GST_INFO_OBJECT (ctx->element, "Got surface of %dx%d pixels", width,
        height);
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx)
{
  gboolean ret = eglSwapBuffers (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->surface);
  if (ret == EGL_FALSE) {
    got_egl_error ("eglSwapBuffers");
  }
  return ret;
}

gboolean
_gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only,
    gint * num_configs)
{
  EGLint cfg_number;
  gboolean ret;
  EGLConfig *config = NULL;

  if (!try_only)
    config = &ctx->eglglesctx->config;

  ret = eglChooseConfig (gst_egl_display_get (ctx->display),
      eglglessink_RGBA8888_attribs, config, 1, &cfg_number) != EGL_FALSE;

  if (!ret)
    got_egl_error ("eglChooseConfig");
  else if (num_configs)
    *num_configs = cfg_number;
  return ret;
}

gboolean
gst_egl_adaptation_create_surface (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx->surface =
      eglCreateWindowSurface (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->config, ctx->used_window, NULL);

  if (ctx->eglglesctx->surface == EGL_NO_SURFACE) {
    got_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    return FALSE;
  }
  return TRUE;
}

void
gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx)
{
  EGLint swap_behavior;

  ctx->buffer_preserved = FALSE;
  if (eglQuerySurface (gst_egl_display_get (ctx->display),
          ctx->eglglesctx->surface, EGL_SWAP_BEHAVIOR, &swap_behavior)) {
    GST_DEBUG_OBJECT (ctx->element, "Buffer swap behavior %x", swap_behavior);
    ctx->buffer_preserved = swap_behavior == EGL_BUFFER_PRESERVED;
  } else {
    GST_DEBUG_OBJECT (ctx->element, "Can't query buffer swap behavior");
  }
}

void
gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx)
{
  EGLint display_par;

  /* fixed value */
  ctx->pixel_aspect_ratio_d = EGL_DISPLAY_SCALING;

  /* Save display's pixel aspect ratio
   *
   * DAR is reported as w/h * EGL_DISPLAY_SCALING wich is
   * a constant with value 10000. This attribute is only
   * supported if the EGL version is >= 1.2
   * XXX: Setup this as a property.
   * or some other one time check. Right now it's being called once
   * per frame.
   */
  if (ctx->eglglesctx->egl_major == 1 && ctx->eglglesctx->egl_minor < 2) {
    GST_DEBUG_OBJECT (ctx->element, "Can't query PAR. Using default: %dx%d",
        EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
    ctx->pixel_aspect_ratio_n = EGL_DISPLAY_SCALING;
  } else {
    eglQuerySurface (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->surface, EGL_PIXEL_ASPECT_RATIO, &display_par);
    /* Fix for outbound DAR reporting on some implementations not
     * honoring the 'should return w/h * EGL_DISPLAY_SCALING' spec
     * requirement
     */
    if (display_par == EGL_UNKNOWN || display_par < EGL_SANE_DAR_MIN ||
        display_par > EGL_SANE_DAR_MAX) {
      GST_DEBUG_OBJECT (ctx->element, "Nonsensical PAR value returned: %d. "
          "Bad EGL implementation? "
          "Will use default: %d/%d", ctx->pixel_aspect_ratio_n,
          EGL_DISPLAY_SCALING, EGL_DISPLAY_SCALING);
      ctx->pixel_aspect_ratio_n = EGL_DISPLAY_SCALING;
    } else {
      ctx->pixel_aspect_ratio_n = display_par;
    }
  }
}

gboolean
gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

  ctx->eglglesctx->eglcontext =
      eglCreateContext (gst_egl_display_get (ctx->display),
      ctx->eglglesctx->config, EGL_NO_CONTEXT, con_attribs);

  if (ctx->eglglesctx->eglcontext == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (ctx->element, "EGL call returned error %x",
        eglGetError ());
    return FALSE;
  }

  GST_DEBUG_OBJECT (ctx->element, "EGL Context: %p",
      ctx->eglglesctx->eglcontext);

  return TRUE;
}

EGLContext
gst_egl_adaptation_context_get_egl_context (GstEglAdaptationContext * ctx)
{
  g_return_val_if_fail (ctx != NULL, EGL_NO_CONTEXT);

  return ctx->eglglesctx->eglcontext;
}

static void
gst_egl_gles_image_data_free (GstEGLGLESImageData * data)
{
  glDeleteTextures (1, &data->texture);
  g_slice_free (GstEGLGLESImageData, data);
}


GstBuffer *
gst_egl_image_allocator_alloc_eglimage (GstAllocator * allocator,
    GstEGLDisplay * display, EGLContext eglcontext, GstVideoFormat format,
    gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  gint i;
  gint stride[3];
  gsize offset[3];
  GstMemory *mem[3] = { NULL, NULL, NULL };
  guint n_mem;
  GstMemoryFlags flags = 0;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  if (!gst_egl_image_memory_is_mappable ())
    flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  /* See https://bugzilla.gnome.org/show_bug.cgi?id=695203 */
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  gst_video_info_set_format (&info, format, width, height);

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 3);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (display),
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (guintptr) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      EGLImageKHR image;
      gsize size;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 2);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
            NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (display),
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (guintptr) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      EGLImageKHR image;
      gsize size[2];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_COMP_WIDTH (&info, 1),
          GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);

      if (mem[0] && mem[1]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 2;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        mem[0] = mem[1] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1) * 2);
        offset[1] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[0] = offset[1];
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);

        for (i = 0; i < 2; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          if (i == 0)
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
                GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              eglCreateImageKHR (gst_egl_display_get (display),
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (guintptr) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image,
              (i ==
                  0 ? GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE :
                  GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA),
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 2;
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      EGLImageKHR image;
      gsize size[3];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              1), GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);
      mem[2] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              2), GST_VIDEO_INFO_COMP_HEIGHT (&info, 2), &size[2]);

      if (mem[0] && mem[1] && mem[2]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[2] = size[1];
        stride[2] = size[2] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 3;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[2], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        if (mem[2])
          gst_memory_unref (mem[2]);
        mem[0] = mem[1] = mem[2] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1));
        stride[2] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 2));
        size[0] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);
        size[2] = stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 2);
        offset[0] = 0;
        offset[1] = size[0];
        offset[2] = offset[1] + size[1];

        for (i = 0; i < 3; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
              GST_VIDEO_INFO_COMP_WIDTH (&info, i),
              GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
              GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              eglCreateImageKHR (gst_egl_display_get (display),
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (guintptr) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image, GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE,
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 3;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            eglCreateImageKHR (gst_egl_display_get (display),
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (guintptr) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGBA,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);

        n_mem = 1;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, 0, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;

mem_error:
  {
    GST_ERROR_OBJECT (GST_CAT_DEFAULT, "Failed to create EGLImage");

    if (data)
      gst_egl_gles_image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);
    if (mem[1])
      gst_memory_unref (mem[1]);
    if (mem[2])
      gst_memory_unref (mem[2]);

    return NULL;
  }
}

void
gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx,
    gpointer * own_window_data)
{
  platform_destroy_native_window (gst_egl_display_get
      (ctx->display), ctx->used_window, own_window_data);
  ctx->used_window = 0;
}

gboolean
gst_egl_adaptation_create_native_window (GstEglAdaptationContext * ctx,
    gint width, gint height, gpointer * own_window_data)
{
  EGLNativeWindowType window =
      platform_create_native_window (width, height, own_window_data);
  if (window)
    gst_egl_adaptation_set_window (ctx, (guintptr) window);
  GST_DEBUG_OBJECT (ctx->element, "Using window handle %p", (gpointer) window);
  return window != 0;
}

void
gst_egl_adaptation_set_window (GstEglAdaptationContext * ctx, guintptr window)
{
  ctx->window = (EGLNativeWindowType) window;
}

void
gst_egl_adaptation_init (GstEglAdaptationContext * ctx)
{
  ctx->eglglesctx = g_new0 (GstEglGlesRenderContext, 1);
}

void
gst_egl_adaptation_deinit (GstEglAdaptationContext * ctx)
{
  g_free (ctx->eglglesctx);
}

void
gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->surface) {
    eglDestroySurface (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->surface);
    ctx->eglglesctx->surface = NULL;
    ctx->have_surface = FALSE;
  }
}

void
gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx)
{
  if (ctx->eglglesctx->eglcontext) {
    eglDestroyContext (gst_egl_display_get (ctx->display),
        ctx->eglglesctx->eglcontext);
    ctx->eglglesctx->eglcontext = NULL;
  }
}
