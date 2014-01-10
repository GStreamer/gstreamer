/*
 *  gstvaapiencoder_h264.h - H.264 encoder
 *
 *  Copyright (C) 2011-2013 Intel Corporation
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

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_H264(encoder) \
  ((GstVaapiEncoderH264 *) (encoder))

typedef struct _GstVaapiEncoderH264 GstVaapiEncoderH264;

/**
 * GstVaapiEncoderH264Prop:
 * @GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES: Number of B-frames between I
 *   and P (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_INIT_QP: Initial quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_MIN_QP: Minimal quantizer value (uint).
 * @GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES: Number of slices per frame (uint).
 *
 * The set of H.264 encoder specific configurable properties.
 */
typedef enum {
  GST_VAAPI_ENCODER_H264_PROP_MAX_BFRAMES = -1,
  GST_VAAPI_ENCODER_H264_PROP_INIT_QP = -2,
  GST_VAAPI_ENCODER_H264_PROP_MIN_QP = -3,
  GST_VAAPI_ENCODER_H264_PROP_NUM_SLICES = -4,
} GstVaapiEncoderH264Prop;

GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display);

GPtrArray *
gst_vaapi_encoder_h264_get_default_properties (void);

void
gst_vaapi_encoder_h264_set_avc (GstVaapiEncoderH264 * encoder, gboolean is_avc);

gboolean
gst_vaapi_encoder_h264_is_avc (GstVaapiEncoderH264 * encoder);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_H */
