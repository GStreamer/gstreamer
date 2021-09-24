/*
 *  gstvaapiencoder.c - VA encoder abstraction
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

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiencoder.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_core.h"
#include "gstvaapivalue.h"

#define DEBUG 1
#include "gstvaapidebug.h"

gboolean
gst_vaapi_encoder_ensure_param_quality_level (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncMiscParam *misc;

  /* quality level param is not supported */
  if (GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder) == 0)
    return TRUE;

  misc = GST_VAAPI_ENC_QUALITY_LEVEL_MISC_PARAM_NEW (encoder);
  if (!misc)
    return FALSE;
  memcpy (misc->data, &encoder->va_quality_level,
      sizeof (encoder->va_quality_level));
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);
  return TRUE;
}

gboolean
gst_vaapi_encoder_ensure_param_control_rate (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncMiscParam *misc;

  if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CQP)
    return TRUE;

  /* HRD params */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (HRD, encoder);
  if (!misc)
    return FALSE;
  memcpy (misc->data, &GST_VAAPI_ENCODER_VA_HRD (encoder),
      sizeof (VAEncMiscParameterHRD));
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  /* RateControl params */
  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (RateControl, encoder);
  if (!misc)
    return FALSE;
  memcpy (misc->data, &GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder),
      sizeof (VAEncMiscParameterRateControl));
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  /* FrameRate params */
  if (GST_VAAPI_ENCODER_VA_FRAME_RATE (encoder).framerate == 0)
    return TRUE;

  misc = GST_VAAPI_ENC_MISC_PARAM_NEW (FrameRate, encoder);
  if (!misc)
    return FALSE;
  memcpy (misc->data, &GST_VAAPI_ENCODER_VA_FRAME_RATE (encoder),
      sizeof (VAEncMiscParameterFrameRate));
  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);

  return TRUE;
}

gboolean
gst_vaapi_encoder_ensure_param_trellis (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture)
{
#if VA_CHECK_VERSION(1,0,0)
  GstVaapiEncMiscParam *misc;
  VAEncMiscParameterQuantization *param;

  if (!encoder->trellis)
    return TRUE;

  misc = GST_VAAPI_ENC_QUANTIZATION_MISC_PARAM_NEW (encoder);
  if (!misc)
    return FALSE;
  if (!misc->data)
    return FALSE;

  param = (VAEncMiscParameterQuantization *) misc->data;
  param->quantization_flags.bits.disable_trellis = 0;
  param->quantization_flags.bits.enable_trellis_I = 1;
  param->quantization_flags.bits.enable_trellis_B = 1;
  param->quantization_flags.bits.enable_trellis_P = 1;

  gst_vaapi_enc_picture_add_misc_param (picture, misc);
  gst_vaapi_codec_object_replace (&misc, NULL);
#endif
  return TRUE;
}

gboolean
gst_vaapi_encoder_ensure_param_roi_regions (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture)
{
#if VA_CHECK_VERSION(0,39,1)
  GstVaapiContextInfo *const cip = &encoder->context_info;
  const GstVaapiConfigInfoEncoder *const config = &cip->config.encoder;
  VAEncMiscParameterBufferROI *roi_param;
  GstVaapiEncMiscParam *misc;
  VAEncROI *region_roi;
  GstBuffer *input;
  guint num_roi, i;
  gpointer state = NULL;

  if (!config->roi_capability)
    return TRUE;

  if (!picture->frame)
    return FALSE;

  input = picture->frame->input_buffer;
  if (!input)
    return FALSE;

  num_roi =
      gst_buffer_get_n_meta (input, GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
  if (num_roi == 0)
    return TRUE;
  num_roi = CLAMP (num_roi, 1, config->roi_num_supported);

  misc =
      gst_vaapi_enc_misc_param_new (encoder, VAEncMiscParameterTypeROI,
      sizeof (VAEncMiscParameterBufferROI) + num_roi * sizeof (VAEncROI));
  if (!misc)
    return FALSE;

  region_roi =
      (VAEncROI *) ((guint8 *) misc->param + sizeof (VAEncMiscParameterBuffer) +
      sizeof (VAEncMiscParameterBufferROI));

  roi_param = misc->data;
  roi_param->num_roi = num_roi;
  roi_param->roi = region_roi;

  /* roi_value in VAEncROI should be used as ROI delta QP */
  roi_param->roi_flags.bits.roi_value_is_qp_delta = 1;
  roi_param->max_delta_qp = 10;
  roi_param->min_delta_qp = -10;

  for (i = 0; i < num_roi; i++) {
    GstVideoRegionOfInterestMeta *roi;
    GstStructure *s;

    roi = (GstVideoRegionOfInterestMeta *)
        gst_buffer_iterate_meta_filtered (input, &state,
        GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE);
    if (!roi)
      continue;

    /* ignore roi if overflow */
    if ((roi->x > G_MAXINT16) || (roi->y > G_MAXINT16)
        || (roi->w > G_MAXUINT16) || (roi->h > G_MAXUINT16))
      continue;

    GST_LOG ("Input buffer ROI: type=%s id=%d (%d, %d) %dx%d",
        g_quark_to_string (roi->roi_type), roi->id, roi->x, roi->y, roi->w,
        roi->h);

    picture->has_roi = TRUE;

    region_roi[i].roi_rectangle.x = roi->x;
    region_roi[i].roi_rectangle.y = roi->y;
    region_roi[i].roi_rectangle.width = roi->w;
    region_roi[i].roi_rectangle.height = roi->h;

    s = gst_video_region_of_interest_meta_get_param (roi, "roi/vaapi");
    if (s) {
      int value = 0;

      if (!gst_structure_get_int (s, "delta-qp", &value))
        continue;
      value = CLAMP (value, roi_param->min_delta_qp, roi_param->max_delta_qp);
      region_roi[i].roi_value = value;
    } else {
      region_roi[i].roi_value = encoder->default_roi_value;

      GST_LOG ("No ROI value specified upstream, use default (%d)",
          encoder->default_roi_value);
    }
  }

  if (picture->has_roi)
    gst_vaapi_enc_picture_add_misc_param (picture, misc);

  gst_vaapi_codec_object_replace (&misc, NULL);
#endif
  return TRUE;
}

/**
 * gst_vaapi_encoder_replace:
 * @old_encoder_ptr: a pointer to a #GstVaapiEncoder
 * @new_encoder: a #GstVaapiEncoder
 *
 * Atomically replaces the encoder encoder held in @old_encoder_ptr
 * with @new_encoder. This means that @old_encoder_ptr shall reference
 * a valid encoder. However, @new_encoder can be NULL.
 */
void
gst_vaapi_encoder_replace (GstVaapiEncoder ** old_encoder_ptr,
    GstVaapiEncoder * new_encoder)
{
  gst_object_replace ((GstObject **) old_encoder_ptr,
      (GstObject *) new_encoder);
}

/* Notifies gst_vaapi_encoder_create_coded_buffer() that a new buffer is free */
static void
_coded_buffer_proxy_released_notify (GstVaapiEncoder * encoder)
{
  g_mutex_lock (&encoder->mutex);
  g_cond_signal (&encoder->codedbuf_free);
  g_mutex_unlock (&encoder->mutex);
}

/* Creates a new VA coded buffer object proxy, backed from a pool */
static GstVaapiCodedBufferProxy *
gst_vaapi_encoder_create_coded_buffer (GstVaapiEncoder * encoder)
{
  GstVaapiCodedBufferPool *const pool =
      GST_VAAPI_CODED_BUFFER_POOL (encoder->codedbuf_pool);
  GstVaapiCodedBufferProxy *codedbuf_proxy;

  g_mutex_lock (&encoder->mutex);
  do {
    codedbuf_proxy = gst_vaapi_coded_buffer_proxy_new_from_pool (pool);
    if (codedbuf_proxy)
      break;

    /* Wait for a free coded buffer to become available */
    g_cond_wait (&encoder->codedbuf_free, &encoder->mutex);
    codedbuf_proxy = gst_vaapi_coded_buffer_proxy_new_from_pool (pool);
  } while (0);
  g_mutex_unlock (&encoder->mutex);
  if (!codedbuf_proxy)
    return NULL;

  gst_vaapi_coded_buffer_proxy_set_destroy_notify (codedbuf_proxy,
      (GDestroyNotify) _coded_buffer_proxy_released_notify, encoder);
  return codedbuf_proxy;
}

/* Notifies gst_vaapi_encoder_create_surface() that a new surface is free */
static void
_surface_proxy_released_notify (GstVaapiEncoder * encoder)
{
  g_mutex_lock (&encoder->mutex);
  g_cond_signal (&encoder->surface_free);
  g_mutex_unlock (&encoder->mutex);
}

/* Creates a new VA surface object proxy, backed from a pool and
   useful to allocate reconstructed surfaces */
GstVaapiSurfaceProxy *
gst_vaapi_encoder_create_surface (GstVaapiEncoder * encoder)
{
  GstVaapiSurfaceProxy *proxy;

  g_return_val_if_fail (encoder->context != NULL, NULL);

  g_mutex_lock (&encoder->mutex);
  for (;;) {
    proxy = gst_vaapi_context_get_surface_proxy (encoder->context);
    if (proxy)
      break;

    /* Wait for a free surface proxy to become available */
    g_cond_wait (&encoder->surface_free, &encoder->mutex);
  }
  g_mutex_unlock (&encoder->mutex);

  gst_vaapi_surface_proxy_set_destroy_notify (proxy,
      (GDestroyNotify) _surface_proxy_released_notify, encoder);
  return proxy;
}

/* Create a coded buffer proxy where the picture is going to be
 * decoded, the subclass encode vmethod is called and, if it doesn't
 * fail, the coded buffer is pushed into the async queue */
static GstVaapiEncoderStatus
gst_vaapi_encoder_encode_and_queue (GstVaapiEncoder * encoder,
    GstVaapiEncPicture * picture)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiCodedBufferProxy *codedbuf_proxy;
  GstVaapiEncoderStatus status;

  codedbuf_proxy = gst_vaapi_encoder_create_coded_buffer (encoder);
  if (!codedbuf_proxy)
    goto error_create_coded_buffer;

  status = klass->encode (encoder, picture, codedbuf_proxy);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto error_encode;

  gst_vaapi_coded_buffer_proxy_set_user_data (codedbuf_proxy,
      picture, (GDestroyNotify) gst_vaapi_mini_object_unref);
  g_async_queue_push (encoder->codedbuf_queue, codedbuf_proxy);
  encoder->num_codedbuf_queued++;

  return status;

  /* ERRORS */
error_create_coded_buffer:
  {
    GST_ERROR ("failed to allocate coded buffer");
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_encode:
  {
    GST_ERROR ("failed to encode frame (status = %d)", status);
    gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
    return status;
  }
}

/**
 * gst_vaapi_encoder_put_frame:
 * @encoder: a #GstVaapiEncoder
 * @frame: a #GstVideoCodecFrame
 *
 * Queues a #GstVideoCodedFrame to the HW encoder. The encoder holds
 * an extra reference to the @frame.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiEncoderStatus status;
  GstVaapiEncPicture *picture;

  for (;;) {
    picture = NULL;
    status = klass->reordering (encoder, frame, &picture);
    if (status == GST_VAAPI_ENCODER_STATUS_NO_SURFACE)
      break;
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_reorder_frame;

    status = gst_vaapi_encoder_encode_and_queue (encoder, picture);
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_encode;

    /* Try again with any pending reordered frame now available for encoding */
    frame = NULL;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_reorder_frame:
  {
    GST_ERROR ("failed to process reordered frames");
    return status;
  }
error_encode:
  {
    gst_vaapi_enc_picture_unref (picture);
    return status;
  }
}

/**
 * gst_vaapi_encoder_get_buffer_with_timeout:
 * @encoder: a #GstVaapiEncoder
 * @out_codedbuf_proxy_ptr: the next coded buffer as a #GstVaapiCodedBufferProxy
 * @timeout: the number of microseconds to wait for the coded buffer, at most
 *
 * Upon successful return, *@out_codedbuf_proxy_ptr contains the next
 * coded buffer as a #GstVaapiCodedBufferProxy. The caller owns this
 * object, so gst_vaapi_coded_buffer_proxy_unref() shall be called
 * after usage. Otherwise, @GST_VAAPI_DECODER_STATUS_ERROR_NO_BUFFER
 * is returned if no coded buffer is available so far (timeout).
 *
 * The parent frame is available as a #GstVideoCodecFrame attached to
 * the user-data anchor of the output coded buffer. Ownership of the
 * frame is transferred to the coded buffer.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer_with_timeout (GstVaapiEncoder * encoder,
    GstVaapiCodedBufferProxy ** out_codedbuf_proxy_ptr, guint64 timeout)
{
  GstVaapiEncPicture *picture;
  GstVaapiCodedBufferProxy *codedbuf_proxy;

  codedbuf_proxy = g_async_queue_timeout_pop (encoder->codedbuf_queue, timeout);
  if (!codedbuf_proxy)
    return GST_VAAPI_ENCODER_STATUS_NO_BUFFER;

  /* Wait for completion of all operations and report any error that occurred */
  picture = gst_vaapi_coded_buffer_proxy_get_user_data (codedbuf_proxy);
  if (!gst_vaapi_surface_sync (picture->surface))
    goto error_invalid_buffer;

  gst_vaapi_coded_buffer_proxy_set_user_data (codedbuf_proxy,
      gst_video_codec_frame_ref (picture->frame),
      (GDestroyNotify) gst_video_codec_frame_unref);

  if (out_codedbuf_proxy_ptr)
    *out_codedbuf_proxy_ptr = gst_vaapi_coded_buffer_proxy_ref (codedbuf_proxy);
  gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR ("failed to encode the frame");
    gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_SURFACE;
  }
}

static inline gboolean
_get_pending_reordered (GstVaapiEncoder * encoder,
    GstVaapiEncPicture ** picture, gpointer * state)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  if (!klass->get_pending_reordered)
    return FALSE;
  return klass->get_pending_reordered (encoder, picture, state);
}

/**
 * gst_vaapi_encoder_flush:
 * @encoder: a #GstVaapiEncoder
 *
 * Submits any pending (reordered) frame for encoding.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiEncPicture *picture;
  GstVaapiEncoderStatus status;
  gpointer iter = NULL;

  picture = NULL;
  while (_get_pending_reordered (encoder, &picture, &iter)) {
    if (!picture)
      continue;
    status = gst_vaapi_encoder_encode_and_queue (encoder, picture);
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_encode;
  }
  g_free (iter);

  return klass->flush (encoder);

  /* ERRORS */
error_encode:
  {
    gst_vaapi_enc_picture_unref (picture);
    return status;
  }
}

/**
 * gst_vaapi_encoder_get_codec_data:
 * @encoder: a #GstVaapiEncoder
 * @out_codec_data_ptr: the pointer to the resulting codec-data (#GstBuffer)
 *
 * Returns a codec-data buffer that best represents the encoded
 * bitstream. Upon successful return, and if the @out_codec_data_ptr
 * contents is not NULL, then the caller function shall deallocates
 * that buffer with gst_buffer_unref().
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_get_codec_data (GstVaapiEncoder * encoder,
    GstBuffer ** out_codec_data_ptr)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  *out_codec_data_ptr = NULL;
  if (!klass->get_codec_data)
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  ret = klass->get_codec_data (encoder, out_codec_data_ptr);
  return ret;
}

/* Checks video info */
static GstVaapiEncoderStatus
check_video_info (GstVaapiEncoder * encoder, const GstVideoInfo * vip)
{
  if (!vip->width || !vip->height)
    goto error_invalid_resolution;
  if (vip->fps_n < 0 || vip->fps_d <= 0)
    goto error_invalid_framerate;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_invalid_resolution:
  {
    GST_ERROR ("invalid resolution (%dx%d)", vip->width, vip->height);
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
error_invalid_framerate:
  {
    GST_ERROR ("invalid framerate (%d/%d)", vip->fps_n, vip->fps_d);
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
}

/* Gets a compatible profile for the active codec */
static GstVaapiProfile
get_compatible_profile (GstVaapiEncoder * encoder)
{
  const GstVaapiEncoderClassData *const cdata =
      GST_VAAPI_ENCODER_GET_CLASS (encoder)->class_data;
  GstVaapiProfile profile;
  GArray *profiles;
  guint i;

  profiles = gst_vaapi_display_get_encode_profiles (encoder->display);
  if (!profiles)
    return GST_VAAPI_PROFILE_UNKNOWN;

  // Pick a profile matching the class codec
  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    if (gst_vaapi_profile_get_codec (profile) == cdata->codec)
      break;
  }
  if (i == profiles->len)
    profile = GST_VAAPI_PROFILE_UNKNOWN;

  g_array_unref (profiles);
  return profile;
}

/* Gets a supported profile for the active codec */
static GstVaapiProfile
get_profile (GstVaapiEncoder * encoder)
{
  if (!encoder->profile)
    encoder->profile = get_compatible_profile (encoder);
  return encoder->profile;
}

/* Gets config attribute for the supplied profile */
static gboolean
get_config_attribute (GstVaapiEncoder * encoder, VAConfigAttribType type,
    guint * out_value_ptr)
{
  GstVaapiProfile profile;
  VAProfile va_profile;
  VAEntrypoint va_entrypoint;

  profile = get_profile (encoder);
  if (!profile)
    return FALSE;
  va_profile = gst_vaapi_profile_get_va_profile (profile);

  va_entrypoint =
      gst_vaapi_entrypoint_get_va_entrypoint (encoder->context_info.entrypoint);

  return gst_vaapi_get_config_attribute (encoder->display, va_profile,
      va_entrypoint, type, out_value_ptr);
}

/* Determines the set of supported packed headers */
static guint
get_packed_headers (GstVaapiEncoder * encoder)
{
  const GstVaapiEncoderClassData *const cdata =
      GST_VAAPI_ENCODER_GET_CLASS (encoder)->class_data;
  guint value;

  if (encoder->got_packed_headers)
    return encoder->packed_headers;

  if (!get_config_attribute (encoder, VAConfigAttribEncPackedHeaders, &value))
    value = 0;
  GST_INFO ("supported packed headers: 0x%08x", value);

  encoder->got_packed_headers = TRUE;
  encoder->packed_headers = cdata->packed_headers & value;

  return encoder->packed_headers;
}

static gboolean
get_roi_capability (GstVaapiEncoder * encoder, guint * num_roi_supported)
{
#if VA_CHECK_VERSION(0,39,1)
  VAConfigAttribValEncROI *roi_config;
  guint value;

  if (!get_config_attribute (encoder, VAConfigAttribEncROI, &value))
    return FALSE;

  roi_config = (VAConfigAttribValEncROI *) & value;

  if (roi_config->bits.num_roi_regions == 0)
    return FALSE;

  /* Only support QP delta, and it only makes sense when rate control
   * is not CQP */
  if ((GST_VAAPI_ENCODER_RATE_CONTROL (encoder) != GST_VAAPI_RATECONTROL_CQP)
      && (VA_ROI_RC_QP_DELTA_SUPPORT (roi_config) == 0))
    return FALSE;

  GST_INFO ("Support for ROI - number of regions supported: %d",
      roi_config->bits.num_roi_regions);

  *num_roi_supported = roi_config->bits.num_roi_regions;
  return TRUE;
#else
  return FALSE;
#endif
}

static inline gboolean
is_chroma_type_supported (GstVaapiEncoder * encoder)
{
  GstVaapiContextInfo *const cip = &encoder->context_info;
  const GstVideoFormat fmt =
      GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder));
  guint format = 0;

  if (fmt == GST_VIDEO_FORMAT_ENCODED)
    return TRUE;

  if (cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV420 &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV422 &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV420_10BPP &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV444 &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV444_10BPP &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV422_10BPP &&
      cip->chroma_type != GST_VAAPI_CHROMA_TYPE_YUV420_12BPP)
    goto unsupported;

  if (!get_config_attribute (encoder, VAConfigAttribRTFormat, &format))
    return FALSE;

  if (!(format & from_GstVaapiChromaType (cip->chroma_type)))
    goto unsupported;

  return TRUE;

  /* ERRORS */
unsupported:
  {
    GST_ERROR ("The encoding format %s is not supported, "
        "Please try to use vaapipostproc to convert the input format.",
        gst_video_format_to_string (fmt));
    return FALSE;
  }
}

static guint
get_default_chroma_type (GstVaapiEncoder * encoder,
    const GstVaapiContextInfo * cip)
{
  guint value;

  if (!gst_vaapi_get_config_attribute (encoder->display,
          gst_vaapi_profile_get_va_profile (cip->profile),
          gst_vaapi_entrypoint_get_va_entrypoint (cip->entrypoint),
          VAConfigAttribRTFormat, &value))
    return 0;

  return to_GstVaapiChromaType (value);
}

static void
init_context_info (GstVaapiEncoder * encoder, GstVaapiContextInfo * cip)
{
  cip->usage = GST_VAAPI_CONTEXT_USAGE_ENCODE;
  cip->chroma_type = get_default_chroma_type (encoder, cip);
  cip->width = 0;
  cip->height = 0;
  cip->ref_frames = encoder->num_ref_frames;
}

/* Updates video context */
static gboolean
set_context_info (GstVaapiEncoder * encoder)
{
  GstVaapiContextInfo *const cip = &encoder->context_info;
  GstVaapiConfigInfoEncoder *const config = &cip->config.encoder;
  const GstVideoFormat format =
      GST_VIDEO_INFO_FORMAT (GST_VAAPI_ENCODER_VIDEO_INFO (encoder));

  g_assert (cip->profile != GST_VAAPI_PROFILE_UNKNOWN);
  g_assert (cip->entrypoint != GST_VAAPI_ENTRYPOINT_INVALID);

  init_context_info (encoder, cip);
  cip->chroma_type = gst_vaapi_video_format_get_chroma_type (format);
  cip->width = GST_VAAPI_ENCODER_WIDTH (encoder);
  cip->height = GST_VAAPI_ENCODER_HEIGHT (encoder);

  if (!is_chroma_type_supported (encoder))
    goto error_unsupported_format;

  memset (config, 0, sizeof (*config));
  config->rc_mode = GST_VAAPI_ENCODER_RATE_CONTROL (encoder);
  config->packed_headers = get_packed_headers (encoder);
  config->roi_capability =
      get_roi_capability (encoder, &config->roi_num_supported);

  return TRUE;

  /* ERRORS */
error_unsupported_format:
  {
    GST_ERROR ("failed to determine chroma type for format %s",
        gst_vaapi_video_format_to_string (format));
    return FALSE;
  }
}

/* Ensures the underlying VA context for encoding is created */
static gboolean
gst_vaapi_encoder_ensure_context (GstVaapiEncoder * encoder)
{
  GstVaapiContextInfo *const cip = &encoder->context_info;

  if (!set_context_info (encoder))
    return FALSE;

  if (encoder->context) {
    if (!gst_vaapi_context_reset (encoder->context, cip))
      return FALSE;
  } else {
    encoder->context = gst_vaapi_context_new (encoder->display, cip);
    if (!encoder->context)
      return FALSE;
  }
  encoder->va_context = gst_vaapi_context_get_id (encoder->context);
  return TRUE;
}

/* Reconfigures the encoder with the new properties */
static GstVaapiEncoderStatus
gst_vaapi_encoder_reconfigure_internal (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);
  GstVaapiEncoderStatus status;
  GstVaapiVideoPool *pool;
  guint codedbuf_size, target_percentage;
  guint fps_d, fps_n;
  guint quality_level_max = 0;

  fps_d = GST_VIDEO_INFO_FPS_D (vip);
  fps_n = GST_VIDEO_INFO_FPS_N (vip);

  /* Generate a keyframe every second */
  if (!encoder->keyframe_period)
    encoder->keyframe_period = (fps_n + fps_d - 1) / fps_d;

  /* Default frame rate parameter */
  if (fps_d > 0 && fps_n > 0)
    GST_VAAPI_ENCODER_VA_FRAME_RATE (encoder).framerate = fps_d << 16 | fps_n;

  target_percentage =
      (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) == GST_VAAPI_RATECONTROL_CBR) ?
      100 : encoder->target_percentage;

  /* *INDENT-OFF* */
  /* Default values for rate control parameter */
  GST_VAAPI_ENCODER_VA_RATE_CONTROL (encoder) = (VAEncMiscParameterRateControl) {
    .bits_per_second = encoder->bitrate * 1000,
    .target_percentage = target_percentage,
    .window_size = 500,
  };
  /* *INDENT-ON* */

  status = klass->reconfigure (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  if (!gst_vaapi_encoder_ensure_context (encoder))
    goto error_reset_context;

  if (get_config_attribute (encoder, VAConfigAttribEncQualityRange,
          &quality_level_max) && quality_level_max > 0) {
    GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder) =
        CLAMP (GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder), 1, quality_level_max);
  } else {
    GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder) = 0;
  }
  GST_INFO ("Quality level is fixed to %d",
      GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder));

  if (encoder->trellis) {
#if VA_CHECK_VERSION(1,0,0)
    guint quantization_method = 0;
    if (get_config_attribute (encoder, VAConfigAttribEncQuantization,
            &quantization_method) == FALSE
        || !(quantization_method & VA_ENC_QUANTIZATION_TRELLIS_SUPPORTED)) {

      GST_INFO ("Trellis Quantization is not supported,"
          " trellis will be disabled");
      encoder->trellis = FALSE;
    }
#else
    GST_INFO ("The encode trellis quantization option is not supported"
        " in this VAAPI version.");
    encoder->trellis = FALSE;
#endif
  }

  codedbuf_size = encoder->codedbuf_pool ?
      gst_vaapi_coded_buffer_pool_get_buffer_size (GST_VAAPI_CODED_BUFFER_POOL
      (encoder)) : 0;
  if (codedbuf_size != encoder->codedbuf_size) {
    pool = gst_vaapi_coded_buffer_pool_new (encoder, encoder->codedbuf_size);
    if (!pool)
      goto error_alloc_codedbuf_pool;
    gst_vaapi_video_pool_set_capacity (pool, 5);
    gst_vaapi_video_pool_replace (&encoder->codedbuf_pool, pool);
    gst_vaapi_video_pool_unref (pool);
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_alloc_codedbuf_pool:
  {
    GST_ERROR ("failed to initialize coded buffer pool");
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_reset_context:
  {
    GST_ERROR ("failed to update VA context");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

/**
 * gst_vaapi_encoder_set_codec_state:
 * @encoder: a #GstVaapiEncoder
 * @state : a #GstVideoCodecState
 *
 * Notifies the encoder about the source surface properties. The
 * accepted set of properties is: video resolution, colorimetry,
 * pixel-aspect-ratio and framerate.
 *
 * This function is a synchronization point for codec configuration.
 * This means that, at this point, the encoder is reconfigured to
 * match the new properties and any other change beyond this point has
 * zero effect.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_codec_state (GstVaapiEncoder * encoder,
    GstVideoCodecState * state)
{
  GstVaapiEncoderStatus status;

  g_return_val_if_fail (encoder != NULL,
      GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER);
  g_return_val_if_fail (state != NULL,
      GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER);

  if (!gst_video_info_is_equal (&state->info, &encoder->video_info)) {
    status = check_video_info (encoder, &state->info);
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      return status;
    encoder->video_info = state->info;
  }
  return gst_vaapi_encoder_reconfigure_internal (encoder);
}

/* Determine the supported rate control modes */
static guint
get_rate_control_mask (GstVaapiEncoder * encoder)
{
  const GstVaapiEncoderClassData *const cdata =
      GST_VAAPI_ENCODER_GET_CLASS (encoder)->class_data;
  guint i, value, rate_control_mask = 0;

  if (encoder->got_rate_control_mask)
    return encoder->rate_control_mask;

  if (get_config_attribute (encoder, VAConfigAttribRateControl, &value)) {
    for (i = 0; i < 32; i++) {
      if (!(value & (1U << i)))
        continue;
      rate_control_mask |= 1 << to_GstVaapiRateControl (1 << i);
    }
    GST_INFO ("supported rate controls: 0x%08x", rate_control_mask);

    encoder->got_rate_control_mask = TRUE;
    encoder->rate_control_mask = cdata->rate_control_mask & rate_control_mask;
  }

  return encoder->rate_control_mask;
}

/**
 * gst_vaapi_encoder_set_rate_control:
 * @encoder: a #GstVaapiEncoder
 * @rate_control: the requested rate control
 *
 * Notifies the @encoder to use the supplied @rate_control mode.
 *
 * If the underlying encoder does not support that rate control mode,
 * then @GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_RATE_CONTROL is
 * returned.
 *
 * The rate control mode can only be specified before the first frame
 * is to be encoded. Afterwards, any change to this parameter is
 * invalid and @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED is
 * returned.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_rate_control (GstVaapiEncoder * encoder,
    GstVaapiRateControl rate_control)
{
  guint32 rate_control_mask;

  g_return_val_if_fail (encoder != NULL,
      GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER);

  if (encoder->rate_control != rate_control && encoder->num_codedbuf_queued > 0)
    goto error_operation_failed;

  rate_control_mask = get_rate_control_mask (encoder);
  if (rate_control_mask && !(rate_control_mask & (1U << rate_control)))
    goto error_unsupported_rate_control;

  encoder->rate_control = rate_control;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_operation_failed:
  {
    GST_ERROR ("could not change rate control mode after encoding started");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
error_unsupported_rate_control:
  {
    GST_ERROR ("unsupported rate control mode (%d)", rate_control);
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_RATE_CONTROL;
  }
}

/**
 * gst_vaapi_encoder_set_bitrate:
 * @encoder: a #GstVaapiEncoder
 * @bitrate: the requested bitrate (in kbps)
 *
 * Notifies the @encoder to use the supplied @bitrate value.
 *
 * Note: currently, the bitrate can only be specified before the first
 * frame is encoded. Afterwards, any change to this parameter is
 * invalid and @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED is
 * returned.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_bitrate (GstVaapiEncoder * encoder, guint bitrate)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (encoder->bitrate != bitrate && encoder->num_codedbuf_queued > 0) {
    GST_INFO ("Bitrate is changed to %d on runtime", bitrate);
    encoder->bitrate = bitrate;
    return gst_vaapi_encoder_reconfigure_internal (encoder);
  }

  encoder->bitrate = bitrate;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GstVaapiEncoderStatus
gst_vaapi_encoder_set_target_percentage (GstVaapiEncoder * encoder,
    guint target_percentage)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (encoder->target_percentage != target_percentage
      && encoder->num_codedbuf_queued > 0) {
    if (GST_VAAPI_ENCODER_RATE_CONTROL (encoder) != GST_VAAPI_RATECONTROL_CBR) {
      GST_INFO ("Target percentage is changed to %d on runtime",
          target_percentage);
      encoder->target_percentage = target_percentage;
      return gst_vaapi_encoder_reconfigure_internal (encoder);
    }
    GST_WARNING ("Target percentage is ignored for CBR rate-control");
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;
  }

  encoder->target_percentage = target_percentage;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/**
 * gst_vaapi_encoder_set_keyframe_period:
 * @encoder: a #GstVaapiEncoder
 * @keyframe_period: the maximal distance between two keyframes
 *
 * Notifies the @encoder to use the supplied @keyframe_period value.
 *
 * Note: currently, the keyframe period can only be specified before
 * the last call to gst_vaapi_encoder_set_codec_state(), which shall
 * occur before the first frame is encoded. Afterwards, any change to
 * this parameter causes gst_vaapi_encoder_set_keyframe_period() to
 * return @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_keyframe_period (GstVaapiEncoder * encoder,
    guint keyframe_period)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (encoder->keyframe_period != keyframe_period
      && encoder->num_codedbuf_queued > 0)
    goto error_operation_failed;

  encoder->keyframe_period = keyframe_period;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_operation_failed:
  {
    GST_ERROR ("could not change keyframe period after encoding started");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

/**
 * gst_vaapi_encoder_set_tuning:
 * @encoder: a #GstVaapiEncoder
 * @tuning: the #GstVaapiEncoderTune option
 *
 * Notifies the @encoder to use the supplied @tuning option.
 *
 * Note: currently, the tuning option can only be specified before the
 * last call to gst_vaapi_encoder_set_codec_state(), which shall occur
 * before the first frame is encoded. Afterwards, any change to this
 * parameter causes gst_vaapi_encoder_set_tuning() to return
 * @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_tuning (GstVaapiEncoder * encoder,
    GstVaapiEncoderTune tuning)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (encoder->tune != tuning && encoder->num_codedbuf_queued > 0)
    goto error_operation_failed;

  encoder->tune = tuning;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_operation_failed:
  {
    GST_ERROR ("could not change tuning options after encoding started");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

/**
 * gst_vaapi_encoder_set_quality_level:
 * @encoder: a #GstVaapiEncoder
 * @quality_level: the encoder quality level
 *
 * Notifies the @encoder to use the supplied @quality_level value.
 *
 * Note: currently, the quality_level can only be specified before
 * the last call to gst_vaapi_encoder_set_codec_state(), which shall
 * occur before the first frame is encoded. Afterwards, any change to
 * this parameter causes gst_vaapi_encoder_set_quality_level() to
 * return @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_quality_level (GstVaapiEncoder * encoder,
    guint quality_level)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder) != quality_level
      && encoder->num_codedbuf_queued > 0)
    goto error_operation_failed;

  GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder) = quality_level;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_operation_failed:
  {
    GST_ERROR ("could not change quality level after encoding started");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

/**
 * gst_vaapi_encoder_set_trellis:
 * @encoder: a #GstVaapiEncoder
 * @trellis: whether to use trellis quantization
 *
 * Notifies the @encoder to use the supplied @trellis option.
 *
 * Note: currently, the tuning option can only be specified before the
 * last call to gst_vaapi_encoder_set_codec_state(), which shall occur
 * before the first frame is encoded. Afterwards, any change to this
 * parameter causes gst_vaapi_encoder_set_tuning() to return
 * @GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_set_trellis (GstVaapiEncoder * encoder, gboolean trellis)
{
  g_return_val_if_fail (encoder != NULL, 0);

  if (encoder->trellis != trellis && encoder->num_codedbuf_queued > 0)
    goto error_operation_failed;

  encoder->trellis = trellis;
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_operation_failed:
  {
    GST_ERROR ("could not change trellis options after encoding started");
    return GST_VAAPI_ENCODER_STATUS_ERROR_OPERATION_FAILED;
  }
}

G_DEFINE_ABSTRACT_TYPE (GstVaapiEncoder, gst_vaapi_encoder, GST_TYPE_OBJECT);

/**
 * GstVaapiEncoderProp:
 * @ENCODER_PROP_DISPLAY: The display.
 * @ENCODER_PROP_BITRATE: Bitrate expressed in kbps (uint).
 * @ENCODER_PROP_TARGET_PERCENTAGE: Desired target percentage of
 *  bitrate for variable rate controls.
 * @ENCODER_PROP_KEYFRAME_PERIOD: The maximal distance
 *   between two keyframes (uint).
 * @ENCODER_PROP_DEFAULT_ROI_VALUE: The default delta qp to apply
 *   to each region of interest.
 * @ENCODER_PROP_TRELLIS: Use trellis quantization method (gboolean).
 *
 * The set of configurable properties for the encoder.
 */
enum
{
  ENCODER_PROP_DISPLAY = 1,
  ENCODER_PROP_BITRATE,
  ENCODER_PROP_TARGET_PERCENTAGE,
  ENCODER_PROP_KEYFRAME_PERIOD,
  ENCODER_PROP_QUALITY_LEVEL,
  ENCODER_PROP_DEFAULT_ROI_VALUE,
  ENCODER_PROP_TRELLIS,
  ENCODER_N_PROPERTIES
};

static GParamSpec *properties[ENCODER_N_PROPERTIES];

static void
gst_vaapi_encoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER (object);
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  switch (prop_id) {
    case ENCODER_PROP_DISPLAY:
      g_assert (encoder->display == NULL);
      encoder->display = g_value_dup_object (value);
      g_assert (encoder->display != NULL);
      encoder->va_display = GST_VAAPI_DISPLAY_VADISPLAY (encoder->display);
      break;
    case ENCODER_PROP_BITRATE:
      status = gst_vaapi_encoder_set_bitrate (encoder,
          g_value_get_uint (value));
      break;
    case ENCODER_PROP_TARGET_PERCENTAGE:
      status =
          gst_vaapi_encoder_set_target_percentage (encoder,
          g_value_get_uint (value));
      break;
    case ENCODER_PROP_KEYFRAME_PERIOD:
      status =
          gst_vaapi_encoder_set_keyframe_period (encoder,
          g_value_get_uint (value));
      break;
    case ENCODER_PROP_QUALITY_LEVEL:
      status =
          gst_vaapi_encoder_set_quality_level (encoder,
          g_value_get_uint (value));
      break;
    case ENCODER_PROP_DEFAULT_ROI_VALUE:
      encoder->default_roi_value = g_value_get_int (value);
      status = GST_VAAPI_ENCODER_STATUS_SUCCESS;
      break;
    case ENCODER_PROP_TRELLIS:
      status =
          gst_vaapi_encoder_set_trellis (encoder, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (status)
    GST_WARNING_OBJECT (encoder, "Failed to set the property:%s, error is %d",
        g_param_spec_get_name (pspec), status);
}

static void
gst_vaapi_encoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER (object);

  switch (prop_id) {
    case ENCODER_PROP_DISPLAY:
      g_value_set_object (value, encoder->display);
      break;
    case ENCODER_PROP_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
      break;
    case ENCODER_PROP_TARGET_PERCENTAGE:
      g_value_set_uint (value, encoder->target_percentage);
      break;
    case ENCODER_PROP_KEYFRAME_PERIOD:
      g_value_set_uint (value, encoder->keyframe_period);
      break;
    case ENCODER_PROP_QUALITY_LEVEL:
      g_value_set_uint (value, GST_VAAPI_ENCODER_QUALITY_LEVEL (encoder));
      break;
    case ENCODER_PROP_DEFAULT_ROI_VALUE:
      g_value_set_int (value, encoder->default_roi_value);
      break;
    case ENCODER_PROP_TRELLIS:
      g_value_set_boolean (value, encoder->trellis);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encoder_init (GstVaapiEncoder * encoder)
{
  encoder->va_context = VA_INVALID_ID;

  gst_video_info_init (&encoder->video_info);

  g_mutex_init (&encoder->mutex);
  g_cond_init (&encoder->surface_free);
  g_cond_init (&encoder->codedbuf_free);

  encoder->codedbuf_queue = g_async_queue_new_full ((GDestroyNotify)
      gst_vaapi_coded_buffer_proxy_unref);
}

/* Base encoder cleanup (internal) */
static void
gst_vaapi_encoder_finalize (GObject * object)
{
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER (object);

  if (encoder->context)
    gst_vaapi_context_unref (encoder->context);
  encoder->context = NULL;
  gst_vaapi_display_replace (&encoder->display, NULL);
  encoder->va_display = NULL;

  if (encoder->properties) {
    g_ptr_array_unref (encoder->properties);
    encoder->properties = NULL;
  }

  gst_vaapi_video_pool_replace (&encoder->codedbuf_pool, NULL);
  if (encoder->codedbuf_queue) {
    g_async_queue_unref (encoder->codedbuf_queue);
    encoder->codedbuf_queue = NULL;
  }
  g_cond_clear (&encoder->surface_free);
  g_cond_clear (&encoder->codedbuf_free);
  g_mutex_clear (&encoder->mutex);

  G_OBJECT_CLASS (gst_vaapi_encoder_parent_class)->finalize (object);
}

static void
gst_vaapi_encoder_class_init (GstVaapiEncoderClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_vaapi_encoder_set_property;
  object_class->get_property = gst_vaapi_encoder_get_property;
  object_class->finalize = gst_vaapi_encoder_finalize;

  /**
   * GstVaapiDecoder:display:
   *
   * #GstVaapiDisplay to be used.
   */
  properties[ENCODER_PROP_DISPLAY] =
      g_param_spec_object ("display", "Gst VA-API Display",
      "The VA-API display object to use", GST_TYPE_VAAPI_DISPLAY,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  /**
   * GstVaapiEncoder:bitrate:
   *
   * The desired bitrate, expressed in kbps.
   * This is available when rate-control is CBR or VBR.
   *
   * CBR: This applies equally to minimum, maximum and target bitrate in the driver.
   * VBR: This applies to maximum bitrate in the driver.
   *      Minimum bitrate will be calculated like the following in the driver.
   *      if (target percentage < 50) minimum bitrate = 0
   *      else minimum bitrate = maximum bitrate * (2 * target percentage -100) / 100
   *      Target bitrate will be calculated like the following in the driver.
   *      target bitrate = maximum bitrate * target percentage / 100
   */
  properties[ENCODER_PROP_BITRATE] =
      g_param_spec_uint ("bitrate",
      "Bitrate (kbps)",
      "The desired bitrate expressed in kbps (0: auto-calculate)",
      0, 2000 * 1024, 0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoder:target-percentage:
   *
   * The desired target percentage of bitrate for variable rate controls.
   */
  properties[ENCODER_PROP_TARGET_PERCENTAGE] =
      g_param_spec_uint ("target-percentage",
      "Target Percentage",
      "The desired target percentage of bitrate for variable rate "
      "controls.", 1, 100, 70,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoder:keyframe-period:
   *
   * The maximal distance between two keyframes.
   */
  properties[ENCODER_PROP_KEYFRAME_PERIOD] =
      g_param_spec_uint ("keyframe-period",
      "Keyframe Period",
      "Maximal distance between two keyframes (0: auto-calculate)", 0,
      G_MAXUINT32, 30,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoder:quality-level:
   *
   * The Encoding quality level.
   */
  properties[ENCODER_PROP_QUALITY_LEVEL] =
      g_param_spec_uint ("quality-level",
      "Quality Level", "Encoding Quality Level "
      "(lower value means higher-quality/slow-encode, "
      " higher value means lower-quality/fast-encode)",
      1, 7, 4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVapiEncoder:roi-default-delta-qp
   *
   * Default delta-qp to apply to each Region of Interest
   */
  properties[ENCODER_PROP_DEFAULT_ROI_VALUE] =
      g_param_spec_int ("default-roi-delta-qp", "Default ROI delta QP",
      "The default delta-qp to apply to each Region of Interest"
      "(lower value means higher-quality, "
      "higher value means lower-quality)",
      -10, 10, -10,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  /**
   * GstVaapiEncoder: trellis:
   *
   * The trellis quantization method the encoder can use.
   * Trellis is an improved quantization algorithm.
   *
   */
  properties[ENCODER_PROP_TRELLIS] =
      g_param_spec_boolean ("trellis",
      "Trellis Quantization",
      "The Trellis Quantization Method of Encoder",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  g_object_class_install_properties (object_class, ENCODER_N_PROPERTIES,
      properties);
}

static GstVaapiContext *
create_test_context_config (GstVaapiEncoder * encoder, GstVaapiProfile profile)
{
  GstVaapiContextInfo cip = { 0, };
  GstVaapiContext *ctxt;

  g_assert (profile != GST_VAAPI_PROFILE_UNKNOWN);

  cip.profile = profile;
  cip.entrypoint = gst_vaapi_encoder_get_entrypoint (encoder, profile);
  if (cip.entrypoint == GST_VAAPI_ENTRYPOINT_INVALID) {
    GST_INFO ("can not find %s entrypoint for profile %s to create"
        " text context. Ignore this profile",
        GST_VAAPI_ENCODER_TUNE (encoder) == GST_VAAPI_ENCODER_TUNE_LOW_POWER ?
        "the low-power" : "an available",
        gst_vaapi_profile_get_va_name (profile));
    return NULL;
  }

  init_context_info (encoder, &cip);
  ctxt = gst_vaapi_context_new (encoder->display, &cip);
  return ctxt;
}

static gboolean
get_profile_surface_attributes (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiConfigSurfaceAttributes * attribs)
{
  GstVaapiContext *ctxt = NULL;
  gboolean ret;

  g_return_val_if_fail (attribs != NULL, FALSE);
  g_return_val_if_fail (profile != GST_VAAPI_PROFILE_UNKNOWN, FALSE);

  ctxt = create_test_context_config (encoder, profile);
  if (!ctxt)
    return FALSE;

  ret = gst_vaapi_context_get_surface_attributes (ctxt, attribs);
  if (ret) {
    attribs->formats = gst_vaapi_context_get_surface_formats (ctxt);

    if (!attribs->formats)
      ret = FALSE;
  }

  gst_vaapi_context_unref (ctxt);
  return ret;
}

static gboolean
merge_profile_surface_attributes (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiConfigSurfaceAttributes * attribs)
{
  GstVaapiConfigSurfaceAttributes attr = { 0, };
  guint i, j;
  GstVideoFormat fmt, sfmt;

  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    return FALSE;

  if (!get_profile_surface_attributes (encoder, profile, &attr))
    return FALSE;

  for (i = 0; i < attr.formats->len; i++) {
    sfmt = g_array_index (attr.formats, GstVideoFormat, i);
    for (j = 0; j < attribs->formats->len; j++) {
      fmt = g_array_index (attribs->formats, GstVideoFormat, j);
      if (fmt == sfmt)
        break;
    }
    if (j >= attribs->formats->len)
      g_array_append_val (attribs->formats, sfmt);
  }

  g_array_unref (attr.formats);

  attribs->min_width = MIN (attribs->min_width, attr.min_width);
  attribs->min_height = MIN (attribs->min_height, attr.min_height);
  attribs->max_width = MAX (attribs->max_width, attr.max_width);
  attribs->max_height = MAX (attribs->max_height, attr.max_height);
  attribs->mem_types &= attr.mem_types;

  return TRUE;
}

/**
 * gst_vaapi_encoder_get_surface_attributres:
 * @encoder: a #GstVaapiEncoder instances
 * @profiles: a #GArray of #GstVaapiProfile to be test
 * @min_width (out): the minimal surface width
 * @min_height (out): the minimal surface height
 * @max_width (out): the maximal surface width
 * @max_height (out): the maximal surface height
 *
 * Fetches the valid surface's attributes for the specified @profiles
 *
 * Returns: a #GArray of valid formats we get or %NULL if failed.
 **/
GArray *
gst_vaapi_encoder_get_surface_attributes (GstVaapiEncoder * encoder,
    GArray * profiles, gint * min_width, gint * min_height,
    gint * max_width, gint * max_height, guint * mem_types)
{
  GstVaapiConfigSurfaceAttributes attribs = {
    G_MAXINT, G_MAXINT, 1, 1, G_MAXUINT, NULL
  };
  GstVaapiProfile profile;
  guint i;

  attribs.formats = g_array_new (FALSE, FALSE, sizeof (GstVideoFormat));
  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    g_assert (profile != GST_VAAPI_PROFILE_UNKNOWN);
    GST_LOG ("Detect input formats of profile %s",
        gst_vaapi_profile_get_va_name (profile));

    if (!merge_profile_surface_attributes (encoder, profile, &attribs)) {
      GST_INFO ("Can not get surface formats for profile %s",
          gst_vaapi_profile_get_va_name (profile));
      continue;
    }
  }

  if (attribs.formats->len == 0) {
    g_array_unref (attribs.formats);
    return NULL;
  }

  if (min_width)
    *min_width = attribs.min_width;
  if (min_height)
    *min_height = attribs.min_height;
  if (max_width)
    *max_width = attribs.max_width;
  if (max_height)
    *max_height = attribs.max_height;
  if (mem_types)
    *mem_types = attribs.mem_types;
  return attribs.formats;
}

/**
 * gst_vaapi_encoder_ensure_num_slices:
 * @encoder: a #GstVaapiEncoder
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 * @media_max_slices: the number of the slices permitted by the stream
 * @num_slices: (out): the possible number of slices to process
 *
 * This function will clamp the @num_slices provided by the user,
 * according the limit of the number of slices permitted by the stream
 * and by the hardware.
 *
 * We need to pass the @profile and the @entrypoint, because at the
 * moment the encoder base class, still doesn't have them assigned,
 * and this function is meant to be called by the derived classes
 * while they are configured.
 *
 * Returns: %TRUE if the number of slices is different than zero.
 **/
gboolean
gst_vaapi_encoder_ensure_num_slices (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint,
    guint media_max_slices, guint * num_slices)
{
  VAProfile va_profile;
  VAEntrypoint va_entrypoint;
  guint max_slices, num;

  va_profile = gst_vaapi_profile_get_va_profile (profile);
  va_entrypoint = gst_vaapi_entrypoint_get_va_entrypoint (entrypoint);

  if (!gst_vaapi_get_config_attribute (encoder->display, va_profile,
          va_entrypoint, VAConfigAttribEncMaxSlices, &max_slices)) {
    *num_slices = 1;
    return TRUE;
  }

  num = *num_slices;
  if (num > max_slices)
    num = max_slices;
  if (num > media_max_slices)
    num = media_max_slices;

  if (num == 0)
    return FALSE;
  *num_slices = num;
  return TRUE;
}

/**
 * gst_vaapi_encoder_ensure_max_num_ref_frames:
 * @encoder: a #GstVaapiEncoder
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 *
 * This function will query VAConfigAttribEncMaxRefFrames to get the
 * maximum number of reference frames in the driver,
 * for both the reference picture list 0 (bottom 16 bits) and
 * the reference picture list 1 (top 16 bits).
 *
 * We need to pass the @profile and the @entrypoint, because at the
 * moment the encoder base class, still doesn't have them assigned,
 * and this function is meant to be called by the derived classes
 * while they are configured.
 *
 * Returns: %TRUE if the number of reference frames is different than zero.
 **/
gboolean
gst_vaapi_encoder_ensure_max_num_ref_frames (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint)
{
  VAProfile va_profile;
  VAEntrypoint va_entrypoint;
  guint max_ref_frames;

  va_profile = gst_vaapi_profile_get_va_profile (profile);
  va_entrypoint = gst_vaapi_entrypoint_get_va_entrypoint (entrypoint);

  if (!gst_vaapi_get_config_attribute (encoder->display, va_profile,
          va_entrypoint, VAConfigAttribEncMaxRefFrames, &max_ref_frames)) {
    /* Set the default the number of reference frames */
    encoder->max_num_ref_frames_0 = 1;
    encoder->max_num_ref_frames_1 = 0;
    return TRUE;
  }

  encoder->max_num_ref_frames_0 = max_ref_frames & 0xffff;
  encoder->max_num_ref_frames_1 = (max_ref_frames >> 16) & 0xffff;

  return TRUE;
}

/**
 * gst_vaapi_encoder_ensure_tile_support:
 * @encoder: a #GstVaapiEncoder
 * @profile: a #GstVaapiProfile
 * @entrypoint: a #GstVaapiEntrypoint
 *
 * This function will query VAConfigAttribEncTileSupport to check
 * whether the encoder support tile.
 *
 * We need to pass the @profile and the @entrypoint, because at the
 * moment the encoder base class, still doesn't have them assigned,
 * and this function is meant to be called by the derived classes
 * while they are configured.
 *
 * Returns: %TRUE if supported, %FALSE if not.
 **/
gboolean
gst_vaapi_encoder_ensure_tile_support (GstVaapiEncoder * encoder,
    GstVaapiProfile profile, GstVaapiEntrypoint entrypoint)
{
  guint tile = 0;

#if VA_CHECK_VERSION(1,0,1)
  VAProfile va_profile;
  VAEntrypoint va_entrypoint;

  va_profile = gst_vaapi_profile_get_va_profile (profile);
  va_entrypoint = gst_vaapi_entrypoint_get_va_entrypoint (entrypoint);

  if (!gst_vaapi_get_config_attribute (encoder->display, va_profile,
          va_entrypoint, VAConfigAttribEncTileSupport, &tile))
    return FALSE;
#endif

  return tile > 0;
}

GstVaapiProfile
gst_vaapi_encoder_get_profile (GstVaapiEncoder * encoder)
{
  g_return_val_if_fail (encoder, GST_VAAPI_PROFILE_UNKNOWN);

  return encoder->profile;
}

/* Get the entrypoint based on the tune option. */
/**
 * gst_vaapi_encoder_get_entrypoint:
 * @encoder: a #GstVaapiEncoder
 * @profile: a #GstVaapiProfile
 *
 * This function will return the valid entrypoint of the @encoder for
 * @profile. If the low-power mode(tune option) is set, only LP
 * entrypoints will be considered. If not, the first available entry
 * point will be return.
 *
 * Returns: The #GstVaapiEntrypoint.
 **/
GstVaapiEntrypoint
gst_vaapi_encoder_get_entrypoint (GstVaapiEncoder * encoder,
    GstVaapiProfile profile)
{
  /* XXX: The profile may not be the same with encoder->profile */

  g_return_val_if_fail (encoder, GST_VAAPI_ENTRYPOINT_INVALID);
  g_return_val_if_fail (profile != GST_VAAPI_PROFILE_UNKNOWN,
      GST_VAAPI_ENTRYPOINT_INVALID);

  if (profile == GST_VAAPI_PROFILE_JPEG_BASELINE)
    return GST_VAAPI_ENTRYPOINT_PICTURE_ENCODE;

  if (GST_VAAPI_ENCODER_TUNE (encoder) == GST_VAAPI_ENCODER_TUNE_LOW_POWER) {
    if (gst_vaapi_display_has_encoder (GST_VAAPI_ENCODER_DISPLAY (encoder),
            profile, GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP))
      return GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP;
  } else {
    /* If not set, choose the available one */
    if (gst_vaapi_display_has_encoder (GST_VAAPI_ENCODER_DISPLAY (encoder),
            profile, GST_VAAPI_ENTRYPOINT_SLICE_ENCODE))
      return GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;

    if (gst_vaapi_display_has_encoder (GST_VAAPI_ENCODER_DISPLAY (encoder),
            profile, GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP))
      return GST_VAAPI_ENTRYPOINT_SLICE_ENCODE_LP;
  }

  return GST_VAAPI_ENTRYPOINT_INVALID;
}

/**
 * gst_vaapi_encoder_get_available_profiles:
 * @encoder: a #GstVaapiEncoder
 *
 * Collect all supported #GstVaapiProfile of current @encoder's #GstVaapiCodec,
 * and return them as a #GArray
 *
 * Returns: An #GArray of #GstVaapiProfile.
 **/
GArray *
gst_vaapi_encoder_get_available_profiles (GstVaapiEncoder * encoder)
{
  GstVaapiCodec codec;
  GArray *all_profiles = NULL;
  GArray *profiles = NULL;
  GstVaapiProfile profile;
  guint i;

  g_return_val_if_fail (encoder != NULL, 0);

  codec = GST_VAAPI_ENCODER_GET_CLASS (encoder)->class_data->codec;

  all_profiles = gst_vaapi_display_get_encode_profiles
      (GST_VAAPI_ENCODER_DISPLAY (encoder));
  if (!all_profiles)
    goto out;

  /* Add all supported profiles belong to current codec */
  profiles = g_array_new (FALSE, FALSE, sizeof (GstVaapiProfile));
  if (!profiles)
    goto out;

  for (i = 0; i < all_profiles->len; i++) {
    profile = g_array_index (all_profiles, GstVaapiProfile, i);
    if (gst_vaapi_profile_get_codec (profile) == codec)
      g_array_append_val (profiles, profile);
  }

out:
  if (all_profiles)
    g_array_unref (all_profiles);
  if (profiles && profiles->len == 0) {
    g_array_unref (profiles);
    profiles = NULL;
  }

  return profiles;
}

/** Returns a GType for the #GstVaapiEncoderTune set */
GType
gst_vaapi_encoder_tune_get_type (void)
{
  static gsize g_type = 0;

  static const GEnumValue encoder_tune_values[] = {
    /* *INDENT-OFF* */
    { GST_VAAPI_ENCODER_TUNE_NONE,
      "None", "none" },
    { GST_VAAPI_ENCODER_TUNE_HIGH_COMPRESSION,
      "High compression", "high-compression" },
    { GST_VAAPI_ENCODER_TUNE_LOW_LATENCY,
      "Low latency", "low-latency" },
    { GST_VAAPI_ENCODER_TUNE_LOW_POWER,
      "Low power mode", "low-power" },
    { 0, NULL, NULL },
    /* *INDENT-ON* */
  };

  if (g_once_init_enter (&g_type)) {
    GType type =
        g_enum_register_static ("GstVaapiEncoderTune", encoder_tune_values);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}

/** Returns a GType for the #GstVaapiEncoderMbbrc set */
GType
gst_vaapi_encoder_mbbrc_get_type (void)
{
  static gsize g_type = 0;

  if (g_once_init_enter (&g_type)) {
    static const GEnumValue encoder_mbbrc_values[] = {
      {GST_VAAPI_ENCODER_MBBRC_AUTO, "Auto", "auto"},
      {GST_VAAPI_ENCODER_MBBRC_ON, "On", "on"},
      {GST_VAAPI_ENCODER_MBBRC_OFF, "Off", "off"},
      {0, NULL, NULL},
    };

    GType type =
        g_enum_register_static (g_intern_static_string ("GstVaapiEncoderMbbrc"),
        encoder_mbbrc_values);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}
