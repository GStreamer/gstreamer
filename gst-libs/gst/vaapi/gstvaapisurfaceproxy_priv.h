/*
 *  gstvaapisurfaceproxy_priv.h - VA surface proxy (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_SURFACE_PROXY_PRIV_H
#define GST_VAAPI_SURFACE_PROXY_PRIV_H

#include "gstvaapiminiobject.h"
#include "gstvaapisurfaceproxy.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapisurface_priv.h"

#define GST_VAAPI_SURFACE_PROXY(obj) \
    ((GstVaapiSurfaceProxy *)(obj))

#define GST_VAAPI_IS_SURFACE_PROXY(obj) \
    (GST_VAAPI_SURFACE_PROXY(obj) != NULL)

struct _GstVaapiSurfaceProxy {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    GstVaapiVideoPool  *pool;
    GstVaapiSurface    *surface;
    GstClockTime        timestamp;
    GstClockTime        duration;
    GDestroyNotify      destroy_func;
    gpointer            destroy_data;
};

#define GST_VAAPI_SURFACE_PROXY_FLAGS       GST_VAAPI_MINI_OBJECT_FLAGS
#define GST_VAAPI_SURFACE_PROXY_FLAG_IS_SET GST_VAAPI_MINI_OBJECT_FLAG_IS_SET
#define GST_VAAPI_SURFACE_PROXY_FLAG_SET    GST_VAAPI_MINI_OBJECT_FLAG_SET
#define GST_VAAPI_SURFACE_PROXY_FLAG_UNSET  GST_VAAPI_MINI_OBJECT_FLAG_UNSET

/**
 * GST_VAAPI_SURFACE_PROXY_SURFACE:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the #GstVaapiSurface of @proxy.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_PROXY_SURFACE
#define GST_VAAPI_SURFACE_PROXY_SURFACE(proxy) \
    proxy->surface

/**
 * GST_VAAPI_SURFACE_PROXY_SURFACE_ID:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the VA surface ID of the underlying @proxy
 * surface.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_PROXY_SURFACE_ID
#define GST_VAAPI_SURFACE_PROXY_SURFACE_ID(proxy) \
    GST_VAAPI_OBJECT_ID(proxy->surface)

/**
 * GST_VAAPI_SURFACE_PROXY_TIMESTAMP:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the presentation timestamp of the
 * underlying @proxy surface.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_PROXY_TIMESTAMP
#define GST_VAAPI_SURFACE_PROXY_TIMESTAMP(proxy) \
    proxy->timestamp

/**
 * GST_VAAPI_SURFACE_PROXY_DURATION:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Macro that evaluates to the presentation duration of the
 * underlying @proxy surface.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_PROXY_DURATION
#define GST_VAAPI_SURFACE_PROXY_DURATION(proxy) \
    proxy->duration

#endif /* GST_VAAPI_SURFACE_PROXY_PRIV_H */
