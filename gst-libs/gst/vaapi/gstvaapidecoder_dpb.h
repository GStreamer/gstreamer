/*
 *  gstvaapidecoder_dpb.h - Decoded Picture Buffer
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

#ifndef GST_VAAPI_DECODER_DPB_H
#define GST_VAAPI_DECODER_DPB_H

#include <gst/vaapi/gstvaapidecoder_objects.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDpb             GstVaapiDpb;
typedef struct _GstVaapiDpbClass        GstVaapiDpbClass;

/* ------------------------------------------------------------------------- */
/* --- Decoded Picture Buffer                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_DPB(obj) \
    ((GstVaapiDpb *)(obj))

#define GST_VAAPI_IS_DPB(obj) \
    (GST_VAAPI_DPB(obj) != NULL)

G_GNUC_INTERNAL
GstVaapiDpb *
gst_vaapi_dpb_new(guint max_pictures);

G_GNUC_INTERNAL
void
gst_vaapi_dpb_flush(GstVaapiDpb *dpb);

G_GNUC_INTERNAL
gboolean
gst_vaapi_dpb_add(GstVaapiDpb *dpb, GstVaapiPicture *picture);

G_GNUC_INTERNAL
guint
gst_vaapi_dpb_size(GstVaapiDpb *dpb);

G_GNUC_INTERNAL
void
gst_vaapi_dpb_get_neighbours(
    GstVaapiDpb        *dpb,
    GstVaapiPicture    *picture,
    GstVaapiPicture   **prev_picture_ptr,
    GstVaapiPicture   **next_picture_ptr
);

#define gst_vaapi_dpb_ref(dpb) \
    gst_vaapi_mini_object_ref(GST_VAAPI_MINI_OBJECT(dpb))

#define gst_vaapi_dpb_unref(dpb) \
    gst_vaapi_mini_object_unref(GST_VAAPI_MINI_OBJECT(dpb))

#define gst_vaapi_dpb_replace(old_dpb_ptr, new_dpb) \
    gst_vaapi_mini_object_replace((GstVaapiMiniObject **)(old_dpb_ptr), \
        (GstVaapiMiniObject *)(new_dpb))

G_END_DECLS

#endif /* GST_VAAPI_DECODER_DPB */
