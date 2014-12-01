/*
 *  gstvaapidisplaycache.h - VA display cache
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

#ifndef GSTVAAPIDISPLAYCACHE_H
#define GSTVAAPIDISPLAYCACHE_H

#include "libgstvaapi_priv_check.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include "gstvaapiminiobject.h"

typedef struct _GstVaapiDisplayCache            GstVaapiDisplayCache;

G_GNUC_INTERNAL
GstVaapiDisplayCache *
gst_vaapi_display_cache_new (void);

#define gst_vaapi_display_cache_ref(cache) \
    ((GstVaapiDisplayCache *) gst_vaapi_mini_object_ref ( \
        GST_VAAPI_MINI_OBJECT (cache)))
#define gst_vaapi_display_cache_unref(cache) \
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (cache))
#define gst_vaapi_display_cache_replace(old_cache_ptr, new_cache) \
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) (old_cache_ptr), \
        GST_VAAPI_MINI_OBJECT (new_cache))

G_GNUC_INTERNAL
void
gst_vaapi_display_cache_lock (GstVaapiDisplayCache * cache);

G_GNUC_INTERNAL
void
gst_vaapi_display_cache_unlock (GstVaapiDisplayCache * cache);

G_GNUC_INTERNAL
gboolean
gst_vaapi_display_cache_is_empty (GstVaapiDisplayCache * cache);

G_GNUC_INTERNAL
gboolean
gst_vaapi_display_cache_add (GstVaapiDisplayCache * cache,
    GstVaapiDisplayInfo * info);

G_GNUC_INTERNAL
void
gst_vaapi_display_cache_remove (GstVaapiDisplayCache * cache,
    GstVaapiDisplay * display);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup (GstVaapiDisplayCache
    * cache, GstVaapiDisplay * display);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_custom (GstVaapiDisplayCache * cache,
    GCompareFunc func, gconstpointer data, guint display_types);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_va_display (GstVaapiDisplayCache * cache,
    VADisplay va_display);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_native_display (GstVaapiDisplayCache *
    cache, gpointer native_display, guint display_types);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_name (GstVaapiDisplayCache * cache,
    const gchar * display_name, guint display_types);

#endif /* GSTVAAPIDISPLAYCACHE_H */
