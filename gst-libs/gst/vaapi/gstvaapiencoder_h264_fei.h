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

#define GST_TYPE_VAAPI_ENCODER_H264_FEI \
    (gst_vaapi_encoder_h264_fei_get_type ())
#define GST_VAAPI_ENCODER_H264_FEI(encoder) \
    (G_TYPE_CHECK_INSTANCE_CAST ((encoder), GST_TYPE_VAAPI_ENCODER_H264_FEI, GstVaapiEncoderH264Fei))
#define GST_IS_VAAPI_ENCODER_H264_FEI(encoder) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((encoder), GST_TYPE_VAAPI_ENCODER_H264_FEI))

typedef struct _GstVaapiEncoderH264Fei GstVaapiEncoderH264Fei;
typedef struct _GstVaapiEncoderH264FeiClass GstVaapiEncoderH264FeiClass;

GType
gst_vaapi_encoder_h264_fei_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_h264_fei_new (GstVaapiDisplay * display);

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
