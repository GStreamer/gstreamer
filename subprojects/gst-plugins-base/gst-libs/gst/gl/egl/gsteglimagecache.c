/*
 * GStreamer
 * Copyright (C) 2024 Pengutronix, Philipp Zabel <graphics@pengutronix.de>
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

#include "gsteglimage.h"
#include "gsteglimage_private.h"
#include "gsteglimagecache.h"

struct _GstEGLImageCacheEntry
{
  GstEGLImage *eglimage[GST_VIDEO_MAX_PLANES];
};

struct _GstEGLImageCache
{
  gint ref_count;
  GHashTable *hash_table;       /* for GstMemory -> GstEGLImageCacheEntry lookup */
  GMutex lock;                  /* protects hash_table */
};

/**
 * gst_egl_image_cache_ref:
 * @cache: a #GstEGLImageCache.
 *
 * Increases the refcount of the given image cache by one.
 *
 * Returns: (transfer full): @cache
 *
 * Since: 1.26
 */
GstEGLImageCache *
gst_egl_image_cache_ref (GstEGLImageCache * cache)
{
  g_atomic_int_inc (&cache->ref_count);
  return cache;
}

/**
 * gst_egl_image_cache_unref:
 * @cache: (transfer full): a #GstEGLImageCache.
 *
 * Decreases the refcount of the image cache. If the refcount reaches 0, the
 * image cache will be freed and all cached images will be unreffed.
 *
 * Since: 1.26
 */
void
gst_egl_image_cache_unref (GstEGLImageCache * cache)
{
  if (g_atomic_int_dec_and_test (&cache->ref_count)) {
    g_hash_table_unref (cache->hash_table);
    g_mutex_clear (&cache->lock);
    g_free (cache);
  }
}

static void
gst_egl_image_cache_entry_remove (GstEGLImageCache * cache, GstMiniObject * mem)
{
  g_mutex_lock (&cache->lock);
  g_hash_table_remove (cache->hash_table, mem);
  g_mutex_unlock (&cache->lock);
  gst_egl_image_cache_unref (cache);
}

static GstEGLImageCacheEntry *
gst_egl_image_cache_entry_add (GstEGLImageCache * cache, GstMemory * mem)
{
  GstEGLImageCacheEntry *cache_entry;

  cache_entry = g_new0 (GstEGLImageCacheEntry, 1);
  gst_egl_image_cache_ref (cache);
  gst_mini_object_weak_ref (GST_MINI_OBJECT (mem),
      (GstMiniObjectNotify) gst_egl_image_cache_entry_remove, cache);
  g_mutex_lock (&cache->lock);
  g_hash_table_insert (cache->hash_table, mem, cache_entry);
  g_mutex_unlock (&cache->lock);

  return cache_entry;
}

/*
 * Called with the cache lock taken.
 */
static void
gst_egl_image_cache_entry_free (GstEGLImageCacheEntry * cache_entry)
{
  gint i;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (cache_entry->eglimage[i])
      gst_egl_image_unref (cache_entry->eglimage[i]);
  }
  g_free (cache_entry);
}

/**
 * gst_egl_image_cache_lookup:
 * @cache: a #GstEGLImageCache.
 * @mem: a #GstMemory to look a cached #GetEGLImage up for
 * @plane: the plane to select which cached #GstEGLImage to look up for @mem
 * @previous_mem:
 * @cache_entry:
 *
 * Looks up a @cache_entry for @mem if mem is different from @previous_mem.
 * If @mem is the same as @previous_mem, the costly lookup is skipped and the
 * provided (previous) @cache_entry is used instead. In this case, @cache_entry
 * must have been returned by a previous call of @gst_egl_image_cache_lookup
 * with the same @mem.
 *
 * Returns: (nullable): a cached #GstEGLImage for @mem and @plane or %NULL.
 * @previous_mem is set to @mem.
 *
 * Since: 1.26
 */
GstEGLImage *
gst_egl_image_cache_lookup (GstEGLImageCache * cache, GstMemory * mem,
    gint plane, GstMemory ** previous_mem, GstEGLImageCacheEntry ** cache_entry)
{
  if (mem != *previous_mem) {
    g_mutex_lock (&cache->lock);
    *cache_entry = g_hash_table_lookup (cache->hash_table, mem);
    g_mutex_unlock (&cache->lock);
    *previous_mem = mem;
  }

  if (*cache_entry)
    return (*cache_entry)->eglimage[plane];

  return NULL;
}

/**
 * gst_egl_image_cache_store:
 * @cache: a #GstEGLImageCache.
 * @mem: a #GstMemory to store @eglimage for
 * @plane: the plane slot to store @eglimage in
 * @eglimage: a #GstEGLImage to be cached for @mem and @plane
 * @cache_entry: a #GstEGLImageCacheEntry looked up for @mem, or %NULL
 *
 * Creates a new cache_entry for mem if no cache_entry is provided.
 * Stores the eglimage for the given plane in the cache_entry. If an
 * existing cache entry is provided, it must be returned by a
 * @gst_egl_image_cache_lookup call with the same @mem.
 *
 * Since: 1.26
 */
void
gst_egl_image_cache_store (GstEGLImageCache * cache, GstMemory * mem,
    gint plane, GstEGLImage * eglimage, GstEGLImageCacheEntry ** cache_entry)
{
  if (!(*cache_entry))
    *cache_entry = gst_egl_image_cache_entry_add (cache, mem);
  (*cache_entry)->eglimage[plane] = eglimage;
}

/**
 * gst_egl_image_cache_new:
 *
 * Creates an EGL image cache that holds references to EGL images
 * until the cache is freed. Each cache entry can be looked up by
 * GstMemory and holds one or more EGL images derived from it.
 *
 * Returns: (nullable): an empty #GstEGLImageCache or %NULL on failure
 *
 * Since: 1.26
 */
GstEGLImageCache *
gst_egl_image_cache_new (void)
{
  GstEGLImageCache *cache;

  cache = g_new0 (GstEGLImageCache, 1);
  cache->ref_count = 1;

  cache->hash_table = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_egl_image_cache_entry_free);
  g_mutex_init (&cache->lock);

  return cache;
}
