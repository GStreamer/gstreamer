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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#if defined (USE_EGL_RPI) && defined(__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#define EGL_EGLEXT_PROTOTYPES

#include <gst/egl/egl.h>
#include <string.h>

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

typedef struct
{
  GstMemory parent;

  GstEGLDisplay *display;
  EGLImageKHR image;
  GstVideoGLTextureType type;
  GstVideoGLTextureOrientation orientation;

  gpointer user_data;
  GDestroyNotify user_data_destroy;
} GstEGLImageMemory;

#define GST_EGL_IMAGE_MEMORY(mem) ((GstEGLImageMemory*)(mem))

gboolean
gst_egl_image_memory_is_mappable (void)
{
  return FALSE;
}

gboolean
gst_is_egl_image_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (mem->allocator != NULL, FALSE);

  return g_strcmp0 (mem->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0;
}

EGLImageKHR
gst_egl_image_memory_get_image (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), EGL_NO_IMAGE_KHR);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->image;
}

GstEGLDisplay *
gst_egl_image_memory_get_display (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), NULL);

  if (mem->parent)
    mem = mem->parent;

  return gst_egl_display_ref (GST_EGL_IMAGE_MEMORY (mem)->display);
}

GstVideoGLTextureType
gst_egl_image_memory_get_type (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), -1);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->type;
}

GstVideoGLTextureOrientation
gst_egl_image_memory_get_orientation (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem),
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->orientation;
}

void
gst_egl_image_memory_set_orientation (GstMemory * mem,
    GstVideoGLTextureOrientation orientation)
{
  g_return_if_fail (gst_is_egl_image_memory (mem));

  if (mem->parent)
    mem = mem->parent;

  GST_EGL_IMAGE_MEMORY (mem)->orientation = orientation;
}

static GstMemory *
gst_egl_image_allocator_alloc_vfunc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning
      ("Use gst_egl_image_allocator_alloc() to allocate from this allocator");

  return NULL;
}

static void
gst_egl_image_allocator_free_vfunc (GstAllocator * allocator, GstMemory * mem)
{
  GstEGLImageMemory *emem = (GstEGLImageMemory *) mem;
  EGLDisplay display;

  g_return_if_fail (gst_is_egl_image_memory (mem));

  /* Shared memory should not destroy all the data */
  if (!mem->parent) {
    display = gst_egl_display_get (emem->display);
    eglDestroyImageKHR (display, emem->image);

    if (emem->user_data_destroy)
      emem->user_data_destroy (emem->user_data);

    gst_egl_display_unref (emem->display);
  }

  g_slice_free (GstEGLImageMemory, emem);
}

static gpointer
gst_egl_image_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  return NULL;
}

static void
gst_egl_image_mem_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_egl_image_mem_share (GstMemory * mem, gssize offset, gssize size)
{
  GstMemory *sub;
  GstMemory *parent;

  if (offset != 0)
    return NULL;

  if (size != -1 && size != mem->size)
    return NULL;

  /* find the real parent */
  if ((parent = mem->parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->size - offset;

  sub = (GstMemory *) g_slice_new (GstEGLImageMemory);

  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->allocator, parent,
      mem->maxsize, mem->align, mem->offset + offset, size);

  return sub;
}

static GstMemory *
gst_egl_image_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
gst_egl_image_mem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

typedef GstAllocator GstEGLImageAllocator;
typedef GstAllocatorClass GstEGLImageAllocatorClass;

GType gst_egl_image_allocator_get_type (void);
G_DEFINE_TYPE (GstEGLImageAllocator, gst_egl_image_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_TYPE_EGL_IMAGE_ALLOCATOR   (gst_egl_image_mem_allocator_get_type())
#define GST_IS_EGL_IMAGE_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EGL_IMAGE_ALLOCATOR))

static void
gst_egl_image_allocator_class_init (GstEGLImageAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_egl_image_allocator_alloc_vfunc;
  allocator_class->free = gst_egl_image_allocator_free_vfunc;
}

static void
gst_egl_image_allocator_init (GstEGLImageAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_EGL_IMAGE_MEMORY_TYPE;
  alloc->mem_map = gst_egl_image_mem_map;
  alloc->mem_unmap = gst_egl_image_mem_unmap;
  alloc->mem_share = gst_egl_image_mem_share;
  alloc->mem_copy = gst_egl_image_mem_copy;
  alloc->mem_is_span = gst_egl_image_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static gpointer
gst_egl_image_allocator_init_instance (gpointer data)
{
  return g_object_new (gst_egl_image_allocator_get_type (), NULL);
}

GstAllocator *
gst_egl_image_allocator_obtain (void)
{
  static GOnce once = G_ONCE_INIT;

  g_once (&once, gst_egl_image_allocator_init_instance, NULL);

  g_return_val_if_fail (once.retval != NULL, NULL);

  return GST_ALLOCATOR (g_object_ref (once.retval));
}

GstMemory *
gst_egl_image_allocator_alloc (GstAllocator * allocator,
    GstEGLDisplay * display, GstVideoGLTextureType type, gint width,
    gint height, gsize * size)
{
  return NULL;
}

GstMemory *
gst_egl_image_allocator_wrap (GstAllocator * allocator,
    GstEGLDisplay * display, EGLImageKHR image, GstVideoGLTextureType type,
    GstMemoryFlags flags, gsize size, gpointer user_data,
    GDestroyNotify user_data_destroy)
{
  GstEGLImageMemory *mem;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);

  if (!allocator) {
    allocator = gst_egl_image_allocator_obtain ();
  }

  mem = g_slice_new (GstEGLImageMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), flags,
      allocator, NULL, size, 0, 0, size);

  mem->display = gst_egl_display_ref (display);
  mem->image = image;
  mem->type = type;
  mem->orientation = GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;

  mem->user_data = user_data;
  mem->user_data_destroy = user_data_destroy;

  return GST_MEMORY_CAST (mem);
}

GstContext *
gst_context_new_egl_display (GstEGLDisplay * display, gboolean persistent)
{
  GstContext *context;
  GstStructure *s;

  context = gst_context_new (GST_EGL_DISPLAY_CONTEXT_TYPE, persistent);
  s = gst_context_writable_structure (context);
  gst_structure_set (s, "display", GST_TYPE_EGL_DISPLAY, display, NULL);

  return context;
}

gboolean
gst_context_get_egl_display (GstContext * context, GstEGLDisplay ** display)
{
  const GstStructure *s;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);
  g_return_val_if_fail (strcmp (gst_context_get_context_type (context),
          GST_EGL_DISPLAY_CONTEXT_TYPE) == 0, FALSE);

  s = gst_context_get_structure (context);
  return gst_structure_get (s, "display", GST_TYPE_EGL_DISPLAY, display, NULL);
}

struct _GstEGLDisplay
{
  EGLDisplay display;
  volatile gint refcount;
  GDestroyNotify destroy_notify;
};

GstEGLDisplay *
gst_egl_display_new (EGLDisplay display, GDestroyNotify destroy_notify)
{
  GstEGLDisplay *gdisplay;

  gdisplay = g_slice_new (GstEGLDisplay);
  gdisplay->display = display;
  gdisplay->refcount = 1;
  gdisplay->destroy_notify = destroy_notify;

  return gdisplay;
}

GstEGLDisplay *
gst_egl_display_ref (GstEGLDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  g_atomic_int_inc (&display->refcount);

  return display;
}

void
gst_egl_display_unref (GstEGLDisplay * display)
{
  g_return_if_fail (display != NULL);

  if (g_atomic_int_dec_and_test (&display->refcount)) {
    if (display->destroy_notify)
      display->destroy_notify (display->display);
    g_slice_free (GstEGLDisplay, display);
  }
}

EGLDisplay
gst_egl_display_get (GstEGLDisplay * display)
{
  g_return_val_if_fail (display != NULL, EGL_NO_DISPLAY);

  return display->display;
}

G_DEFINE_BOXED_TYPE (GstEGLDisplay, gst_egl_display,
    (GBoxedCopyFunc) gst_egl_display_ref,
    (GBoxedFreeFunc) gst_egl_display_unref);
