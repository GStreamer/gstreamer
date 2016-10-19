/*
 *  gstvaapidisplaycache.c - VA display cache
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
#include <glib.h>
#include <string.h>
#include "gstvaapidisplaycache.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _CacheEntry CacheEntry;
struct _CacheEntry
{
  GstVaapiDisplayInfo info;
};

struct _GstVaapiDisplayCache
{
  GstVaapiMiniObject parent_instance;
  GRecMutex mutex;
  GList *list;
};

static void
cache_entry_free (CacheEntry * entry)
{
  GstVaapiDisplayInfo *info;

  if (!entry)
    return;

  info = &entry->info;

  if (info->display_name) {
    g_free (info->display_name);
    info->display_name = NULL;
  }
  g_slice_free (CacheEntry, entry);
}

static CacheEntry *
cache_entry_new (const GstVaapiDisplayInfo * di)
{
  GstVaapiDisplayInfo *info;
  CacheEntry *entry;

  entry = g_slice_new (CacheEntry);
  if (!entry)
    return NULL;

  info = &entry->info;
  info->display = di->display;
  info->va_display = di->va_display;
  info->native_display = di->native_display;
  info->display_type = di->display_type;
  info->display_name = NULL;

  if (di->display_name) {
    info->display_name = g_strdup (di->display_name);
    if (!info->display_name)
      goto error;
  }
  return entry;

  /* ERRORS */
error:
  {
    cache_entry_free (entry);
    return NULL;
  }
}

static inline gboolean
is_compatible_display_type (const GstVaapiDisplayType display_type,
    guint display_types)
{
  if (display_type == GST_VAAPI_DISPLAY_TYPE_ANY)
    return TRUE;
  if (display_types == GST_VAAPI_DISPLAY_TYPE_ANY)
    return TRUE;
  return ((1U << display_type) & display_types) != 0;
}

static GList *
cache_lookup_1 (GstVaapiDisplayCache * cache, GCompareFunc func,
    gconstpointer data, guint display_types)
{
  GList *l;

  for (l = cache->list; l != NULL; l = l->next) {
    GstVaapiDisplayInfo *const info = &((CacheEntry *) l->data)->info;
    if (!is_compatible_display_type (info->display_type, display_types))
      continue;
    if (func (info, data))
      break;
  }
  return l;
}

static inline const GstVaapiDisplayInfo *
cache_lookup (GstVaapiDisplayCache * cache, GCompareFunc func,
    gconstpointer data, guint display_types)
{
  GList *const m = cache_lookup_1 (cache, func, data, display_types);

  return m ? &((CacheEntry *) m->data)->info : NULL;
}

static gint
compare_display (gconstpointer a, gconstpointer display)
{
  const GstVaapiDisplayInfo *const info = a;

  return info->display == display;
}

static gint
compare_va_display (gconstpointer a, gconstpointer va_display)
{
  const GstVaapiDisplayInfo *const info = a;

  return info->va_display == va_display;
}

static gint
compare_native_display (gconstpointer a, gconstpointer native_display)
{
  const GstVaapiDisplayInfo *const info = a;

  return info->native_display == native_display;
}

static gint
compare_display_name (gconstpointer a, gconstpointer b)
{
  const GstVaapiDisplayInfo *const info = a;
  const gchar *const display_name = b;

  if (info->display_name == NULL && display_name == NULL)
    return TRUE;
  if (!info->display_name || !display_name)
    return FALSE;
  return strcmp (info->display_name, display_name) == 0;
}

static void
gst_vaapi_display_cache_finalize (GstVaapiDisplayCache * cache)
{
  GList *l;

  if (cache->list) {
    for (l = cache->list; l != NULL; l = l->next)
      cache_entry_free (l->data);
    g_list_free (cache->list);
    cache->list = NULL;
  }
  g_rec_mutex_clear (&cache->mutex);
}

static const GstVaapiMiniObjectClass *
gst_vaapi_display_cache_class (void)
{
  static const GstVaapiMiniObjectClass GstVaapiDisplayCacheClass = {
    .size = sizeof (GstVaapiDisplayCache),
    .finalize = (GDestroyNotify) gst_vaapi_display_cache_finalize
  };
  return &GstVaapiDisplayCacheClass;
}

/**
 * gst_vaapi_display_cache_new:
 *
 * Creates a new VA display cache.
 *
 * Return value: the newly created #GstVaapiDisplayCache object
 */
GstVaapiDisplayCache *
gst_vaapi_display_cache_new (void)
{
  GstVaapiDisplayCache *cache;

  cache = (GstVaapiDisplayCache *)
      gst_vaapi_mini_object_new (gst_vaapi_display_cache_class ());
  if (!cache)
    return NULL;

  g_rec_mutex_init (&cache->mutex);
  cache->list = NULL;
  return cache;
}

/**
 * gst_vaapi_display_cache_lock:
 * @cache: the #GstVaapiDisplayCache
 *
 * Locks the display cache @cache.
 */
void
gst_vaapi_display_cache_lock (GstVaapiDisplayCache * cache)
{
  g_return_if_fail (cache != NULL);

  g_rec_mutex_lock (&cache->mutex);
}

/**
 * gst_vaapi_display_cache_unlock:
 * @cache: the #GstVaapiDisplayCache
 *
 * Unlocks the display cache @cache.
 */
void
gst_vaapi_display_cache_unlock (GstVaapiDisplayCache * cache)
{
  g_return_if_fail (cache != NULL);

  g_rec_mutex_unlock (&cache->mutex);
}

/**
 * gst_vaapi_display_cache_is_empty:
 * @cache: the #GstVaapiDisplayCache
 *
 * Checks whether the display cache @cache is empty.
 *
 * Return value: %TRUE if the display @cache is empty, %FALSE otherwise.
 */
gboolean
gst_vaapi_display_cache_is_empty (GstVaapiDisplayCache * cache)
{
  g_return_val_if_fail (cache != NULL, 0);

  return cache->list == NULL;
}

/**
 * gst_vaapi_display_cache_add:
 * @cache: the #GstVaapiDisplayCache
 * @info: the display cache info to add
 *
 * Adds a new entry with data from @info. The display @info data is
 * copied into the newly created cache entry.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_display_cache_add (GstVaapiDisplayCache * cache,
    GstVaapiDisplayInfo * info)
{
  CacheEntry *entry;

  g_return_val_if_fail (cache != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  entry = cache_entry_new (info);
  if (!entry)
    return FALSE;

  cache->list = g_list_prepend (cache->list, entry);
  return TRUE;
}

/**
 * gst_vaapi_display_cache_remove:
 * @cache: the #GstVaapiDisplayCache
 * @display: the display to remove from cache
 *
 * Removes any cache entry that matches the specified #GstVaapiDisplay.
 */
void
gst_vaapi_display_cache_remove (GstVaapiDisplayCache * cache,
    GstVaapiDisplay * display)
{
  GList *m;

  m = cache_lookup_1 (cache, compare_display, display,
      GST_VAAPI_DISPLAY_TYPE_ANY);
  if (!m)
    return;

  cache_entry_free (m->data);
  cache->list = g_list_delete_link (cache->list, m);
}

/**
 * gst_vaapi_display_cache_lookup:
 * @cache: the #GstVaapiDisplayCache
 * @display: the display to find
 *
 * Looks up the display cache for the specified #GstVaapiDisplay.
 *
 * Return value: a #GstVaapiDisplayInfo matching @display, or %NULL if
 *   none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup (GstVaapiDisplayCache * cache,
    GstVaapiDisplay * display)
{
  g_return_val_if_fail (cache != NULL, NULL);
  g_return_val_if_fail (display != NULL, NULL);

  return cache_lookup (cache, compare_display, display,
      GST_VAAPI_DISPLAY_TYPE_ANY);
}

/**
 * gst_vaapi_display_cache_lookup_custom:
 * @cache: the #GstVaapiDisplayCache
 * @func: an comparison function
 * @data: user data passed to the function
 *
 * Looks up an element in the display @cache using the supplied
 * function @func to find the desired element. It iterates over all
 * elements in cache, calling the given function which should return
 * %TRUE when the desired element is found.
 *
 * The comparison function takes two gconstpointer arguments, a
 * #GstVaapiDisplayInfo as the first argument, and that is used to
 * compare against the given user @data argument as the second
 * argument.
 *
 * Return value: a #GstVaapiDisplayInfo causing @func to succeed
 *   (i.e. returning %TRUE), or %NULL if none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_custom (GstVaapiDisplayCache * cache,
    GCompareFunc func, gconstpointer data, guint display_types)
{
  g_return_val_if_fail (cache != NULL, NULL);
  g_return_val_if_fail (func != NULL, NULL);

  return cache_lookup (cache, func, data, display_types);
}

/**
 * gst_vaapi_display_cache_lookup_by_va_display:
 * @cache: the #GstVaapiDisplayCache
 * @va_display: the VA display to find
 *
 * Looks up the display cache for the specified VA display.
 *
 * Return value: a #GstVaapiDisplayInfo matching @va_display, or %NULL
 *   if none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_va_display (GstVaapiDisplayCache * cache,
    VADisplay va_display)
{
  g_return_val_if_fail (cache != NULL, NULL);
  g_return_val_if_fail (va_display != NULL, NULL);

  return cache_lookup (cache, compare_va_display, va_display,
      GST_VAAPI_DISPLAY_TYPE_ANY);
}

/**
 * gst_vaapi_display_cache_lookup_by_native_display:
 * @cache: the #GstVaapiDisplayCache
 * @native_display: the native display to find
 *
 * Looks up the display cache for the specified native display.
 *
 * Return value: a #GstVaapiDisplayInfo matching @native_display, or
 *   %NULL if none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_native_display (GstVaapiDisplayCache * cache,
    gpointer native_display, guint display_types)
{
  g_return_val_if_fail (cache != NULL, NULL);
  g_return_val_if_fail (native_display != NULL, NULL);

  return cache_lookup (cache, compare_native_display, native_display,
      display_types);
}

/**
 * gst_vaapi_display_cache_lookup_by_name:
 * @cache: the #GstVaapiDisplayCache
 * @display_name: the display name to match
 *
 * Looks up the display cache for the specified display name.
 *
 * Return value: a #GstVaapiDisplayInfo matching @display_name, or
 *   %NULL if none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_name (GstVaapiDisplayCache * cache,
    const gchar * display_name, guint display_types)
{
  g_return_val_if_fail (cache != NULL, NULL);

  return cache_lookup (cache, compare_display_name, display_name,
      display_types);
}
