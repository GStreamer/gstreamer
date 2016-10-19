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
#include "gstvaapiobject.h"
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
  GstVaapiObject       *parent;

  GDestroyNotify        destroy_func;
  gpointer              destroy_data;
  guint                 type;
  VABufferID            va_buf;
#if VA_CHECK_VERSION (0,36,0)
  VABufferInfo          va_info;
#endif
  GstMemory             *mem;
};

G_GNUC_INTERNAL
GstVaapiBufferProxy *
gst_vaapi_buffer_proxy_new_from_object (GstVaapiObject * object,
    VABufferID buf_id, guint type, GDestroyNotify destroy_func, gpointer data);

G_GNUC_INTERNAL
guint
from_GstVaapiBufferMemoryType (guint type);

G_GNUC_INTERNAL
guint
to_GstVaapiBufferMemoryType (guint va_type);

/* Inline reference counting for core libgstvaapi library */
#ifdef IN_LIBGSTVAAPI_CORE
#define gst_vaapi_buffer_proxy_ref_internal(proxy) \
  ((gpointer) gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (proxy)))

#define gst_vaapi_buffer_proxy_unref_internal(proxy) \
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy))

#define gst_vaapi_buffer_proxy_replace_internal(old_proxy_ptr, new_proxy) \
    gst_vaapi_mini_object_replace ((GstVaapiMiniObject **)(old_proxy_ptr), \
        GST_VAAPI_MINI_OBJECT (new_proxy))

#undef  gst_vaapi_buffer_proxy_ref
#define gst_vaapi_buffer_proxy_ref(proxy) \
    gst_vaapi_buffer_proxy_ref_internal ((proxy))

#undef  gst_vaapi_buffer_proxy_unref
#define gst_vaapi_buffer_proxy_unref(proxy) \
    gst_vaapi_buffer_proxy_unref_internal ((proxy))

#undef  gst_vaapi_buffer_proxy_replace
#define gst_vaapi_buffer_proxy_replace(old_proxy_ptr, new_proxy) \
    gst_vaapi_buffer_proxy_replace_internal ((old_proxy_ptr), (new_proxy))
#endif

G_END_DECLS

#endif /* GST_VAAPI_BUFFER_PROXY_PRIV_H */
