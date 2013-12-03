/*
 *  gstvaapiencoder.h - VA encoder abstraction
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPI_ENCODER_H
#define GST_VAAPI_ENCODER_H

#include <gst/video/gstvideoutils.h>
#include <gst/vaapi/gstvaapicodedbufferproxy.h>

G_BEGIN_DECLS

typedef enum
{
  GST_VAAPI_ENCODER_STATUS_SUCCESS = 0,
  GST_VAAPI_ENCODER_STATUS_FRAME_NOT_READY = 1,
  GST_VAAPI_ENCODER_STATUS_NO_DATA = 2,
  GST_VAAPI_ENCODER_STATUS_TIMEOUT = 3,
  GST_VAAPI_ENCODER_STATUS_NOT_READY = 4,
  GST_VAAPI_ENCODER_STATUS_FRAME_IN_ORDER = 5,

  GST_VAAPI_ENCODER_STATUS_PARAM_ERR = -1,
  GST_VAAPI_ENCODER_STATUS_OBJECT_ERR = -2,
  GST_VAAPI_ENCODER_STATUS_PICTURE_ERR = -3,
  GST_VAAPI_ENCODER_STATUS_THREAD_ERR = -4,
  GST_VAAPI_ENCODER_STATUS_PROFILE_ERR = -5,
  GST_VAAPI_ENCODER_STATUS_FUNC_PTR_ERR = -6,
  GST_VAAPI_ENCODER_STATUS_MEM_ERROR = -7,
  GST_VAAPI_ENCODER_STATUS_UNKNOWN_ERR = -8,
} GstVaapiEncoderStatus;

typedef struct _GstVaapiEncoder GstVaapiEncoder;

#define GST_VAAPI_ENCODER(encoder)  \
    ((GstVaapiEncoder *)(encoder))

#define GST_VAAPI_ENCODER_CAST(encoder) \
    ((GstVaapiEncoder *)(encoder))

GstVaapiEncoder *
gst_vaapi_encoder_ref (GstVaapiEncoder * encoder);

void
gst_vaapi_encoder_unref (GstVaapiEncoder * encoder);

void
gst_vaapi_encoder_replace (GstVaapiEncoder ** old_encoder_ptr,
    GstVaapiEncoder * new_encoder);

GstCaps *gst_vaapi_encoder_set_format (GstVaapiEncoder * encoder,
    GstVideoCodecState * state, GstCaps * ref_caps);

GstVaapiEncoderStatus
gst_vaapi_encoder_get_codec_data (GstVaapiEncoder * encoder,
    GstBuffer ** codec_data_ptr);

GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame);

GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer (GstVaapiEncoder * encoder,
    GstVideoCodecFrame ** out_frame_ptr,
    GstVaapiCodedBufferProxy ** out_codedbuf_ptr, gint64 timeout);

GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder);

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H */
