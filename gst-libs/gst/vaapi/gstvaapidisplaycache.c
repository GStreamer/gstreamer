/*
 *  gstvaapidisplaycache.c - VA display cache
 *
 *  Copyright (C) 2012 Intel Corporation
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
struct _CacheEntry {
    GstVaapiDisplayInfo info;
};

struct _GstVaapiDisplayCache {
    GStaticMutex        mutex;
    GList              *list;
};

static void
cache_entry_free(CacheEntry *entry)
{
    GstVaapiDisplayInfo *info;

    if (!entry)
        return;

    info = &entry->info;

    if (info->display_name) {
        g_free(info->display_name);
        info->display_name = NULL;
    }
    g_slice_free(CacheEntry, entry);
}

static CacheEntry *
cache_entry_new(const GstVaapiDisplayInfo *di)
{
    GstVaapiDisplayInfo *info;
    CacheEntry *entry;

    entry = g_slice_new(CacheEntry);
    if (!entry)
        return NULL;

    info                 = &entry->info;
    info->display        = di->display;
    info->va_display     = di->va_display;
    info->native_display = di->native_display;
    info->display_type   = di->display_type;
    info->display_name   = NULL;

    if (di->display_name) {
        info->display_name = g_strdup(di->display_name);
        if (!info->display_name)
            goto error;
    }
    return entry;

error:
    cache_entry_free(entry);
    return NULL;
}

#define CACHE_LOOKUP(cache, res, prop, comp_func, comp_data, user_data) do { \
        GList *l;                                                       \
                                                                        \
        g_static_mutex_lock(&(cache)->mutex);                           \
        for (l = (cache)->list; l != NULL; l = l->next) {               \
            GstVaapiDisplayInfo * const info =                          \
                &((CacheEntry *)l->data)->info;                         \
            if (comp_func(info->prop, comp_data, user_data))            \
                break;                                                  \
        }                                                               \
        g_static_mutex_unlock(&(cache)->mutex);                         \
        res = l;                                                        \
    } while (0)

#define compare_equal(a, b, user_data) \
    ((a) == (b))

#define compare_string(a, b, user_data) \
    ((a) == (b) || ((a) && (b) && strcmp(a, b) == 0))

static GList *
cache_lookup_display(GstVaapiDisplayCache *cache, GstVaapiDisplay *display)
{
    GList *m;

    CACHE_LOOKUP(cache, m, display, compare_equal, display, NULL);
    return m;
}

static GList *
cache_lookup_va_display(GstVaapiDisplayCache *cache, VADisplay va_display)
{
    GList *m;

    CACHE_LOOKUP(cache, m, va_display, compare_equal, va_display, NULL);
    return m;
}

static GList *
cache_lookup_native_display(GstVaapiDisplayCache *cache, gpointer native_display)
{
    GList *m;

    CACHE_LOOKUP(cache, m, native_display, compare_equal, native_display, NULL);
    return m;
}

/**
 * gst_vaapi_display_cache_new:
 *
 * Creates a new VA display cache.
 *
 * Return value: the newly created #GstVaapiDisplayCache object
 */
GstVaapiDisplayCache *
gst_vaapi_display_cache_new(void)
{
    GstVaapiDisplayCache *cache;

    cache = g_slice_new0(GstVaapiDisplayCache);
    if (!cache)
        return NULL;

    g_static_mutex_init(&cache->mutex);
    return cache;
}

/**
 * gst_vaapi_display_cache_new:
 * @cache: the #GstVaapiDisplayCache to destroy
 *
 * Destroys a VA display cache.
 */
void
gst_vaapi_display_cache_free(GstVaapiDisplayCache *cache)
{
    GList *l;

    if (!cache)
        return;

    if (cache->list) {
        for (l = cache->list; l != NULL; l = l->next)
            cache_entry_free(l->data);
        g_list_free(cache->list);
        cache->list = NULL;
    }
    g_static_mutex_free(&cache->mutex);
    g_slice_free(GstVaapiDisplayCache, cache);
}

/**
 * gst_vaapi_display_cache_get_size:
 * @cache: the #GstVaapiDisplayCache
 *
 * Gets the size of the display cache @cache.
 *
 * Return value: the size of the display cache
 */
guint
gst_vaapi_display_cache_get_size(GstVaapiDisplayCache *cache)
{
    guint size;

    g_return_val_if_fail(cache != NULL, 0);

    g_static_mutex_lock(&cache->mutex);
    size = g_list_length(cache->list);
    g_static_mutex_unlock(&cache->mutex);
    return size;
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
gst_vaapi_display_cache_add(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplayInfo        *info
)
{
    CacheEntry *entry;

    g_return_val_if_fail(cache != NULL, FALSE);
    g_return_val_if_fail(info != NULL, FALSE);

    entry = cache_entry_new(info);
    if (!entry)
        return FALSE;

    g_static_mutex_lock(&cache->mutex);
    cache->list = g_list_prepend(cache->list, entry);
    g_static_mutex_unlock(&cache->mutex);
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
gst_vaapi_display_cache_remove(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplay            *display
)
{
    GList *m;

    m = cache_lookup_display(cache, display);
    if (!m)
        return;

    cache_entry_free(m->data);
    g_static_mutex_lock(&cache->mutex);
    cache->list = g_list_delete_link(cache->list, m);
    g_static_mutex_unlock(&cache->mutex);
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
gst_vaapi_display_cache_lookup(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplay            *display
)
{
    CacheEntry *entry;
    GList *m;

    g_return_val_if_fail(cache != NULL, NULL);
    g_return_val_if_fail(display != NULL, NULL);

    m = cache_lookup_display(cache, display);
    if (!m)
        return NULL;

    entry = m->data;
    return &entry->info;
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
gst_vaapi_display_cache_lookup_by_va_display(
    GstVaapiDisplayCache       *cache,
    VADisplay                   va_display
)
{
    CacheEntry *entry;
    GList *m;

    g_return_val_if_fail(cache != NULL, NULL);
    g_return_val_if_fail(va_display != NULL, NULL);

    m = cache_lookup_va_display(cache, va_display);
    if (!m)
        return NULL;

    entry = m->data;
    return &entry->info;
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
gst_vaapi_display_cache_lookup_by_native_display(
    GstVaapiDisplayCache       *cache,
    gpointer                    native_display
)
{
    CacheEntry *entry;
    GList *m;

    g_return_val_if_fail(cache != NULL, NULL);
    g_return_val_if_fail(native_display != NULL, NULL);

    m = cache_lookup_native_display(cache, native_display);
    if (!m)
        return NULL;

    entry = m->data;
    return &entry->info;
}

/**
 * gst_vaapi_display_cache_lookup_by_name:
 * @cache: the #GstVaapiDisplayCache
 * @display_name: the display name to match
 * @compare_func: an optional string comparison function
 * @user_data: any relevant data pointer to the comparison function
 *
 * Looks up the display cache for the specified display name. A
 * specific comparison function can be provided to avoid a plain
 * strcmp().
 *
 * Return value: a #GstVaapiDisplayInfo matching @display_name, or
 *   %NULL if none was found
 */
const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_name(
    GstVaapiDisplayCache       *cache,
    const gchar                *display_name,
    GCompareDataFunc            compare_func,
    gpointer                    user_data
)
{
    CacheEntry *entry;
    GList *m;

    g_return_val_if_fail(cache != NULL, NULL);

    if (compare_func)
        CACHE_LOOKUP(cache, m, display_name, compare_func, display_name, user_data);
    else
        CACHE_LOOKUP(cache, m, display_name, compare_string, display_name, NULL);
    if (!m)
        return NULL;

    entry = m->data;
    return &entry->info;
}
