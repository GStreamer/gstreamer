/*
 *  gstvaapiencoder_h265.h - H.265 encoder
 *
 *  Copyright (C) 2015 Intel Corporation
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

#ifndef GST_VAAPI_ENCODER_H265_H
#define GST_VAAPI_ENCODER_H265_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiutils_h265.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODER_H265 \
    (gst_vaapi_encoder_h265_get_type ())
#define GST_VAAPI_ENCODER_H265(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_ENCODER_H265, GstVaapiEncoderH265))
#define GST_IS_VAAPI_ENCODER_H265(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_ENCODER_H265))

typedef struct _GstVaapiEncoderH265 GstVaapiEncoderH265;
typedef struct _GstVaapiEncoderH265Class GstVaapiEncoderH265Class;

/**
 * GstVaapiEncoderH265Prop:
 * @GST_VAAPI_ENCODER_H265_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 * @GST_VAAPI_ENCODER_H265_PROP_INIT_QP: Initial quantizer value (uint).
 * @GST_VAAPI_ENCODER_H265_PROP_MIN_QP: Minimal quantizer value (uint).
 * @GST_VAAPI_ENCODER_H265_PROP_NUM_SLICES: Number of slices per frame (uint).
 * @GST_VAAPI_ENCODER_H265_PROP_NUM_REF_FRAMES: Maximum number of reference frames.
 * @GST_VAAPI_ENCODER_H265_PROP_CPB_LENGTH: Length of the CPB buffer
 *   in milliseconds (uint).
 * @GST_VAAPI_ENCODER_H265_PROP_MBBRC: Macroblock level Bitrate Control.
 * @GST_VAAPI_ENCODER_H265_PROP_QP_IP: Difference of QP between I and P frame.
 * @GST_VAAPI_ENCODER_H265_PROP_QP_IB: Difference of QP between I and B frame.
 * @GST_VAAPI_ENCODER_H265_PROP_LOW_DELAY_B: use low delay b feature.
 * @GST_VAAPI_ENCODER_H265_PROP_MAX_QP: Maximal quantizer value (uint).
 *
 * The set of H.265 encoder specific configurable properties.
 */
typedef enum {
  GST_VAAPI_ENCODER_H265_PROP_MAX_BFRAMES = -1,
  GST_VAAPI_ENCODER_H265_PROP_INIT_QP = -2,
  GST_VAAPI_ENCODER_H265_PROP_MIN_QP = -3,
  GST_VAAPI_ENCODER_H265_PROP_NUM_SLICES = -4,
  GST_VAAPI_ENCODER_H265_PROP_NUM_REF_FRAMES = -5,
  GST_VAAPI_ENCODER_H265_PROP_CPB_LENGTH = -7,
  GST_VAAPI_ENCODER_H265_PROP_MBBRC = -8,
  GST_VAAPI_ENCODER_H265_PROP_QP_IP = -9,
  GST_VAAPI_ENCODER_H265_PROP_QP_IB = -10,
  GST_VAAPI_ENCODER_H265_PROP_LOW_DELAY_B = -11,
  GST_VAAPI_ENCODER_H265_PROP_MAX_QP = -12,
} GstVaapiEncoderH265Prop;

GType
gst_vaapi_encoder_h265_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_h265_new (GstVaapiDisplay * display);

GPtrArray *
gst_vaapi_encoder_h265_get_default_properties (void);

gboolean
gst_vaapi_encoder_h265_set_max_profile (GstVaapiEncoderH265 * encoder,
    GstVaapiProfile profile);

gboolean
gst_vaapi_encoder_h265_get_profile_tier_level (GstVaapiEncoderH265 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiTierH265 *out_tier_ptr, GstVaapiLevelH265 * out_level_ptr);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H265_H */
