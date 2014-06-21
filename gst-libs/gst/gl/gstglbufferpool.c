/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gl.h"
#include "gstglbufferpool.h"

#if GST_GL_HAVE_PLATFORM_EGL
#include <gst/gl/egl/gsteglimagememory.h>
#endif

/**
 * SECTION:gstglbufferpool
 * @short_description: buffer pool for #GstGLMemory objects
 * @see_also: #GstBufferPool, #GstGLMemory
 *
 * a #GstGLBufferPool is an object that allocates buffers with #GstGLMemory
 *
 * A #GstGLBufferPool is created with gst_gl_buffer_pool_new()
 *
 * #GstGLBufferPool implements the VideoMeta buffer pool option 
 * #GST_BUFFER_POOL_OPTION_VIDEO_META
 */

/* bufferpool */
struct _GstGLBufferPoolPrivate
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  gint im_format;
  GstVideoInfo info;
  gboolean add_videometa;
  gboolean want_eglimage;
  GstBuffer *last_buffer;
};

static void gst_gl_buffer_pool_finalize (GObject * object);

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_BUFFER_POOL);
#define GST_CAT_DEFAULT GST_CAT_GL_BUFFER_POOL

#define GST_GL_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_GL_BUFFER_POOL, GstGLBufferPoolPrivate))

#define gst_gl_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLBufferPool, gst_gl_buffer_pool,
    GST_TYPE_BUFFER_POOL, GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_BUFFER_POOL,
        "glbufferpool", 0, "GL Buffer Pool"));

static const gchar **
gst_gl_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_gl_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstGLBufferPool *glpool = GST_GL_BUFFER_POOL_CAST (pool);
  GstGLBufferPoolPrivate *priv = glpool->priv;
  GstVideoInfo info;
  GstCaps *caps;
  GstAllocator *allocator;
  GstAllocationParams alloc_params;
  gboolean reset = TRUE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &alloc_params))
    goto wrong_config;

  if (!allocator) {
    gst_gl_memory_init ();
    allocator = gst_allocator_find (GST_GL_MEMORY_ALLOCATOR);
  }
  priv->allocator = allocator;
  priv->params = alloc_params;

  priv->im_format = GST_VIDEO_INFO_FORMAT (&info);
  if (priv->im_format == -1)
    goto unknown_format;

  if (priv->caps)
    reset = !gst_caps_is_equal (priv->caps, caps);

  gst_caps_replace (&priv->caps, caps);
  priv->info = info;

  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

#if GST_GL_HAVE_PLATFORM_EGL
  priv->want_eglimage = (priv->allocator
      && g_strcmp0 (priv->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);
#else
  priv->want_eglimage = FALSE;
#endif

  if (reset) {
    if (glpool->upload)
      gst_object_unref (glpool->upload);

    glpool->upload = gst_gl_upload_meta_new (glpool->context);
  }

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
unknown_format:
  {
    GST_WARNING_OBJECT (glpool, "failed to get format from caps %"
        GST_PTR_FORMAT, caps);
    GST_ELEMENT_ERROR (glpool, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            priv->info.width, priv->info.height),
        ("Invalid input caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }
}

static gboolean
gst_gl_buffer_pool_start (GstBufferPool * pool)
{
  GstGLBufferPool *glpool = GST_GL_BUFFER_POOL_CAST (pool);
  GstGLBufferPoolPrivate *priv = glpool->priv;

  gst_gl_upload_meta_set_format (glpool->upload, &priv->info);

  return GST_BUFFER_POOL_CLASS (parent_class)->start (pool);
}

/* This function handles GstBuffer creation */
static GstFlowReturn
gst_gl_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstGLBufferPool *glpool = GST_GL_BUFFER_POOL_CAST (pool);
  GstGLBufferPoolPrivate *priv = glpool->priv;
  GstVideoInfo *info;
  GstBuffer *buf;

  info = &priv->info;

  if (!(buf = gst_buffer_new ())) {
    goto no_buffer;
  }
#if GST_GL_HAVE_PLATFORM_EGL
  if (priv->want_eglimage) {
    /* alloc and append memories, also add video_meta and
     * texture_upload_meta */
    if (!gst_egl_image_memory_setup_buffer (glpool->context, info, buf))
      goto egl_image_mem_create_failed;

    *buffer = buf;

    return GST_FLOW_OK;
  }
#endif

  if (!gst_gl_memory_setup_buffer (glpool->context, info, buf))
    goto mem_create_failed;

  gst_gl_upload_meta_add_to_buffer (glpool->upload, buf);

  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create image");
    return GST_FLOW_ERROR;
  }
mem_create_failed:
  {
    GST_WARNING_OBJECT (pool, "Could not create GL Memory");
    return GST_FLOW_ERROR;
  }

#if GST_GL_HAVE_PLATFORM_EGL
egl_image_mem_create_failed:
  {
    GST_WARNING_OBJECT (pool, "Could not create EGLImage Memory");
    return GST_FLOW_ERROR;
  }
#endif
}


static GstFlowReturn
gst_gl_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstGLBufferPool *glpool = NULL;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_gl_buffer_pool_parent_class)->acquire_buffer (bpool, buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  glpool = GST_GL_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (glpool->priv->want_eglimage && *buffer
      && *buffer == glpool->priv->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_gl_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) glpool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

/**
 * gst_gl_buffer_pool_new:
 * @display: the #GstGLDisplay to use
 *
 * Returns: a #GstBufferPool that allocates buffers with #GstGLMemory
 */
GstBufferPool *
gst_gl_buffer_pool_new (GstGLContext * context)
{
  GstGLBufferPool *pool;

  pool = g_object_new (GST_TYPE_GL_BUFFER_POOL, NULL);
  pool->context = gst_object_ref (context);

  GST_LOG_OBJECT (pool, "new GL buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

/**
 * gst_gl_buffer_pool_replace_last_buffer:
 * @pool: a #GstGLBufferPool
 * @buffer: a #GstBuffer
 *
 * Set @pool<--  -->s last buffer to @buffer for #GstGLPlatform<--  -->s that
 * require it.
 */
void
gst_gl_buffer_pool_replace_last_buffer (GstGLBufferPool * pool,
    GstBuffer * buffer)
{
  g_return_if_fail (pool != NULL);
  g_return_if_fail (buffer != NULL);

  gst_buffer_replace (&pool->priv->last_buffer, buffer);
}

static void
gst_gl_buffer_pool_class_init (GstGLBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstGLBufferPoolPrivate));

  gobject_class->finalize = gst_gl_buffer_pool_finalize;

  gstbufferpool_class->get_options = gst_gl_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_gl_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_gl_buffer_pool_alloc;
  gstbufferpool_class->acquire_buffer = gst_gl_buffer_pool_acquire_buffer;
  gstbufferpool_class->start = gst_gl_buffer_pool_start;
}

static void
gst_gl_buffer_pool_init (GstGLBufferPool * pool)
{
  GstGLBufferPoolPrivate *priv = NULL;

  pool->priv = GST_GL_BUFFER_POOL_GET_PRIVATE (pool);
  priv = pool->priv;

  priv->allocator = NULL;
  priv->caps = NULL;
  priv->im_format = GST_VIDEO_FORMAT_UNKNOWN;
  priv->add_videometa = TRUE;
  priv->want_eglimage = FALSE;
  priv->last_buffer = FALSE;

  gst_video_info_init (&priv->info);
  gst_allocation_params_init (&priv->params);
}

static void
gst_gl_buffer_pool_finalize (GObject * object)
{
  GstGLBufferPool *pool = GST_GL_BUFFER_POOL_CAST (object);
  GstGLBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize GL buffer pool %p", pool);

  gst_buffer_replace (&pool->priv->last_buffer, NULL);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  if (pool->upload)
    gst_object_unref (pool->upload);

  G_OBJECT_CLASS (gst_gl_buffer_pool_parent_class)->finalize (object);

  /* only release the context once all our memory have been deleted */
  if (pool->context) {
    gst_object_unref (pool->context);
    pool->context = NULL;
  }
}
