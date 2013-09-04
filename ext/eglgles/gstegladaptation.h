/*
 * GStreamer EGL/GLES Sink Adaptation
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

#ifndef __GST_EGL_ADAPTATION_H__
#define __GST_EGL_ADAPTATION_H__

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/gstvideopool.h>

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <gst/egl/egl.h>

#ifdef HAVE_IOS
#include <OpenGLES/ES2/gl.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

#define GST_EGLGLESSINK_EGL_MIN_VERSION 1

static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

G_BEGIN_DECLS

typedef struct _GstEglAdaptationContext GstEglAdaptationContext;
typedef struct _GstEglGlesImageFmt GstEglGlesImageFmt;

#ifdef HAVE_IOS
typedef struct _GstEaglContext GstEaglContext;
#else
typedef struct _GstEglGlesRenderContext GstEglGlesRenderContext;
#endif

typedef struct _coord5
{
  float x;
  float y;
  float z;
  float a;                      /* texpos x */
  float b;                      /* texpos y */
} coord5;

typedef struct
{
  GLuint texture;
} GstEGLGLESImageData;

/*
 * GstEglAdaptationContext:
 * @have_vbo: Set if the GLES VBO setup has been performed
 * @have_texture: Set if the GLES texture setup has been performed
 * @have_surface: Set if the EGL surface setup has been performed
 *  
 * The #GstEglAdaptationContext data structure.
 */
struct _GstEglAdaptationContext
{
  GstElement *element;

#ifdef HAVE_IOS
  GstEaglContext *eaglctx;
#else
  GstEglGlesRenderContext *eglglesctx;
#endif

  GstEGLDisplay *display, *set_display;
  EGLNativeWindowType window, used_window;

  GLuint fragshader[2]; /* frame, border */
  GLuint vertshader[2]; /* frame, border */
  GLuint glslprogram[2]; /* frame, border */
  GLuint texture[3]; /* RGB/Y, U/UV, V */
  /* shader vars */
  GLuint position_loc[2]; /* frame, border */
  GLuint texpos_loc[1]; /* frame */
  GLuint tex_scale_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  GLuint tex_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  coord5 position_array[16];    /* 4 x Frame x-normal,y-normal, 4x Frame x-normal,y-flip, 4 x Border1, 4 x Border2 */
  unsigned short index_array[4];
  unsigned int position_buffer, index_buffer;
  gint n_textures;

  EGLint surface_width;
  EGLint surface_height;
  EGLint pixel_aspect_ratio_n;
  EGLint pixel_aspect_ratio_d;

  gboolean have_vbo;
  gboolean have_texture;
  gboolean have_surface;
  gboolean buffer_preserved;
};

GST_DEBUG_CATEGORY_EXTERN (egladaption_debug);

void gst_egl_adaption_init (void);

GstEglAdaptationContext * gst_egl_adaptation_context_new (GstElement * element);
void gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_init (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_deinit (GstEglAdaptationContext * ctx);

gboolean gst_egl_adaptation_create_surface (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_query_buffer_preserved (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_query_par (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_destroy_surface (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_destroy_context (GstEglAdaptationContext * ctx);

gboolean
gst_egl_adaptation_create_egl_context (GstEglAdaptationContext * ctx);

#ifndef HAVE_IOS
EGLContext gst_egl_adaptation_context_get_egl_context (GstEglAdaptationContext * ctx);
#endif

/* platform window */
gboolean gst_egl_adaptation_create_native_window (GstEglAdaptationContext
* ctx, gint width, gint height, gpointer * own_window_data);
void gst_egl_adaptation_destroy_native_window (GstEglAdaptationContext * ctx, gpointer * own_window_data);

GstCaps *gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_init_egl_display (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_init_egl_surface (GstEglAdaptationContext * ctx, GstVideoFormat format);
void gst_egl_adaptation_init_egl_exts (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_update_surface_dimensions (GstEglAdaptationContext * ctx);
gboolean _gst_egl_choose_config (GstEglAdaptationContext * ctx, gboolean try_only, gint * num_configs);

gboolean got_gl_error (const char *wtf);
gboolean got_egl_error (const char *wtf);

void gst_egl_adaptation_set_window (GstEglAdaptationContext * ctx, guintptr window);

gboolean gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx, gboolean bind);
void gst_egl_adaptation_cleanup (GstEglAdaptationContext * ctx);

gboolean
gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx);

#ifndef HAVE_IOS
/* TODO: The goal is to move this function to gstegl lib (or
 * splitted between gstegl lib and gstgl lib) in order to be used in
 * webkitVideoSink
 * So it has to be independent of GstEglAdaptationContext */
GstBuffer *
gst_egl_image_allocator_alloc_eglimage (GstAllocator * allocator,
    GstEGLDisplay * display, EGLContext eglcontext, GstVideoFormat format,
    gint width, gint height);
#endif

G_END_DECLS

#endif /* __GST_EGL_ADAPTATION_H__ */
