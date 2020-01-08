/*
 *  gstvaapicodedbuffer_priv.h - VA coded buffer abstraction (private defs)
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

#ifndef GST_VAAPI_CODED_BUFFER_PRIV_H
#define GST_VAAPI_CODED_BUFFER_PRIV_H

#include <gst/vaapi/gstvaapicontext.h>
#include "gstvaapicodedbuffer.h"

G_BEGIN_DECLS

#define GST_VAAPI_CODED_BUFFER_CAST(obj) \
    ((GstVaapiCodedBuffer *)(obj))

/**
 * GstVaapiCodedBuffer:
 *
 * A VA coded buffer object wrapper.
 */
struct _GstVaapiCodedBuffer
{
  /*< private >*/
  GstMiniObject         mini_object;
  GstVaapiDisplay      *display;
  GstVaapiID            object_id;

  /*< public >*/
  VACodedBufferSegment *segment_list;
};

/**
 * GST_VAAPI_CODED_BUFFER_DISPLAY:
 * @buf: a #GstVaapiCodedBuffer
 *
 * Macro that evaluates to the #GstVaapiDisplay of @buf
 */
#undef GST_VAAPI_CODED_BUFFER_DISPLAY
#define GST_VAAPI_CODED_BUFFER_DISPLAY(buf) (GST_VAAPI_CODED_BUFFER (buf)->display)

/**
 * GST_VAAPI_CODED_BUFFER_ID:
 * @buf: a #GstVaapiCodedBuffer
 *
 * Macro that evaluates to the object ID of @buf
 */
#undef GST_VAAPI_CODED_BUFFER_ID
#define GST_VAAPI_CODED_BUFFER_ID(buf) (GST_VAAPI_CODED_BUFFER (buf)->object_id)

G_GNUC_INTERNAL
GstVaapiCodedBuffer *
gst_vaapi_coded_buffer_new (GstVaapiContext * context, guint buf_size);

G_GNUC_INTERNAL
gboolean
gst_vaapi_coded_buffer_map (GstVaapiCodedBuffer * buf,
    VACodedBufferSegment ** out_segment_list_ptr);

G_GNUC_INTERNAL
void
gst_vaapi_coded_buffer_unmap (GstVaapiCodedBuffer * buf);

G_END_DECLS

#endif /* GST_VAAPI_CODED_BUFFER_PRIV_H */
