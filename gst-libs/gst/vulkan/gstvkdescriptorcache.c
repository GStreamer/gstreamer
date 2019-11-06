/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#include "gstvkdescriptorcache.h"
#include "gstvkdescriptorcache-private.h"

/**
 * SECTION:vkdescriptorcache
 * @title: GstVulkanDescriptorCache
 * @short_description: Vulkan descriptor cache
 * @see_also: #GstVulkanDevice
 */

#define GET_PRIV(cache) gst_vulkan_descriptor_cache_get_instance_private (cache)

#define GST_CAT_DEFAULT gst_vulkan_descriptor_cache_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstVulkanDescriptorCachePrivate
{
  guint n_layouts;
  GstVulkanHandle **layouts;

  GQueue *available;
  gsize outstanding;
};

#define parent_class gst_vulkan_descriptor_cache_parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanDescriptorCache, gst_vulkan_descriptor_cache,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstVulkanDescriptorCache);
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "vulkancommandcache", 0, "Vulkan Command Cache"));

static void gst_vulkan_descriptor_cache_finalize (GObject * object);

static void
gst_vulkan_descriptor_cache_init (GstVulkanDescriptorCache * cache)
{
  GstVulkanDescriptorCachePrivate *priv = GET_PRIV (cache);

  priv->available = g_queue_new ();
}

static void
gst_vulkan_descriptor_cache_class_init (GstVulkanDescriptorCacheClass *
    device_class)
{
  GObjectClass *gobject_class = (GObjectClass *) device_class;

  gobject_class->finalize = gst_vulkan_descriptor_cache_finalize;
}

static void
do_free_set (GstVulkanHandle * handle)
{
  gst_vulkan_handle_unref (handle);
}

static void
gst_vulkan_descriptor_cache_finalize (GObject * object)
{
  GstVulkanDescriptorCache *cache = GST_VULKAN_DESCRIPTOR_CACHE (object);
  GstVulkanDescriptorCachePrivate *priv = GET_PRIV (cache);
  guint i;

  if (priv->outstanding > 0)
    g_critical
        ("Destroying a Vulkan descriptor cache that has outstanding descriptors!");

  for (i = 0; i < priv->n_layouts; i++)
    gst_vulkan_handle_unref (priv->layouts[i]);
  g_free (priv->layouts);

  g_queue_free_full (priv->available, (GDestroyNotify) do_free_set);
  priv->available = NULL;

  gst_clear_object (&cache->pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_vulkan_descriptor_cache_new:
 * @pool: a #GstVulkanDescriptorPool
 * @n_layouts: number of @layouts
 * @layouts: list of #GstVulkanHandle containing descriptor set layouts
 *
 * Returns: (transfer full): a new #GstVulkanDescriptorCache
 *
 * Since: 1.18
 */
GstVulkanDescriptorCache *
gst_vulkan_descriptor_cache_new (GstVulkanDescriptorPool * pool,
    guint n_layouts, GstVulkanHandle ** layouts)
{
  GstVulkanDescriptorCache *ret;
  GstVulkanDescriptorCachePrivate *priv;
  guint i;

  g_return_val_if_fail (GST_IS_VULKAN_DESCRIPTOR_POOL (pool), NULL);

  ret = g_object_new (GST_TYPE_VULKAN_DESCRIPTOR_CACHE, NULL);
  ret->pool = gst_object_ref (pool);

  priv = GET_PRIV (ret);
  priv->n_layouts = n_layouts;
  priv->layouts = g_new0 (GstVulkanHandle *, n_layouts);
  for (i = 0; i < n_layouts; i++)
    priv->layouts[i] = gst_vulkan_handle_ref (layouts[i]);

  gst_object_ref_sink (ret);

  return ret;
}

/**
 * gst_vulkan_descriptor_cache_acquire:
 * @cache: a #GstVulkanDescriptorCache
 * @error: a #GError
 *
 * Returns: a new #GstVulkanDescriptorSet
 *
 * Since: 1.18
 */
GstVulkanDescriptorSet *
gst_vulkan_descriptor_cache_acquire (GstVulkanDescriptorCache * cache,
    GError ** error)
{
  GstVulkanDescriptorSet *set = NULL;
  GstVulkanDescriptorCachePrivate *priv;

  g_return_val_if_fail (GST_IS_VULKAN_DESCRIPTOR_CACHE (cache), NULL);

  priv = GET_PRIV (cache);

  GST_OBJECT_LOCK (cache);
  set = g_queue_pop_head (priv->available);
  GST_OBJECT_UNLOCK (cache);

  if (!set)
    set = gst_vulkan_descriptor_pool_create (cache->pool, priv->n_layouts,
        priv->layouts, error);
  if (!set)
    return NULL;

  GST_OBJECT_LOCK (cache);
  priv->outstanding++;
  GST_OBJECT_UNLOCK (cache);

  set->cache = gst_object_ref (cache);
  return set;
}

void
gst_vulkan_descriptor_cache_release_set (GstVulkanDescriptorCache * cache,
    GstVulkanDescriptorSet * set)
{
  GstVulkanDescriptorCachePrivate *priv;

  g_return_if_fail (GST_IS_VULKAN_DESCRIPTOR_CACHE (cache));
  g_return_if_fail (set != NULL);

  priv = GET_PRIV (cache);

  GST_OBJECT_LOCK (cache);
  g_queue_push_tail (priv->available, set);
  priv->outstanding--;
  GST_OBJECT_UNLOCK (cache);

  /* decrease the refcount that the set had to us */
  gst_clear_object (&set->cache);
}
