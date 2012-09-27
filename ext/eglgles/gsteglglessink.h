/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
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

#ifndef __GST_EGLGLESSINK_H__
#define __GST_EGLGLESSINK_H__

#include <gst/gst.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

G_BEGIN_DECLS
#define GST_TYPE_EGLGLESSINK \
  (gst_eglglessink_get_type())
#define GST_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EGLGLESSINK,GstEglGlesSink))
#define GST_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EGLGLESSINK,GstEglGlesSinkClass))
#define GST_IS_EGLGLESSINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EGLGLESSINK))
#define GST_IS_EGLGLESSINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EGLGLESSINK))
#define GST_EGLGLESSINK_IMAGE_NOFMT 0
#define GST_EGLGLESSINK_IMAGE_RGB888 1
#define GST_EGLGLESSINK_IMAGE_RGB565 2
#define GST_EGLGLESSINK_IMAGE_RGBA8888 3
#define GST_EGLGLESSINK_EGL_MIN_VERSION 1
typedef struct _GstEglGlesBuffer GstEglGlesBuffer;
typedef struct _GstEglGlesBufferClass GstEglGlesBufferClass;

typedef struct _GstEglGlesSink GstEglGlesSink;
typedef struct _GstEglGlesSinkClass GstEglGlesSinkClass;
typedef struct _GstEglGlesRenderContext GstEglGlesRenderContext;

typedef struct _GstEglGlesImageFmt GstEglGlesImageFmt;

/* Should be extended when new rendering methods
 *   get implemented.
 */
typedef enum
{
  GST_EGLGLESSINK_RENDER_SLOW,
  GST_EGLGLESSINK_RENDER_FAST
} GstEglGlesSinkRenderingPath;

typedef struct _coord2
{
  float x;
  float y;
} coord2;

typedef struct _coord3
{
  float x;
  float y;
  float z;
} coord3;

struct _GstEglGlesRenderContext
{
  EGLConfig config;
  EGLContext eglcontext;
  EGLDisplay display;
  EGLNativeWindowType window, used_window;
  EGLSurface surface;
  GLuint fragshader, vertshader, glslprogram;
  GLuint texture[3];
  EGLint surface_width;
  EGLint surface_height;
  gint n_textures;

  /* shader vars */
  GLuint coord_pos, tex_pos;
  GLuint tex_uniform[3];
  coord3 coordarray[4];
  coord2 texarray[4];
  unsigned short indexarray[4];
  unsigned int vdata, tdata, idata;
};

struct _GstEglGlesImageFmt
{
  gint fmt;                     /* Private identifier */
  const EGLint *attribs;        /* EGL Attributes */
  GstCaps *caps;                /* Matching caps for the attribs */
};

/* XXX: Maybe use GstVideoRectangle for the image data? */
struct _GstEglGlesBuffer
{
  GstBuffer buffer;
  GstEglGlesSink *eglglessink;

  EGLint *image;
  gint format;

  gint width, height;
  size_t size;
};

struct _GstEglGlesSink
{
  GstVideoSink videosink;
  GstVideoFormat format;
  GstCaps *current_caps;

  GstEglGlesRenderContext *eglglesctx;

  GstVideoRectangle display_region;
  GList *supported_fmts;
  GstEglGlesImageFmt *selected_fmt;
  GstCaps *sinkcaps;

  GMutex *flow_lock;

  gboolean have_window;
  gboolean using_own_window;
  gboolean have_surface;;
  gboolean have_vbo;
  gboolean have_texture;
  gboolean egl_started;

  GstEglGlesSinkRenderingPath rendering_path;

  /* props */
  gboolean create_window;
  gboolean force_rendering_slow;
  gboolean force_aspect_ratio;
};

struct _GstEglGlesSinkClass
{
  GstVideoSinkClass parent_class;
};

GType gst_eglglessink_get_type (void);

G_END_DECLS
#endif /* __GST_EGLGLESSINK_H__ */
