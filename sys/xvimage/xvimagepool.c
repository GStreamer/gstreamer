/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

/* Object header */
#include "xvimagepool.h"
#include "xvimageallocator.h"

/* Debugging category */
#include <gst/gstinfo.h>

/* Helper functions */
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>


GST_DEBUG_CATEGORY_EXTERN (gst_debug_xvimagepool);
#define GST_CAT_DEFAULT gst_debug_xvimagepool


struct _GstXvImageBufferPoolPrivate
{
  GstXvImageAllocator *allocator;

  GstCaps *caps;
  gint im_format;
  GstVideoRectangle crop;
  GstVideoInfo info;
  GstVideoAlignment align;
  guint padded_width;
  guint padded_height;
  gboolean add_metavideo;
  gboolean need_alignment;
};

/* bufferpool */
static void gst_xvimage_buffer_pool_finalize (GObject * object);

#define GST_XVIMAGE_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_XVIMAGE_BUFFER_POOL, GstXvImageBufferPoolPrivate))

#define gst_xvimage_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstXvImageBufferPool, gst_xvimage_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
xvimage_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
xvimage_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstXvImageBufferPool *xvpool = GST_XVIMAGE_BUFFER_POOL_CAST (pool);
  GstXvImageBufferPoolPrivate *priv = xvpool->priv;
  GstVideoInfo info;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  GstXvContext *context;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  context = gst_xvimage_allocator_peek_context (priv->allocator);

  priv->im_format = gst_xvcontext_get_format_from_info (context, &info);
  if (priv->im_format == -1)
    goto unknown_format;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_ref (caps);

  /* enable metadata based on config of the pool */
  priv->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  priv->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    GST_LOG_OBJECT (pool, "padding %u-%ux%u-%u", priv->align.padding_top,
        priv->align.padding_left, priv->align.padding_left,
        priv->align.padding_bottom);

    /* do padding and alignment */
    gst_video_info_align (&info, &priv->align);

    /* we need the video metadata too now */
    priv->add_metavideo = TRUE;
  } else {
    gst_video_alignment_reset (&priv->align);
  }

  /* add the padding */
  priv->padded_width =
      GST_VIDEO_INFO_WIDTH (&info) + priv->align.padding_left +
      priv->align.padding_right;
  priv->padded_height =
      GST_VIDEO_INFO_HEIGHT (&info) + priv->align.padding_top +
      priv->align.padding_bottom;

  priv->info = info;
  priv->crop.x = priv->align.padding_left;
  priv->crop.y = priv->align.padding_top;
  priv->crop.w = priv->info.width;
  priv->crop.h = priv->info.height;

  gst_buffer_pool_config_set_params (config, caps, info.size, min_buffers,
      max_buffers);

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
    GST_WARNING_OBJECT (pool, "failed to get format from caps %"
        GST_PTR_FORMAT, caps);
    return FALSE;;
  }
}

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstFlowReturn
xvimage_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstXvImageBufferPool *xvpool = GST_XVIMAGE_BUFFER_POOL_CAST (pool);
  GstXvImageBufferPoolPrivate *priv = xvpool->priv;
  GstVideoInfo *info;
  GstBuffer *xvimage;
  GstMemory *mem;

  info = &priv->info;

  xvimage = gst_buffer_new ();

  mem = gst_xvimage_allocator_alloc (priv->allocator, priv->im_format,
      priv->padded_width, priv->padded_height, &priv->crop, NULL);

  if (mem == NULL) {
    gst_buffer_unref (xvimage);
    goto no_buffer;
  }
  gst_buffer_append_memory (xvimage, mem);

  if (priv->add_metavideo) {
    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
    gst_buffer_add_video_meta_full (xvimage, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);
  }

  *buffer = xvimage;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create image");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_xvimage_buffer_pool_new (GstXvImageAllocator * allocator)
{
  GstXvImageBufferPool *pool;

  pool = g_object_new (GST_TYPE_XVIMAGE_BUFFER_POOL, NULL);
  pool->priv->allocator = gst_object_ref (allocator);

  GST_LOG_OBJECT (pool, "new XvImage buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_xvimage_buffer_pool_class_init (GstXvImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstXvImageBufferPoolPrivate));

  gobject_class->finalize = gst_xvimage_buffer_pool_finalize;

  gstbufferpool_class->get_options = xvimage_buffer_pool_get_options;
  gstbufferpool_class->set_config = xvimage_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = xvimage_buffer_pool_alloc;
}

static void
gst_xvimage_buffer_pool_init (GstXvImageBufferPool * pool)
{
  pool->priv = GST_XVIMAGE_BUFFER_POOL_GET_PRIVATE (pool);
}

static void
gst_xvimage_buffer_pool_finalize (GObject * object)
{
  GstXvImageBufferPool *pool = GST_XVIMAGE_BUFFER_POOL_CAST (object);
  GstXvImageBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize XvImage buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);
  if (priv->allocator)
    gst_object_unref (priv->allocator);

  G_OBJECT_CLASS (gst_xvimage_buffer_pool_parent_class)->finalize (object);
}
