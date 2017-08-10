/*
 *  gstvaapiencoder_h264_fei.h - H.264 FEI encoder
 *
 *  Copyright (C) 2016-2017 Intel Corporation
 *    Author: Yi A Wang <yi.a.wang@intel.com>
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

#ifndef GST_VAAPI_ENCODER_H264_FEI_H
#define GST_VAAPI_ENCODER_H264_FEI_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h264.h>
#include <gst/vaapi/gstvaapifeiutils_h264.h>

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_H264_FEI(encoder) \
  ((GstVaapiEncoderH264Fei *) (encoder))

typedef struct _GstVaapiEncoderH264Fei GstVaapiEncoderH264Fei;

/**
 * GstVaapiEncoderH264FeiProp:
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_INIT_QP: Initial quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_MIN_QP: Minimal quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_NUM_SLICES: Number of slices per frame (uint).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_CABAC: Enable CABAC entropy coding mode (bool).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_DCT8X8: Enable adaptive use of 8x8
 *   transforms in I-frames (bool).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_CPB_LENGTH: Length of the CPB buffer
 *   in milliseconds (uint).
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_NUM_VIEWS: Number of views per frame.
 * @GST_VAAPI_ENCODER_H264_FEI_PROP_VIEW_IDS: View IDs
 *
 * The set of H.264 encoder specific configurable properties.
 */
typedef enum {
  GST_VAAPI_ENCODER_H264_FEI_PROP_MAX_BFRAMES = -1,
  GST_VAAPI_ENCODER_H264_FEI_PROP_INIT_QP = -2,
  GST_VAAPI_ENCODER_H264_FEI_PROP_MIN_QP = -3,
  GST_VAAPI_ENCODER_H264_FEI_PROP_NUM_SLICES = -4,
  GST_VAAPI_ENCODER_H264_FEI_PROP_CABAC = -5,
  GST_VAAPI_ENCODER_H264_FEI_PROP_DCT8X8 = -6,
  GST_VAAPI_ENCODER_H264_FEI_PROP_CPB_LENGTH = -7,
  GST_VAAPI_ENCODER_H264_FEI_PROP_NUM_VIEWS = -8,
  GST_VAAPI_ENCODER_H264_FEI_PROP_VIEW_IDS = -9,
  GST_VAAPI_ENCODER_H264_PROP_FEI_DISABLE= -11,
  GST_VAAPI_ENCODER_H264_PROP_NUM_MV_PREDICT_L0 = -12,
  GST_VAAPI_ENCODER_H264_PROP_NUM_MV_PREDICT_L1 = -13,
  GST_VAAPI_ENCODER_H264_PROP_SEARCH_WINDOW = -14,
  GST_VAAPI_ENCODER_H264_PROP_LEN_SP = -15,
  GST_VAAPI_ENCODER_H264_PROP_SEARCH_PATH = -16,
  GST_VAAPI_ENCODER_H264_PROP_REF_WIDTH = -17,
  GST_VAAPI_ENCODER_H264_PROP_REF_HEIGHT = -18,
  GST_VAAPI_ENCODER_H264_PROP_SUBMB_MASK = -19,
  GST_VAAPI_ENCODER_H264_PROP_SUBPEL_MODE = -20,
  GST_VAAPI_ENCODER_H264_PROP_INTRA_PART_MASK = -21,
  GST_VAAPI_ENCODER_H264_PROP_INTRA_SAD = -22,
  GST_VAAPI_ENCODER_H264_PROP_INTER_SAD = -23,
  GST_VAAPI_ENCODER_H264_PROP_ADAPT_SEARCH = -24,
  GST_VAAPI_ENCODER_H264_PROP_MULTI_PRED_L0 = -25,
  GST_VAAPI_ENCODER_H264_PROP_MULTI_PRED_L1 = -26,
  GST_VAAPI_ENCODER_H264_PROP_ENABLE_STATS_OUT = -27,
  GST_VAAPI_ENCODER_H264_PROP_FEI_MODE = -28

} GstVaapiEncoderH264FeiProp;

GstVaapiEncoder *
gst_vaapi_encoder_h264_fei_new (GstVaapiDisplay * display);

GPtrArray *
gst_vaapi_encoder_h264_fei_get_default_properties (void);

gboolean
gst_vaapi_encoder_h264_fei_set_max_profile (GstVaapiEncoderH264Fei * encoder,
    GstVaapiProfile profile);

gboolean
gst_vaapi_encoder_h264_fei_get_profile_and_level (GstVaapiEncoderH264Fei * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiLevelH264 * out_level_ptr);

gboolean
gst_vaapi_encoder_h264_is_fei_stats_out_enabled (GstVaapiEncoderH264Fei * encoder);

GstVaapiFeiMode
gst_vaapi_encoder_h264_fei_get_function_mode (GstVaapiEncoderH264Fei *encoder);

void
gst_vaapi_encoder_h264_fei_set_function_mode (GstVaapiEncoderH264Fei * encoder,
    guint fei_mode);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_FEI_H */
