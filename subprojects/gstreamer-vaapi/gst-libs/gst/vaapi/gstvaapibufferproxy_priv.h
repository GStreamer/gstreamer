/*
 *  gstvaapibufferproxy_priv.h - Buffer proxy abstraction (private definitions)
 *
 *  Copyright (C) 2014 Intel Corporation
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

#ifndef GST_VAAPI_BUFFER_PROXY_PRIV_H
#define GST_VAAPI_BUFFER_PROXY_PRIV_H

#include "gstvaapibufferproxy.h"
#include "gstvaapiminiobject.h"

G_BEGIN_DECLS

/**
 * GST_VAAPI_BUFFER_PROXY_TYPE:
 * @buf: a #GstVaapiBufferProxy
 *
 * Macro that evaluates to the type of the underlying VA buffer @buf
 */
#undef  GST_VAAPI_BUFFER_PROXY_TYPE
#define GST_VAAPI_BUFFER_PROXY_TYPE(buf) \
  (GST_VAAPI_BUFFER_PROXY (buf)->type)

/**
 * GST_VAAPI_BUFFER_PROXY_HANDLE:
 * @buf: a #GstVaapiBufferProxy
 *
 * Macro that evaluates to the handle of the underlying VA buffer @buf
 */
#undef  GST_VAAPI_BUFFER_PROXY_HANDLE
#define GST_VAAPI_BUFFER_PROXY_HANDLE(buf) \
  (GST_VAAPI_BUFFER_PROXY (buf)->va_info.handle)

/**
 * GST_VAAPI_BUFFER_PROXY_SIZE:
 * @buf: a #GstVaapiBufferProxy
 *
 * Macro that evaluates to the size of the underlying VA buffer @buf
 */
#undef  GST_VAAPI_BUFFER_PROXY_SIZE
#define GST_VAAPI_BUFFER_PROXY_SIZE(buf) \
  (GST_VAAPI_BUFFER_PROXY (buf)->va_info.mem_size)

struct _GstVaapiBufferProxy {
  /*< private >*/
  GstVaapiMiniObject    parent_instance;
  GstMiniObject        *surface;

  GDestroyNotify        destroy_func;
  gpointer              destroy_data;
  guint                 type;
  VABufferID            va_buf;
  VABufferInfo          va_info;
};

G_GNUC_INTERNAL
GstVaapiBufferProxy *
gst_vaapi_buffer_proxy_new_from_surface (GstMiniObject * surface,
    VABufferID buf_id, guint type, GDestroyNotify destroy_func, gpointer data);

G_GNUC_INTERNAL
guint
from_GstVaapiBufferMemoryType (guint type);

G_GNUC_INTERNAL
guint
to_GstVaapiBufferMemoryType (guint va_type);

G_END_DECLS

#endif /* GST_VAAPI_BUFFER_PROXY_PRIV_H */
