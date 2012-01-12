/*
 *  gstvaapidisplaycache.h - VA display cache
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

#ifndef GSTVAAPIDISPLAYCACHE_H
#define GSTVAAPIDISPLAYCACHE_H

#include <gst/vaapi/gstvaapidisplay.h>

typedef struct _GstVaapiDisplayCache    GstVaapiDisplayCache;

GstVaapiDisplayCache *
gst_vaapi_display_cache_new(void);

void
gst_vaapi_display_cache_free(GstVaapiDisplayCache *cache);

guint
gst_vaapi_display_cache_get_size(GstVaapiDisplayCache *cache);

gboolean
gst_vaapi_display_cache_add(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplayInfo        *info
);

void
gst_vaapi_display_cache_remove(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplay            *display
);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup(
    GstVaapiDisplayCache       *cache,
    GstVaapiDisplay            *display
);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_va_display(
    GstVaapiDisplayCache       *cache,
    VADisplay                   va_display
);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_native_display(
    GstVaapiDisplayCache       *cache,
    gpointer                    native_display
);

const GstVaapiDisplayInfo *
gst_vaapi_display_cache_lookup_by_name(
    GstVaapiDisplayCache       *cache,
    const gchar                *display_name,
    GCompareDataFunc            compare_func,
    gpointer                    user_data
);

#endif /* GSTVAAPIDISPLAYCACHE_H */
