/*
 *  gstvaapicodedbuffer.h - VA coded buffer abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
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

#ifndef GST_VAAPI_CODED_BUFFER_H
#define GST_VAAPI_CODED_BUFFER_H

G_BEGIN_DECLS

#define GST_VAAPI_CODED_BUFFER(obj) \
  ((GstVaapiCodedBuffer *)(obj))

/**
 * GST_VAAPI_CODED_BUFFER_SIZE:
 * @buf: a #GstVaapiCodedBuffer
 *
 * Macro that evaluates to the size of the underlying VA coded buffer @buf
 */
#define GST_VAAPI_CODED_BUFFER_SIZE(buf) \
  gst_vaapi_coded_buffer_get_size (GST_VAAPI_CODED_BUFFER(buf))

typedef struct _GstVaapiCodedBuffer             GstVaapiCodedBuffer;
typedef struct _GstVaapiCodedBufferProxy        GstVaapiCodedBufferProxy;
typedef struct _GstVaapiCodedBufferPool         GstVaapiCodedBufferPool;

#define GST_TYPE_VAAPI_CODED_BUFFER (gst_vaapi_coded_buffer_get_type ())

GType
gst_vaapi_coded_buffer_get_type (void) G_GNUC_CONST;

/**
 * gst_vaapi_coded_buffer_unref: (skip)
 * @buf: (transfer full): a #GstVaapiCodedBuffer.
 *
 * Decreases the refcount of @buf. If the refcount reaches 0, the
 * @buf will be freed.
 */
static inline void
gst_vaapi_coded_buffer_unref (GstVaapiCodedBuffer * buf)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (buf));
}

gssize
gst_vaapi_coded_buffer_get_size (GstVaapiCodedBuffer * buf);

gboolean
gst_vaapi_coded_buffer_copy_into (GstBuffer * dest, GstVaapiCodedBuffer * src);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiCodedBuffer, gst_vaapi_coded_buffer_unref)

G_END_DECLS

#endif /* GST_VAAPI_CODED_BUFFER_H */
