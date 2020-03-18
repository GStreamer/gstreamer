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

GType
gst_vaapi_encoder_h264_get_type (void) G_GNUC_CONST;

GstVaapiEncoder *
gst_vaapi_encoder_h264_new (GstVaapiDisplay * display);

gboolean
gst_vaapi_encoder_h264_set_max_profile (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile profile);

gboolean
gst_vaapi_encoder_h264_get_profile_and_level (GstVaapiEncoderH264 * encoder,
    GstVaapiProfile * out_profile_ptr, GstVaapiLevelH264 * out_level_ptr);

gboolean
gst_vaapi_encoder_h264_supports_avc (GstVaapiEncoderH264 * encoder);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiEncoderH264, gst_object_unref)

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_H */
