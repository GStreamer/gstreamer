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

typedef struct _GstVaapiEncoder GstVaapiEncoder;

#define GST_VAAPI_ENCODER(encoder)  \
    ((GstVaapiEncoder *)(encoder))

/**
 * GstVaapiEncoderStatus:
 * @GST_VAAPI_ENCODER_STATUS_SUCCESS: Success.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_NO_SURFACE: No surface left to encode.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_NO_BUFFER: No coded buffer left to
 *   hold the encoded picture.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN: Unknown error.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED: No memory left.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED: The requested
 *   operation failed to execute properly. e.g. invalid point in time to
 *   execute the operation.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_RATE_CONTROL:
 *   Unsupported rate control value.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER: Invalid parameter.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_BUFFER: Invalid buffer.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_SURFACE: Invalid surface.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER: Invalid header.
 *
 * Set of #GstVaapiEncoder status codes.
 */
typedef enum
{
  GST_VAAPI_ENCODER_STATUS_SUCCESS = 0,
  GST_VAAPI_ENCODER_STATUS_NO_SURFACE = 1,
  GST_VAAPI_ENCODER_STATUS_NO_BUFFER = 2,

  GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN = -1,
  GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED = -2,
  GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED = -3,
  GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_RATE_CONTROL = -4,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER = -100,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_BUFFER = -101,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_SURFACE = -102,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER = -103,
} GstVaapiEncoderStatus;

/**
 * GstVaapiEncoderProp:
 * @GST_VAAPI_ENCODER_PROP_RATECONTROL: Rate control (#GstVaapiRateControl).
 * @GST_VAAPI_ENCODER_PROP_BITRATE: Bitrate expressed in kbps (uint).
 * @GST_VAAPI_ENCODER_PROP_KEYFRAME_PERIOD: The maximal distance
 *   between two keyframes (uint).
 *
 * The set of configurable properties for the encoder.
 */
typedef enum {
  GST_VAAPI_ENCODER_PROP_RATECONTROL = 1,
  GST_VAAPI_ENCODER_PROP_BITRATE,
  GST_VAAPI_ENCODER_PROP_KEYFRAME_PERIOD,
} GstVaapiEncoderProp;

/**
 * GstVaapiEncoderPropInfo:
 * @prop: the #GstVaapiEncoderProp
 * @pspec: the #GParamSpec describing the associated configurable value
 *
 * A #GstVaapiEncoderProp descriptor.
 */
typedef struct {
  const gint prop;
  GParamSpec *const pspec;
} GstVaapiEncoderPropInfo;

GstVaapiEncoder *
gst_vaapi_encoder_ref (GstVaapiEncoder * encoder);

void
gst_vaapi_encoder_unref (GstVaapiEncoder * encoder);

void
gst_vaapi_encoder_replace (GstVaapiEncoder ** old_encoder_ptr,
    GstVaapiEncoder * new_encoder);

GstVaapiEncoderStatus
gst_vaapi_encoder_get_codec_data (GstVaapiEncoder * encoder,
    GstBuffer ** out_codec_data_ptr);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_codec_state (GstVaapiEncoder * encoder,
    GstVideoCodecState * state);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_property (GstVaapiEncoder * encoder, gint prop_id,
    const GValue * value);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_rate_control (GstVaapiEncoder * encoder,
    GstVaapiRateControl rate_control);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_bitrate (GstVaapiEncoder * encoder, guint bitrate);

GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_keyframe_period (GstVaapiEncoder * encoder,
    guint keyframe_period);

GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer_with_timeout (GstVaapiEncoder * encoder,
    GstVaapiCodedBufferProxy ** out_codedbuf_proxy_ptr, guint64 timeout);

GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder);

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H */
