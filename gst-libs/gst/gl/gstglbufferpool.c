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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglbufferpool.h"

/* bufferpool */
struct _GstGLBufferPoolPrivate
{
  GstAllocator *allocator;
  GstAllocationParams params;
  GstCaps *caps;
  gint im_format;
  GstVideoInfo info;
  guint padded_width;
  guint padded_height;
  gboolean add_videometa;
};

static void gst_gl_buffer_pool_finalize (GObject * object);

#define GST_GL_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_GL_BUFFER_POOL, GstGLBufferPoolPrivate))

#define gst_gl_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstGLBufferPool, gst_gl_buffer_pool, GST_TYPE_BUFFER_POOL);

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
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_ref (caps);
  priv->info = info;

  priv->add_videometa = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

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
    return FALSE;;
  }
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
  GstMemory *gl_mem;

  info = &priv->info;

  if (!(buf = gst_buffer_new ())) {
    goto no_buffer;
  }

  if (!(gl_mem =
          gst_gl_memory_alloc (glpool->display, GST_VIDEO_INFO_FORMAT (info),
              GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info))))
    goto mem_create_failed;
  gst_buffer_append_memory (buf, gl_mem);

  if (priv->add_videometa) {
    GST_DEBUG_OBJECT (pool, "adding GstGLMeta");
    /* these are just the defaults for now */
    gst_buffer_add_video_meta (buf, 0,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info));
  }

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
    GST_WARNING_OBJECT (pool, "Could create GL Memory");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_gl_buffer_pool_new (GstGLDisplay * display)
{
  GstGLBufferPool *pool;

  pool = g_object_new (GST_TYPE_GL_BUFFER_POOL, NULL);
  pool->display = gst_object_ref (display);

  GST_LOG_OBJECT (pool, "new GL buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
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
}

static void
gst_gl_buffer_pool_init (GstGLBufferPool * pool)
{
  pool->priv = GST_GL_BUFFER_POOL_GET_PRIVATE (pool);
}

static void
gst_gl_buffer_pool_finalize (GObject * object)
{
  GstGLBufferPool *pool = GST_GL_BUFFER_POOL_CAST (object);
  GstGLBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize GL buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  G_OBJECT_CLASS (gst_gl_buffer_pool_parent_class)->finalize (object);
}
