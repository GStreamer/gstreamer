/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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

#include "gstvkimagebufferpool.h"

/**
 * SECTION:vkimagebufferpool
 * @title: GstVulkanImageBufferPool
 * @short_description: buffer pool for #GstVulkanImageMemory objects
 * @see_also: #GstBufferPool, #GstVulkanImageMemory
 *
 * a #GstVulkanImageBufferPool is an object that allocates buffers with #GstVulkanImageMemory
 *
 * A #GstVulkanImageBufferPool is created with gst_vulkan_image_buffer_pool_new()
 */

/* bufferpool */
struct _GstVulkanImageBufferPoolPrivate
{
  GstCaps *caps;
  gboolean raw_caps;
  GstVideoInfo v_info;
};

static void gst_vulkan_image_buffer_pool_finalize (GObject * object);

GST_DEBUG_CATEGORY_STATIC (GST_CAT_VULKAN_IMAGE_BUFFER_POOL);
#define GST_CAT_DEFAULT GST_CAT_VULKAN_IMAGE_BUFFER_POOL

#define GET_PRIV(pool) gst_vulkan_image_buffer_pool_get_instance_private (pool)

#define gst_vulkan_image_buffer_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanImageBufferPool, gst_vulkan_image_buffer_pool,
    GST_TYPE_BUFFER_POOL, G_ADD_PRIVATE (GstVulkanImageBufferPool)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_VULKAN_IMAGE_BUFFER_POOL,
        "vulkanimagebufferpool", 0, "Vulkan Image Buffer Pool"));

static gboolean
gst_vulkan_image_buffer_pool_set_config (GstBufferPool * pool,
    GstStructure * config)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  guint min_buffers, max_buffers;
  GstCaps *caps = NULL;
  GstCapsFeatures *features;
  gboolean ret = TRUE;
  guint i;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&priv->v_info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, priv->v_info.width,
      priv->v_info.height, caps);

  gst_caps_replace (&priv->caps, caps);

  features = gst_caps_get_features (caps, 0);
  priv->raw_caps = features == NULL || gst_caps_features_is_equal (features,
      GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY);

  /* get the size of the buffer to allocate */
  priv->v_info.size = 0;
  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->v_info); i++) {
    GstVulkanImageMemory *img_mem;
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    guint width, height;
    VkFormat vk_format;

    vk_format = gst_vulkan_format_from_video_info (&priv->v_info, i);
    width = GST_VIDEO_INFO_COMP_WIDTH (&priv->v_info, i);
    height = GST_VIDEO_INFO_COMP_HEIGHT (&priv->v_info, i);
    if (priv->raw_caps)
      tiling = VK_IMAGE_TILING_LINEAR;

    img_mem = (GstVulkanImageMemory *)
        gst_vulkan_image_memory_alloc (vk_pool->device, vk_format, width,
        height, tiling,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    priv->v_info.offset[i] = priv->v_info.size;
    priv->v_info.size += img_mem->requirements.size;

    gst_memory_unref (GST_MEMORY_CAST (img_mem));
  }

  gst_buffer_pool_config_set_params (config, caps,
      priv->v_info.size, min_buffers, max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config) && ret;

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
}

/* This function handles GstBuffer creation */
static GstFlowReturn
gst_vulkan_image_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstVulkanImageBufferPool *vk_pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (pool);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (vk_pool);
  GstBuffer *buf;
  guint i;

  if (!(buf = gst_buffer_new ())) {
    goto no_buffer;
  }

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (&priv->v_info); i++) {
    VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkFormat vk_format;
    GstMemory *mem;

    vk_format = gst_vulkan_format_from_video_info (&priv->v_info, i);
    if (priv->raw_caps)
      tiling = VK_IMAGE_TILING_LINEAR;

    mem = gst_vulkan_image_memory_alloc (vk_pool->device,
        vk_format, GST_VIDEO_INFO_COMP_WIDTH (&priv->v_info, i),
        GST_VIDEO_INFO_COMP_HEIGHT (&priv->v_info, i), tiling,
        /* FIXME: choose from outside */
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        /* FIXME: choose from outside */
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!mem) {
      gst_buffer_unref (buf);
      goto mem_create_failed;
    }

    gst_buffer_append_memory (buf, mem);
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
    GST_WARNING_OBJECT (pool, "Could not create Vulkan Memory");
    return GST_FLOW_ERROR;
  }
}

/**
 * gst_vulkan_image_buffer_pool_new:
 * @device: the #GstVulkanDevice to use
 *
 * Returns: (transfer full): a #GstBufferPool that allocates buffers with #GstGLMemory
 *
 * Since: 1.18
 */
GstBufferPool *
gst_vulkan_image_buffer_pool_new (GstVulkanDevice * device)
{
  GstVulkanImageBufferPool *pool;

  pool = g_object_new (GST_TYPE_VULKAN_IMAGE_BUFFER_POOL, NULL);
  g_object_ref_sink (pool);
  pool->device = gst_object_ref (device);

  GST_LOG_OBJECT (pool, "new Vulkan buffer pool for device %" GST_PTR_FORMAT,
      device);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_vulkan_image_buffer_pool_class_init (GstVulkanImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_vulkan_image_buffer_pool_finalize;

  gstbufferpool_class->set_config = gst_vulkan_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_vulkan_image_buffer_pool_alloc;
}

static void
gst_vulkan_image_buffer_pool_init (GstVulkanImageBufferPool * pool)
{
}

static void
gst_vulkan_image_buffer_pool_finalize (GObject * object)
{
  GstVulkanImageBufferPool *pool = GST_VULKAN_IMAGE_BUFFER_POOL_CAST (object);
  GstVulkanImageBufferPoolPrivate *priv = GET_PRIV (pool);

  GST_LOG_OBJECT (pool, "finalize Vulkan buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);

  G_OBJECT_CLASS (gst_vulkan_image_buffer_pool_parent_class)->finalize (object);

  /* only release the context once all our memory have been deleted */
  if (pool->device) {
    gst_object_unref (pool->device);
    pool->device = NULL;
  }
}
