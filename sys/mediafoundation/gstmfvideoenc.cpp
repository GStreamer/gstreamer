/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstmfvideoenc.h"
#include <wrl.h>

using namespace Microsoft::WRL;

GST_DEBUG_CATEGORY (gst_mf_video_enc_debug);
#define GST_CAT_DEFAULT gst_mf_video_enc_debug

#define gst_mf_video_enc_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstMFVideoEnc, gst_mf_video_enc,
    GST_TYPE_VIDEO_ENCODER,
    GST_DEBUG_CATEGORY_INIT (gst_mf_video_enc_debug, "mfvideoenc", 0,
      "mfvideoenc"));

static gboolean gst_mf_video_enc_open (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_close (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_set_format (GstVideoEncoder * enc,
    GstVideoCodecState * state);
static GstFlowReturn gst_mf_video_enc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_mf_video_enc_finish (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_flush (GstVideoEncoder * enc);

static void
gst_mf_video_enc_class_init (GstMFVideoEncClass * klass)
{
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);

  videoenc_class->open = GST_DEBUG_FUNCPTR (gst_mf_video_enc_open);
  videoenc_class->close = GST_DEBUG_FUNCPTR (gst_mf_video_enc_close);
  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_mf_video_enc_set_format);
  videoenc_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mf_video_enc_handle_frame);
  videoenc_class->finish = GST_DEBUG_FUNCPTR (gst_mf_video_enc_finish);
  videoenc_class->flush = GST_DEBUG_FUNCPTR (gst_mf_video_enc_flush);

  gst_type_mark_as_plugin_api (GST_TYPE_MF_VIDEO_ENC, (GstPluginAPIFlags) 0);
}

static void
gst_mf_video_enc_init (GstMFVideoEnc * self)
{
}

static gboolean
gst_mf_video_enc_open (GstVideoEncoder * enc)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (enc);
  GstMFTransformEnumParams enum_params = { 0, };
  MFT_REGISTER_TYPE_INFO output_type;
  gboolean ret;

  output_type.guidMajorType = MFMediaType_Video;
  output_type.guidSubtype = klass->codec_id;

  enum_params.category = MFT_CATEGORY_VIDEO_ENCODER;
  enum_params.enum_flags = klass->enum_flags;
  enum_params.output_typeinfo = &output_type;
  enum_params.device_index = klass->device_index;

  GST_DEBUG_OBJECT (self, "Create MFT with enum flags 0x%x, device index %d",
      klass->enum_flags, klass->device_index);

  self->transform = gst_mf_transform_new (&enum_params);
  ret = !!self->transform;

  if (!ret)
    GST_ERROR_OBJECT (self, "Cannot create MFT object");

  return ret;
}

static gboolean
gst_mf_video_enc_close (GstVideoEncoder * enc)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);

  gst_clear_object (&self->transform);

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  return TRUE;
}

static gboolean
gst_mf_video_enc_set_format (GstVideoEncoder * enc, GstVideoCodecState * state)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (enc);
  GstVideoInfo *info = &state->info;
  ComPtr<IMFMediaType> in_type;
  ComPtr<IMFMediaType> out_type;
  GList *input_types = NULL;
  GList *iter;
  HRESULT hr;
  gint fps_n, fps_d;

  GST_DEBUG_OBJECT (self, "Set format");

  gst_mf_video_enc_finish (enc);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  if (!gst_mf_transform_open (self->transform)) {
    GST_ERROR_OBJECT (self, "Failed to open MFT");
    return FALSE;
  }

  hr = MFCreateMediaType (out_type.GetAddressOf ());
  if (!gst_mf_result (hr))
    return FALSE;

  hr = out_type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (!gst_mf_result (hr))
    return FALSE;

  if (klass->set_option) {
    if (!klass->set_option (self, out_type.Get ())) {
      GST_ERROR_OBJECT (self, "subclass failed to set option");
      return FALSE;
    }
  }

  fps_n = GST_VIDEO_INFO_FPS_N (info);
  fps_d = GST_VIDEO_INFO_FPS_D (info);
  if (fps_n == 0 || fps_d == 0) {
    fps_n = 0;
    fps_d = 1;
  }

  hr = MFSetAttributeRatio (out_type.Get (), MF_MT_FRAME_RATE, fps_n, fps_d);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self,
        "Couldn't set framerate %d/%d, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  hr = MFSetAttributeSize (out_type.Get (), MF_MT_FRAME_SIZE,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self,
        "Couldn't set resolution %dx%d, hr: 0x%x", GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), (guint) hr);
    return FALSE;
  }

  hr = MFSetAttributeRatio (out_type.Get (), MF_MT_PIXEL_ASPECT_RATIO,
      GST_VIDEO_INFO_PAR_N (info), GST_VIDEO_INFO_PAR_D (info));
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set par %d/%d",
        GST_VIDEO_INFO_PAR_N (info), GST_VIDEO_INFO_PAR_D (info));
    return FALSE;
  }

  hr = out_type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self,
        "Couldn't set interlace mode, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  if (!gst_mf_transform_set_output_type (self->transform, out_type.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't set output type");
    return FALSE;
  }

  if (!gst_mf_transform_get_input_available_types (self->transform,
      &input_types)) {
    GST_ERROR_OBJECT (self, "Couldn't get available input types");
    return FALSE;
  }

  for (iter = input_types; iter; iter = g_list_next (iter)) {
    GstVideoFormat format;
    GUID subtype;
    IMFMediaType *type = (IMFMediaType *) iter->data;

    hr = type->GetGUID (MF_MT_SUBTYPE, &subtype);
    if (!gst_mf_result (hr))
      continue;

    format = gst_mf_video_subtype_to_video_format (&subtype);
    if (format != GST_VIDEO_INFO_FORMAT (info))
      continue;

    in_type = type;
  }

  g_list_free_full (input_types, (GDestroyNotify) gst_mf_media_type_release);

  if (!in_type) {
    GST_ERROR_OBJECT (self,
        "Couldn't convert input caps %" GST_PTR_FORMAT " to media type",
        state->caps);
    return FALSE;
  }

  hr = MFSetAttributeSize (in_type.Get (), MF_MT_FRAME_SIZE,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set frame size %dx%d",
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));
    return FALSE;
  }

  hr = in_type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self,
        "Couldn't set interlace mode, hr: 0x%x", (guint) hr);
    return FALSE;
  }

  hr = MFSetAttributeRatio (in_type.Get (), MF_MT_PIXEL_ASPECT_RATIO,
      GST_VIDEO_INFO_PAR_N (info), GST_VIDEO_INFO_PAR_D (info));
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set par %d/%d",
        GST_VIDEO_INFO_PAR_N (info), GST_VIDEO_INFO_PAR_D (info));
    return FALSE;
  }

  hr = MFSetAttributeRatio (in_type.Get (), MF_MT_FRAME_RATE, fps_n, fps_d);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set framerate ratio %d/%d", fps_n, fps_d);
    return FALSE;
  }

  hr = in_type->SetUINT32 (MF_MT_DEFAULT_STRIDE,
      GST_VIDEO_INFO_PLANE_STRIDE (info, 0));
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't set default stride");
    return FALSE;
  }

  if (!gst_mf_transform_set_input_type (self->transform, in_type.Get ())) {
    GST_ERROR_OBJECT (self, "Couldn't set input media type");
    return FALSE;
  }

  g_assert (klass->set_src_caps != NULL);
  if (!klass->set_src_caps (self, self->input_state, out_type.Get ())) {
    GST_ERROR_OBJECT (self, "subclass couldn't set src caps");
    return FALSE;
  }

  return TRUE;
}

typedef struct
{
  GstClockTime mf_pts;
} GstMFVideoEncFrameData;

static gboolean
gst_mf_video_enc_process_input (GstMFVideoEnc * self,
    GstVideoCodecFrame * frame)
{
  GstMFVideoEncClass *klass = GST_MF_VIDEO_ENC_GET_CLASS (self);
  HRESULT hr;
  ComPtr<IMFSample> sample;
  ComPtr<IMFMediaBuffer> media_buffer;
  GstVideoInfo *info = &self->input_state->info;
  gint i, j;
  BYTE *data;
  GstVideoFrame vframe;
  gboolean res = FALSE;
  gboolean unset_force_keyframe = FALSE;
  GstMFVideoEncFrameData *frame_data = NULL;

  if (!gst_video_frame_map (&vframe, info, frame->input_buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    return FALSE;
  }

  hr = MFCreateSample (sample.GetAddressOf ());
  if (!gst_mf_result (hr))
    goto done;

  hr = MFCreateMemoryBuffer (GST_VIDEO_INFO_SIZE (info),
      media_buffer.GetAddressOf ());
  if (!gst_mf_result (hr))
    goto done;

  hr = media_buffer->Lock (&data, NULL, NULL);
  if (!gst_mf_result (hr))
    goto done;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    guint8 *src, *dst;
    gint src_stride, dst_stride;
    gint width;

    src = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, i);
    dst = data + GST_VIDEO_INFO_PLANE_OFFSET (info, i);

    src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, i);
    dst_stride = GST_VIDEO_INFO_PLANE_STRIDE (info, i);

    width = GST_VIDEO_INFO_COMP_WIDTH (info, i)
        * GST_VIDEO_INFO_COMP_PSTRIDE (info, i);

    for (j = 0; j < GST_VIDEO_INFO_COMP_HEIGHT (info, i); j++) {
      memcpy (dst, src, width);
      src += src_stride;
      dst += dst_stride;
    }
  }

  media_buffer->Unlock ();

  hr = media_buffer->SetCurrentLength (GST_VIDEO_INFO_SIZE (info));
  if (!gst_mf_result (hr))
    goto done;

  hr = sample->AddBuffer (media_buffer.Get ());
  if (!gst_mf_result (hr))
    goto done;

  frame_data = g_new0 (GstMFVideoEncFrameData, 1);
  frame_data->mf_pts = frame->pts / 100;

  gst_video_codec_frame_set_user_data (frame,
      frame_data, (GDestroyNotify) g_free);

  hr = sample->SetSampleTime (frame_data->mf_pts);
  if (!gst_mf_result (hr))
    goto done;

  hr = sample->SetSampleDuration (
      GST_CLOCK_TIME_IS_VALID (frame->duration) ? frame->duration / 100 : 0);
  if (!gst_mf_result (hr))
    goto done;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    if (klass->can_force_keyframe) {
      unset_force_keyframe =
          gst_mf_transform_set_codec_api_uint32 (self->transform,
          &CODECAPI_AVEncVideoForceKeyFrame, TRUE);
    } else {
      GST_WARNING_OBJECT (self, "encoder does not support force keyframe");
    }
  }

  if (!gst_mf_transform_process_input (self->transform, sample.Get ())) {
    GST_ERROR_OBJECT (self, "Failed to process input");
    goto done;
  }

  if (unset_force_keyframe) {
    gst_mf_transform_set_codec_api_uint32 (self->transform,
        &CODECAPI_AVEncVideoForceKeyFrame, FALSE);
  }

  res = TRUE;

done:
  gst_video_frame_unmap (&vframe);

  return res;
}

static GstVideoCodecFrame *
gst_mf_video_enc_find_output_frame (GstMFVideoEnc * self, UINT64 mf_dts,
    UINT64 mf_pts)
{
  GList *l, *walk = gst_video_encoder_get_frames (GST_VIDEO_ENCODER (self));
  GstVideoCodecFrame *ret = NULL;

  for (l = walk; l; l = l->next) {
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) l->data;
    GstMFVideoEncFrameData *data = (GstMFVideoEncFrameData *)
        gst_video_codec_frame_get_user_data (frame);

    if (!data)
      continue;

    if (mf_dts == data->mf_pts) {
      ret = frame;
      break;
    }
  }

  /* find target with pts */
  if (!ret) {
    for (l = walk; l; l = l->next) {
      GstVideoCodecFrame *frame = (GstVideoCodecFrame *) l->data;
      GstMFVideoEncFrameData *data = (GstMFVideoEncFrameData *)
          gst_video_codec_frame_get_user_data (frame);

      if (!data)
        continue;

      if (mf_pts == data->mf_pts) {
        ret = frame;
        break;
      }
    }
  }

  if (ret) {
    gst_video_codec_frame_ref (ret);
  } else {
    /* just return the oldest one */
    ret = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (self));
  }

  if (walk)
    g_list_free_full (walk, (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

static GstFlowReturn
gst_mf_video_enc_process_output (GstMFVideoEnc * self)
{
  HRESULT hr;
  BYTE *data;
  ComPtr<IMFMediaBuffer> media_buffer;
  ComPtr<IMFSample> sample;
  GstBuffer *buffer;
  GstFlowReturn res = GST_FLOW_ERROR;
  GstVideoCodecFrame *frame;
  LONGLONG sample_timestamp;
  LONGLONG sample_duration;
  UINT32 keyframe = FALSE;
  UINT64 mf_dts = GST_CLOCK_TIME_NONE;
  DWORD buffer_len;

  res = gst_mf_transform_get_output (self->transform, sample.GetAddressOf ());

  if (res != GST_FLOW_OK)
    return res;

  hr = sample->GetBufferByIndex (0, media_buffer.GetAddressOf ());
  if (!gst_mf_result (hr))
    return GST_FLOW_ERROR;

  hr = media_buffer->Lock (&data, NULL, &buffer_len);
  if (!gst_mf_result (hr))
    return GST_FLOW_ERROR;

  buffer = gst_buffer_new_allocate (NULL, buffer_len, NULL);
  gst_buffer_fill (buffer, 0, data, buffer_len);
  media_buffer->Unlock ();

  sample->GetSampleTime (&sample_timestamp);
  sample->GetSampleDuration (&sample_duration);
  sample->GetUINT32 (MFSampleExtension_CleanPoint, &keyframe);

  hr = sample->GetUINT64 (MFSampleExtension_DecodeTimestamp, &mf_dts);
  if (FAILED (hr))
    mf_dts = sample_timestamp;

  frame = gst_mf_video_enc_find_output_frame (self,
      mf_dts, (UINT64) sample_timestamp);

  if (frame) {
    if (keyframe) {
      GST_DEBUG_OBJECT (self, "Keyframe pts %" GST_TIME_FORMAT,
          GST_TIME_ARGS (frame->pts));
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    frame->pts = sample_timestamp * 100;
    frame->dts = mf_dts * 100;
    frame->duration = sample_duration * 100;
    frame->output_buffer = buffer;

    res = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (self), frame);
  } else {
    GST_BUFFER_DTS (buffer) = mf_dts * 100;
    GST_BUFFER_PTS (buffer) = sample_timestamp * 100;
    GST_BUFFER_DURATION (buffer) = sample_duration * 100;

    if (keyframe) {
      GST_DEBUG_OBJECT (self, "Keyframe pts %" GST_TIME_FORMAT,
          GST_BUFFER_PTS (buffer));
      GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    res = gst_pad_push (GST_VIDEO_ENCODER_SRC_PAD (self), buffer);
  }

  return res;
}

static GstFlowReturn
gst_mf_video_enc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);
  GstFlowReturn ret = GST_FLOW_OK;

  if (!gst_mf_video_enc_process_input (self, frame)) {
    GST_ERROR_OBJECT (self, "Failed to process input");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  do {
    ret = gst_mf_video_enc_process_output (self);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_MF_TRANSFORM_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

done:
  gst_video_codec_frame_unref (frame);

  return ret;
}

static GstFlowReturn
gst_mf_video_enc_finish (GstVideoEncoder * enc)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);
  GstFlowReturn ret = GST_FLOW_OK;

  if (!self->transform)
    return GST_FLOW_OK;

  gst_mf_transform_drain (self->transform);

  do {
    ret = gst_mf_video_enc_process_output (self);
  } while (ret == GST_FLOW_OK);

  if (ret == GST_MF_TRANSFORM_FLOW_NEED_DATA)
    ret = GST_FLOW_OK;

  return ret;
}

static gboolean
gst_mf_video_enc_flush (GstVideoEncoder * enc)
{
  GstMFVideoEnc *self = GST_MF_VIDEO_ENC (enc);

  if (!self->transform)
    return TRUE;

  gst_mf_transform_flush (self->transform);

  return TRUE;
}
