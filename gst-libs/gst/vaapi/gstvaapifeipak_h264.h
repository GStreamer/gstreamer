/*
 *  gstvaapifeipak_h264.h - H.264 FEI PAK
 *
 *  Copyright (C) 2016-2018 Intel Corporation
 *    Author: Chen Xiaomin <xiaomin.chen@intel.com>
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef GST_VAAPI_FEIPAK_H264_H
#define GST_VAAPI_FEIPAK_H264_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h264.h>
#include <gst/vaapi/gstvaapifeiutils_h264.h>
#include <gst/vaapi/gstvaapifei_objects_priv.h>

G_BEGIN_DECLS

typedef struct _GstVaapiFEIPakH264 GstVaapiFEIPakH264;

/**
 * GstVaapiEncoderH264Prop:
 * @GST_VAAPI_FEIPAK_H264_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 * @GST_VAAPI_FEIPAK_H264_PROP_INIT_QP: Initial quantizer value (uint).
 * @GST_VAAPI_FEIPAK_H264_PROP_MIN_QP: Minimal quantizer value (uint).
 * @GST_VAAPI_FEIPAK_H264_PROP_NUM_SLICES: Number of slices per frame (uint).
 * @GST_VAAPI_FEIPAK_H264_PROP_CABAC: Enable CABAC entropy coding mode (bool).
 * @GST_VAAPI_FEIPAK_H264_PROP_DCT8X8: Enable adaptive use of 8x8
 *   transforms in I-frames (bool).
 * @GST_VAAPI_FEIPAK_H264_PROP_CPB_LENGTH: Length of the CPB buffer
 *   in milliseconds (uint).
 * @GST_VAAPI_FEIPAK_H264_PROP_NUM_VIEWS: Number of views per frame.
 * @GST_VAAPI_FEIPAK_H264_PROP_VIEW_IDS: View IDs
 *
 * The set of H.264 feipak specific configurable properties.
 */
typedef enum
{
  GST_VAAPI_FEIPAK_H264_PROP_MAX_BFRAMES = -1,
  GST_VAAPI_FEIPAK_H264_PROP_INIT_QP = -2,
  GST_VAAPI_FEIPAK_H264_PROP_MIN_QP = -3,
  GST_VAAPI_FEIPAK_H264_PROP_NUM_SLICES = -4,
  GST_VAAPI_FEIPAK_H264_PROP_CABAC = -5,
  GST_VAAPI_FEIPAK_H264_PROP_DCT8X8 = -6,
  GST_VAAPI_FEIPAK_H264_PROP_CPB_LENGTH = -7,
  GST_VAAPI_FEIPAK_H264_PROP_NUM_VIEWS = -8,
  GST_VAAPI_FEIPAK_H264_PROP_VIEW_IDS = -9,
  GST_VAAPI_FEIPAK_H264_PROP_NUM_REF = -10,
} GstVaapiFEIPakH264Prop;

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_reconfigure (GstVaapiFEIPakH264 * feipak,
    VAContextID va_context, GstVaapiProfile profile, guint8 profile_idc,
    guint mb_width, guint mb_height, guint32 num_views, guint slices_num,
    guint32 num_ref_frames);

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_encode (GstVaapiFEIPakH264 * feipak,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf,
    GstVaapiSurfaceProxy * surface, GstVaapiFeiInfoToPakH264 *info_to_pak);

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_flush (GstVaapiFEIPakH264 * feipak);

GstVaapiFEIPakH264 *gst_vaapi_feipak_h264_new (GstVaapiEncoder * encoder,
    GstVaapiDisplay * display, VAContextID va_context);

GstVaapiEncoderStatus
gst_vaapi_feipak_h264_set_property (GstVaapiFEIPakH264 * feipak,
    gint prop_id, const GValue * value);

gboolean
gst_vaapi_feipak_h264_get_ref_pool (GstVaapiFEIPakH264 * feipak,
    gpointer * ref_pool_ptr);

G_END_DECLS

#endif /*GST_VAAPI_FEIPAK_H264_H */
