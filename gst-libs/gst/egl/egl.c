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


struct _GstEGLImageBufferPoolPrivate
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoInfo info;
  gboolean add_metavideo;
  gboolean want_eglimage;
  GstBuffer *last_buffer;
  GstEGLImageBufferPoolSendBlockingAllocate send_blocking_allocate_func;
  gpointer send_blocking_allocate_data;
  GDestroyNotify send_blocking_allocate_destroy;
};

G_DEFINE_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

GstBufferPool *
gst_egl_image_buffer_pool_new (GstEGLImageBufferPoolSendBlockingAllocate
    blocking_allocate_func, gpointer blocking_allocate_data,
    GDestroyNotify destroy_func)
{
  GstEGLImageBufferPool *pool =
      g_object_new (GST_TYPE_EGL_IMAGE_BUFFER_POOL, NULL);
  GstEGLImageBufferPoolPrivate *priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pool, GST_TYPE_EGL_IMAGE_BUFFER_POOL,
      GstEGLImageBufferPoolPrivate);

  pool->priv = priv;

  priv->allocator = NULL;
  priv->last_buffer = NULL;
  priv->send_blocking_allocate_func = blocking_allocate_func;
  priv->send_blocking_allocate_data = blocking_allocate_data;
  priv->send_blocking_allocate_destroy = destroy_func;

  return (GstBufferPool *) pool;
}

void
gst_egl_image_buffer_pool_replace_last_buffer (GstEGLImageBufferPool * pool,
    GstBuffer * buffer)
{
  g_return_if_fail (pool != NULL);

  gst_buffer_replace (&pool->priv->last_buffer, buffer);
}

static const gchar **
gst_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstEGLImageBufferPoolPrivate *priv = pool->priv;
  GstCaps *caps;
  GstVideoInfo info;

  if (priv->allocator)
    gst_object_unref (priv->allocator);
  priv->allocator = NULL;

  if (!GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->set_config (bpool, config))
    return FALSE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (!gst_buffer_pool_config_get_allocator (config, &priv->allocator,
          &priv->params))
    return FALSE;
  if (priv->allocator)
    gst_object_ref (priv->allocator);

  priv->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  priv->want_eglimage = (priv->allocator
      && g_strcmp0 (priv->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

  priv->info = info;

  return TRUE;
}

static GstFlowReturn
gst_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstEGLImageBufferPoolPrivate *priv = pool->priv;

  *buffer = NULL;

  if (!priv->add_metavideo || !priv->want_eglimage)
    return
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
        buffer, params);

  if (!priv->allocator)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (priv->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{

      if (priv->send_blocking_allocate_func)
        *buffer = priv->send_blocking_allocate_func (bpool,
            priv->send_blocking_allocate_data);

      if (!*buffer) {
        GST_WARNING ("Fallback memory allocation");
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
            buffer, params);
      }

      return GST_FLOW_OK;
      break;
    }
    default:
      return
          GST_BUFFER_POOL_CLASS
          (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);
      break;
  }

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (*buffer && *buffer == pool->priv->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_egl_image_buffer_pool_finalize (GObject * object)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (object);
  GstEGLImageBufferPoolPrivate *priv = pool->priv;

  if (priv->allocator)
    gst_object_unref (priv->allocator);
  priv->allocator = NULL;

  gst_egl_image_buffer_pool_replace_last_buffer (pool, NULL);

  if (priv->send_blocking_allocate_destroy)
    priv->send_blocking_allocate_destroy (priv->send_blocking_allocate_data);
  priv->send_blocking_allocate_destroy = NULL;
  priv->send_blocking_allocate_data = NULL;

  G_OBJECT_CLASS (gst_egl_image_buffer_pool_parent_class)->finalize (object);
}

static void
gst_egl_image_buffer_pool_class_init (GstEGLImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstEGLImageBufferPoolPrivate));

  gobject_class->finalize = gst_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options = gst_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_egl_image_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_egl_image_buffer_pool_init (GstEGLImageBufferPool * pool)
{
}
