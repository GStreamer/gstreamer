/*
 * GStreamer Android Video Platform Wrapper 
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * General idea is to have all platform dependent code here for easy
 * tweaking and isolation from the main routines
 */

#ifndef __GST_ANDROID_VIDEO_PLATFORM_WRAPPER__
#define __GST_ANDROID_VIDEO_PLATFORM_WRAPPER__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <EGL/egl.h>

gboolean platform_wrapper_init (void);
EGLNativeWindowType platform_create_native_window (gint width, gint height, gpointer * window_data);
gboolean platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType w, gpointer * window_data);

GstBufferPool *platform_create_buffer_pool (EGLDisplay display);

#define GST_EGL_IMAGE_MEMORY_NAME "GstEGLImage"
typedef struct
{
  GstMemory parent;
  
  GstVideoFormat format;
  gint width, height;

  GMutex lock;

  /* Always in order RGB/Y, UV/U, V */
  EGLImageKHR image[3];
  gpointer image_platform_data[3];
  GLuint texture[3];
  gint stride[3];
  gsize offset[3];
  guint n_textures;

  gpointer memory[3];
  gpointer memory_platform_data[3];
  gint memory_refcount[3];
  GstMapFlags memory_flags[3];

  gpointer mapped_memory;
  gint mapped_memory_refcount;
  GstMapFlags mapped_memory_flags;
} GstEGLImageMemory;

#define GST_EGL_IMAGE_MEMORY(m) ((GstEGLImageMemory*)(m))

typedef gboolean (*PlatformMapVideo)    (GstVideoMeta *meta, guint plane, GstMapInfo *info, gpointer *data, gint * stride, GstMapFlags flags);
typedef gboolean (*PlatformUnmapVideo)  (GstVideoMeta *meta, guint plane, GstMapInfo *info);

extern PlatformMapVideo default_map_video;
extern PlatformUnmapVideo default_unmap_video;

gboolean platform_can_map_eglimage (GstMemoryMapFunction *map, GstMemoryUnmapFunction *unmap, PlatformMapVideo *video_map, PlatformUnmapVideo *video_unmap);
gboolean platform_has_custom_eglimage_alloc (void);
gboolean platform_alloc_eglimage (EGLDisplay display, EGLContext context, GLint format, GLint type, gint width, gint height, GLuint tex_id, EGLImageKHR *image, gpointer *image_platform_data);
void platform_free_eglimage (EGLDisplay display, EGLContext context, GLuint tex_id, EGLImageKHR *image, gpointer *image_platform_data);

#endif
