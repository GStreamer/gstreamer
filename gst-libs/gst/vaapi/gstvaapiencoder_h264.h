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

typedef struct _GstVaapiEncoderH264 GstVaapiEncoderH264;

GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display);

void
gst_vaapi_encoder_h264_set_avc (GstVaapiEncoderH264 * encoder, gboolean is_avc);

gboolean
gst_vaapi_encoder_h264_is_avc (GstVaapiEncoderH264 * encoder);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_H */
