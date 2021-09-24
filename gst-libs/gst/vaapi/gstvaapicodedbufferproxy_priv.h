/*
 *  gstvaapicodedbufferproxy_priv.h - VA coded buffer proxy (private defs)
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPI_CODED_BUFFER_PROXY_PRIV_H
#define GST_VAAPI_CODED_BUFFER_PROXY_PRIV_H

#include "gstvaapicodedbuffer_priv.h"
#include "gstvaapiminiobject.h"

G_BEGIN_DECLS

#define GST_VAAPI_CODED_BUFFER_PROXY(proxy) \
  ((GstVaapiCodedBufferProxy *)(proxy))

struct _GstVaapiCodedBufferProxy
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;

  GstVaapiVideoPool    *pool;
  GstVaapiCodedBuffer  *buffer;
  GDestroyNotify        destroy_func;
  gpointer              destroy_data;
  GDestroyNotify        user_data_destroy;
  gpointer              user_data;
};

/**
 * GST_VAAPI_CODED_BUFFER_PROXY_BUFFER:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Macro that evaluated to the underlying #GstVaapiCodedBuffer of @proxy.
 */
#undef  GST_VAAPI_CODED_BUFFER_PROXY_BUFFER
#define GST_VAAPI_CODED_BUFFER_PROXY_BUFFER(proxy) \
  GST_VAAPI_CODED_BUFFER_PROXY(proxy)->buffer

/**
 * GST_VAAPI_CODED_BUFFER_PROXY_BUFFER_SIZE:
 * @proxy: a #GstVaapiCodedBufferProxy
 *
 * Macro that evaluated to the underlying #GstVaapiCodedBuffer size of
 * @proxy.
 */
#undef  GST_VAAPI_CODED_BUFFER_PROXY_BUFFER_SIZE
#define GST_VAAPI_CODED_BUFFER_PROXY_BUFFER_SIZE(proxy) \
  GST_VAAPI_CODED_BUFFER_SIZE(GST_VAAPI_CODED_BUFFER_PROXY_BUFFER(proxy))

G_END_DECLS

#endif /* GST_VAAPI_CODED_BUFFER_PROXY_PRIV_H */
