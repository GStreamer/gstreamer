/*
 * gst-plugins-bad
 * Copyright (C) 2012 Edward Hervey <edward@collabora.com>
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

#include "gstvdpvideobufferpool.h"
#include "gstvdpvideomemory.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_vidbufpool_debug);
#define GST_CAT_DEFAULT gst_vdp_vidbufpool_debug

static void gst_vdp_video_buffer_pool_finalize (GObject * object);

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_vidbufpool_debug, "vdpvideopool", 0, \
    "VDPAU Video bufferpool");

#define gst_vdp_video_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVdpVideoBufferPool, gst_vdp_video_buffer_pool,
    GST_TYPE_BUFFER_POOL, DEBUG_INIT);

static const gchar **
gst_vdp_video_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VDP_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_vdp_video_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstVdpVideoBufferPool *vdppool = GST_VDP_VIDEO_BUFFER_POOL_CAST (pool);
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  if (GST_VIDEO_INFO_FORMAT (&info) == GST_VIDEO_FORMAT_UNKNOWN)
    goto unknown_format;

  vdppool->info = info;

  /* enable metadata based on config of the pool */
  vdppool->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  vdppool->add_vdpmeta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VDP_VIDEO_META);

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
    GST_WARNING_OBJECT (vdppool, "failed to get format from caps %"
        GST_PTR_FORMAT, caps);
    GST_ELEMENT_ERROR (vdppool, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            info.width, info.height),
        ("Invalid input caps %" GST_PTR_FORMAT, caps));
    return FALSE;
  }
}

/* This function handles GstBuffer creation */
static GstFlowReturn
gst_vdp_video_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstVdpVideoBufferPool *vdppool = GST_VDP_VIDEO_BUFFER_POOL_CAST (pool);
  GstVideoInfo *info;
  GstBuffer *buf;
  GstMemory *vdp_mem;

  info = &vdppool->info;

  if (!(buf = gst_buffer_new ()))
    goto no_buffer;

  if (!(vdp_mem = gst_vdp_video_memory_alloc (vdppool->device, info)))
    goto mem_create_failed;

  gst_buffer_append_memory (buf, vdp_mem);

  if (vdppool->add_videometa) {
    GstVideoMeta *vmeta;

    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
    /* these are just the defaults for now */
    vmeta = gst_buffer_add_video_meta (buf, 0, GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
    vmeta->map = gst_vdp_video_memory_map;
    vmeta->unmap = gst_vdp_video_memory_unmap;
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
    GST_WARNING_OBJECT (pool, "Could create GstVdpVideo Memory");
    return GST_FLOW_ERROR;
  }
}


GstBufferPool *
gst_vdp_video_buffer_pool_new (GstVdpDevice * device)
{
  GstVdpVideoBufferPool *pool;

  pool = g_object_new (GST_TYPE_VDP_VIDEO_BUFFER_POOL, NULL);
  g_object_ref_sink (pool);
  pool->device = gst_object_ref (device);

  GST_LOG_OBJECT (pool, "new VdpVideo buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_vdp_video_buffer_pool_class_init (GstVdpVideoBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_vdp_video_buffer_pool_finalize;

  gstbufferpool_class->get_options = gst_vdp_video_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_vdp_video_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_vdp_video_buffer_pool_alloc;
}

static void
gst_vdp_video_buffer_pool_init (GstVdpVideoBufferPool * pool)
{

}

static void
gst_vdp_video_buffer_pool_finalize (GObject * object)
{
  GstVdpVideoBufferPool *pool = GST_VDP_VIDEO_BUFFER_POOL_CAST (object);

  GST_LOG_OBJECT (pool, "finalize VdpVideo buffer pool %p", pool);

  gst_object_unref (pool->device);

  G_OBJECT_CLASS (gst_vdp_video_buffer_pool_parent_class)->finalize (object);
}
