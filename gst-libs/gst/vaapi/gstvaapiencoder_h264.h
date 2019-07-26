/*
 *  gstvaapiencoder_h264.h - H.264 encoder
 *
 *  Copyright (C) 2011-2014 Intel Corporation
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

#ifndef GST_VAAPI_ENCODER_H264_H
#define GST_VAAPI_ENCODER_H264_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h264.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODER_H264 \
    (gst_vaapi_encoder_h264_get_type ())
#define GST_VAAPI_ENCODER_H264(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_ENCODER_H264, GstVaapiEncoderH264))
#define GST_IS_VAAPI_ENCODER_H264(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_ENCODER_H264))

typedef struct _GstVaapiEncoderH264 GstVaapiEncoderH264;
typedef struct _GstVaapiEncoderH264Class GstVaapiEncoderH264Class;

/**
 * GstVaapiEncoderH264Prop:
 * @GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_INIT_QP: Initial quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_MIN_QP: Minimal quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES: Number of slices per frame (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_CABAC: Enable CABAC entropy coding mode (bool).
 * @GST_VAAPI_ENCODER_H264_PROP_DCT8X8: Enable adaptive use of 8x8
 *   transforms in I-frames (bool).
 * @GST_VAAPI_ENCODER_H264_PROP_CPB_LENGTH: Length of the CPB buffer
 *   in milliseconds (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_NUM_VIEWS: Number of views per frame.
 * @GST_VAAPI_ENCODER_H264_PROP_VIEW_IDS: View IDs
 * @GST_VAAPI_ENCODER_H264_PROP_AUD: Insert AUD as first NAL per frame.
 * @GST_VAAPI_ENCODER_H264_PROP_COMPLIANCE_MODE: Relax Compliance restrictions
 * @GST_VAAPI_ENCODER_H264_PROP_NUM_REF_FRAMES: Maximum number of reference frames.
 * @GST_VAAPI_ENCODER_H264_PROP_MBBRC: Macroblock level Bitrate Control.
 * @GST_VAAPI_ENCODER_H264_PROP_QP_IP: Difference of QP between I and P frame.
 * @GST_VAAPI_ENCODER_H264_PROP_QP_IB: Difference of QP between I and B frame.
 * @GST_VAAPI_ENCODER_H264_PROP_TEMPORAL_LEVELS: Number of temporal levels
 * @GST_VAAPI_ENCODER_H264_PROP_PREDICTION_TYPE: Reference picture selection modes
 * @GST_VAAPI_ENCODER_H264_PROP_MAX_QP: Maximal quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_QUALITY_FACTOR: Factor for ICQ/QVBR bitrate control mode.
 *
 * The set of H.264 encoder specific configurable properties.
 */
typedef enum {
  GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES = -1,
  GST_VAAPI_ENCODER_H264_PROP_INIT_QP = -2,
  GST_VAAPI_ENCODER_H264_PROP_MIN_QP = -3,
  GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES = -4,
  GST_VAAPI_ENCODER_H264_PROP_CABAC = -5,
  GST_VAAPI_ENCODER_H264_PROP_DCT8X8 = -6,
  GST_VAAPI_ENCODER_H264_PROP_CPB_LENGTH = -7,
  GST_VAAPI_ENCODER_H264_PROP_NUM_VIEWS = -8,
  GST_VAAPI_ENCODER_H264_PROP_VIEW_IDS = -9,
  GST_VAAPI_ENCODER_H264_PROP_AUD = -10,
  GST_VAAPI_ENCODER_H264_PROP_COMPLIANCE_MODE = -11,
  GST_VAAPI_ENCODER_H264_PROP_NUM_REF_FRAMES = -12,
  GST_VAAPI_ENCODER_H264_PROP_MBBRC = -13,
  GST_VAAPI_ENCODER_H264_PROP_QP_IP = -14,
  GST_VAAPI_ENCODER_H264_PROP_QP_IB = -15,
  GST_VAAPI_ENCODER_H264_PROP_TEMPORAL_LEVELS = -16,
  GST_VAAPI_ENCODER_H264_PROP_PREDICTION_TYPE = -17,
  GST_VAAPI_ENCODER_H264_PROP_MAX_QP = -18,
  GST_VAAPI_ENCODER_H264_PROP_QUALITY_FACTOR = -19,
} GstVaapiEncoderH264Prop;

GType
gst_vaapi_encoder_h264_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display);

GPtrArray *
gst_vaapi_encoder_h264_get_default_properties (void);

gboolean
gst_vaapi_encoder_h264_set_max_profile (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile profile);

gboolean
gst_vaapi_encoder_h264_get_profile_and_level (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiLevelH264 * out_level_ptr);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_H */
