/*
 * GStreamer EGL Library 
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * *
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

#ifndef __GST_EGL_H__
#define __GST_EGL_H__

#include <gst/gst.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GST_EGL_IMAGE_MEMORY_TYPE "EGLImage"

#define GST_CAPS_FEATURE_MEMORY_EGL_IMAGE "memory:EGLImage"

typedef enum
{
  GST_EGL_IMAGE_MEMORY_TYPE_INVALID = -1,
  /* GL formats */
  GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE = 0x0000,
  GST_EGL_IMAGE_MEMORY_TYPE_LUMINANCE_ALPHA,
  GST_EGL_IMAGE_MEMORY_TYPE_RGB16,
  GST_EGL_IMAGE_MEMORY_TYPE_RGB,
  GST_EGL_IMAGE_MEMORY_TYPE_RGBA,
  /* YUV formats */
  /* GST_EGL_IMAGE_MEMORY_TYPE_YUV420P = 0x1000, */
  /* Other */
  GST_EGL_IMAGE_MEMORY_TYPE_OTHER = 0xffff
} GstEGLImageType;

typedef enum
{
  /* GStreamer orientation, top line first in memory, left row first */
  GST_EGL_IMAGE_ORIENTATION_X_NORMAL_Y_NORMAL,
  /* OpenGL orientation, bottom line first in memory, left row first */
  GST_EGL_IMAGE_ORIENTATION_X_NORMAL_Y_FLIP,
  /* Just for the sake of completeness, nothing uses this probably */
  GST_EGL_IMAGE_ORIENTATION_X_FLIP_Y_NORMAL,
  GST_EGL_IMAGE_ORIENTATION_X_FLIP_Y_FLIP
} GstEGLImageOrientation;

typedef struct _GstEGLDisplay GstEGLDisplay;

/* EGLImage GstMemory handling */
gboolean gst_egl_image_memory_is_mappable (void);
gboolean gst_is_egl_image_memory (GstMemory * mem);
EGLImageKHR gst_egl_image_memory_get_image (GstMemory * mem);
GstEGLDisplay *gst_egl_image_memory_get_display (GstMemory * mem);
GstEGLImageType gst_egl_image_memory_get_type (GstMemory * mem);
GstEGLImageOrientation gst_egl_image_memory_get_orientation (GstMemory * mem);
void gst_egl_image_memory_set_orientation (GstMemory * mem,
    GstEGLImageOrientation orientation);

/* Generic EGLImage allocator that doesn't support mapping, copying or anything */
GstAllocator *gst_egl_image_allocator_obtain (void);
GstMemory *gst_egl_image_allocator_alloc (GstAllocator * allocator,
    GstEGLDisplay * display, GstEGLImageType type, gint width, gint height,
    gsize * size);
GstMemory *gst_egl_image_allocator_wrap (GstAllocator * allocator,
    GstEGLDisplay * display, EGLImageKHR image, GstEGLImageType type,
    GstMemoryFlags flags, gsize size, gpointer user_data,
    GDestroyNotify user_data_destroy);

#define GST_EGL_DISPLAY_CONTEXT_TYPE "gst.egl.EGLDisplay"
void gst_context_set_egl_display (GstContext * context,
    GstEGLDisplay * display);
gboolean gst_context_get_egl_display (GstContext * context,
    GstEGLDisplay ** display);

/* EGLDisplay wrapper with refcount, connection is closed after last ref is gone */
#define GST_TYPE_EGL_DISPLAY (gst_egl_display_get_type())
GType gst_egl_display_get_type (void);

GstEGLDisplay *gst_egl_display_new (EGLDisplay display);
GstEGLDisplay *gst_egl_display_ref (GstEGLDisplay * display);
void gst_egl_display_unref (GstEGLDisplay * display);
EGLDisplay gst_egl_display_get (GstEGLDisplay * display);

#endif /* __GST_EGL_H__ */
