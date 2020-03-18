/*
 *  gstvaapiencoder.h - VA encoder abstraction
 *
 *  Copyright (C) 2013-2014 Intel Corporation
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

#ifndef GST_VAAPI_ENCODER_H
#define GST_VAAPI_ENCODER_H

#include <gst/video/gstvideoutils.h>
#include <gst/vaapi/gstvaapicodedbufferproxy.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODER \
    (gst_vaapi_encoder_get_type ())
#define GST_VAAPI_ENCODER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_ENCODER, GstVaapiEncoder))
#define GST_VAAPI_IS_ENCODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODER))

typedef struct _GstVaapiEncoder GstVaapiEncoder;

GType
gst_vaapi_encoder_get_type (void) G_GNUC_CONST;

/**
 * GST_VAAPI_PARAM_ENCODER_EXPOSURE: (value 65536)
 *
 * This user defined flag is added when the internal encoder class
 * wants to expose its property gparam spec to the according encode
 * class. */
#define GST_VAAPI_PARAM_ENCODER_EXPOSURE GST_PARAM_USER_SHIFT

/**
 * GstVaapiEncoderStatus:
 * @GST_VAAPI_ENCODER_STATUS_SUCCESS: Success.
 * @GST_VAAPI_ENCODER_STATUS_NO_SURFACE: No surface left to encode.
 * @GST_VAAPI_ENCODER_STATUS_NO_BUFFER: No coded buffer left to hold
 *   the encoded picture.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN: Unknown error.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED: No memory left.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED: The requested
 *   operation failed to execute properly. e.g. invalid point in time to
 *   execute the operation.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_RATE_CONTROL:
 *   Unsupported rate control value.
 * @GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE: Unsupported profile.
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
  GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE = -5,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER = -100,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_BUFFER = -101,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_SURFACE = -102,
  GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_HEADER = -103,
} GstVaapiEncoderStatus;

/**
 * GstVaapiEncoderTune:
 * @GST_VAAPI_ENCODER_TUNE_NONE: No tuning option set.
 * @GST_VAAPI_ENCODER_TUNE_HIGH_COMPRESSION: Tune for higher compression
 *   ratios, at the expense of lower compatibility at decoding time.
 * @GST_VAAPI_ENCODER_TUNE_LOW_LATENCY: Tune for low latency decoding.
 * @GST_VAAPI_ENCODER_TUNE_LOW_POWER: Tune encoder for low power /
 *   resources conditions. This can affect compression ratio or visual
 *   quality to match low power conditions.
 *
 * The set of tuning options for a #GstVaapiEncoder. By default,
 * maximum compatibility for decoding is preferred, so the lowest
 * coding tools are enabled.
 */
typedef enum {
  GST_VAAPI_ENCODER_TUNE_NONE = 0,
  GST_VAAPI_ENCODER_TUNE_HIGH_COMPRESSION,
  GST_VAAPI_ENCODER_TUNE_LOW_LATENCY,
  GST_VAAPI_ENCODER_TUNE_LOW_POWER,
} GstVaapiEncoderTune;

/**
 * GstVaapiEncoderMbbrc:
 * @GST_VAAPI_ENCODER_MBBRC_AUTO: bitrate control auto
 * @GST_VAAPI_ENCODER_MBBRC_ON: bitrate control on
 * @GST_VAAPI_ENCODER_MBBRC_OFF: bitrate control off
 *
 * Values for the macroblock level bitrate control.
 *
 * This property values are only available for H264 and H265 (HEVC)
 * encoders, when rate control is not Constant QP.
 **/
typedef enum {
  GST_VAAPI_ENCODER_MBBRC_AUTO = 0,
  GST_VAAPI_ENCODER_MBBRC_ON = 1,
  GST_VAAPI_ENCODER_MBBRC_OFF = 2,
} GstVaapiEncoderMbbrc;

GType
gst_vaapi_encoder_tune_get_type (void) G_GNUC_CONST;

GType
gst_vaapi_encoder_mbbrc_get_type (void) G_GNUC_CONST;

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
gst_vaapi_encoder_set_rate_control (GstVaapiEncoder * encoder,
    GstVaapiRateControl rate_control);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_bitrate (GstVaapiEncoder * encoder, guint bitrate);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_target_percentage (GstVaapiEncoder * encoder,
    guint target_percentage);

GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_keyframe_period (GstVaapiEncoder * encoder,
    guint keyframe_period);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_tuning (GstVaapiEncoder * encoder,
    GstVaapiEncoderTune tuning);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_quality_level (GstVaapiEncoder * encoder,
    guint quality_level);

GstVaapiEncoderStatus
gst_vaapi_encoder_set_trellis (GstVaapiEncoder * encoder, gboolean trellis);

GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer_with_timeout (GstVaapiEncoder * encoder,
    GstVaapiCodedBufferProxy ** out_codedbuf_proxy_ptr, guint64 timeout);

GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder);

GArray *
gst_vaapi_encoder_get_surface_attributes (GstVaapiEncoder * encoder,
    GArray * profiles, gint * min_width, gint * min_height,
    gint * max_width, gint * max_height, guint * mem_types);

GstVaapiProfile
gst_vaapi_encoder_get_profile (GstVaapiEncoder * encoder);

GstVaapiEntrypoint
gst_vaapi_encoder_get_entrypoint (GstVaapiEncoder * encoder,
    GstVaapiProfile profile);

GArray *
gst_vaapi_encoder_get_available_profiles (GstVaapiEncoder * encoder);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiEncoder, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H */
