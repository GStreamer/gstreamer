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
#include "gstmfvideobuffer.h"
#include <string.h>

using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_mf_video_enc_debug);
#define GST_CAT_DEFAULT gst_mf_video_enc_debug

G_END_DECLS

#define gst_mf_video_enc_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstMFVideoEnc, gst_mf_video_enc,
    GST_TYPE_VIDEO_ENCODER);

static gboolean gst_mf_video_enc_open (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_close (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_set_format (GstVideoEncoder * enc,
    GstVideoCodecState * state);
static GstFlowReturn gst_mf_video_enc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_mf_video_enc_finish (GstVideoEncoder * enc);
static gboolean gst_mf_video_enc_flush (GstVideoEncoder * enc);

static HRESULT gst_mf_video_on_new_sample (GstMFTransform * object,
    IMFSample * sample, GstMFVideoEnc * self);

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

  /* In case of hardware MFT, it will be running on async mode.
   * And new output sample callback will be called from Media Foundation's
   * internal worker queue thread */
  if (self->transform &&
      (enum_params.enum_flags & MFT_ENUM_FLAG_HARDWARE) ==
          MFT_ENUM_FLAG_HARDWARE) {
    self->async_mft = TRUE;
    gst_mf_transform_set_new_sample_callback (self->transform,
        (GstMFTransformNewSampleCallback) gst_mf_video_on_new_sample,
        self);
  } else {
    self->async_mft = FALSE;
  }

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

static void
gst_mf_video_buffer_free (GstVideoFrame * frame)
{
  if (!frame)
    return;

  gst_video_frame_unmap (frame);
  g_free (frame);
}

static gboolean
gst_mf_video_enc_frame_needs_copy (GstVideoFrame * vframe)
{
  /* Single plane data can be used without copy */
  if (GST_VIDEO_FRAME_N_PLANES (vframe) == 1)
    return FALSE;

  switch (GST_VIDEO_FRAME_FORMAT (vframe)) {
    case GST_VIDEO_FORMAT_I420:
    {
      guint8 *data, *other_data;
      guint size;

      /* Unexpected stride size, Media Foundation doesn't provide API for
       * per plane stride information */
      if (GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) !=
          2 * GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1) ||
          GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1) !=
          GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 2)) {
        return TRUE;
      }

      size = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) *
          GST_VIDEO_FRAME_HEIGHT (vframe);
      if (size + GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 0) !=
          GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 1))
        return TRUE;

      data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
      other_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 1);
      if (data + size != other_data)
        return TRUE;

      size = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1) *
          GST_VIDEO_FRAME_COMP_HEIGHT (vframe, 1);
      if (size + GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 1) !=
          GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 2))
        return TRUE;

      data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 1);
      other_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 2);
      if (data + size != other_data)
        return TRUE;

      return FALSE;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P016_LE:
    {
      guint8 *data, *other_data;
      guint size;

      /* Unexpected stride size, Media Foundation doesn't provide API for
       * per plane stride information */
      if (GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) !=
          GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 1)) {
        return TRUE;
      }

      size = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, 0) *
          GST_VIDEO_FRAME_HEIGHT (vframe);

      /* Unexpected padding */
      if (size + GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 0) !=
          GST_VIDEO_FRAME_PLANE_OFFSET (vframe, 1))
        return TRUE;

      data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 0);
      other_data = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 1);
      if (data + size != other_data)
        return TRUE;

      return FALSE;
    }
    default:
      g_assert_not_reached ();
      return TRUE;
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
  ComPtr<IGstMFVideoBuffer> video_buffer;
  GstVideoInfo *info = &self->input_state->info;
  gint i, j;
  GstVideoFrame *vframe = NULL;
  gboolean unset_force_keyframe = FALSE;
  GstMFVideoEncFrameData *frame_data = NULL;
  BYTE *data = NULL;
  gboolean need_copy;
  gboolean res = FALSE;

  vframe = g_new0 (GstVideoFrame, 1);

  if (!gst_video_frame_map (vframe, info, frame->input_buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input frame");
    g_free (vframe);
    return FALSE;
  }

  hr = MFCreateSample (&sample);
  if (!gst_mf_result (hr))
    goto error;

  /* Check if we can forward this memory to Media Foundation without copy */
  need_copy = gst_mf_video_enc_frame_needs_copy (vframe);
  if (need_copy) {
    GST_TRACE_OBJECT (self, "Copy input buffer into Media Foundation memory");
    hr = MFCreateMemoryBuffer (GST_VIDEO_INFO_SIZE (info), &media_buffer);
  } else {
    GST_TRACE_OBJECT (self, "Can use input buffer without copy");
    hr = IGstMFVideoBuffer::CreateInstanceWrapped (&vframe->info,
      (BYTE *) GST_VIDEO_FRAME_PLANE_DATA (vframe, 0),
      GST_VIDEO_INFO_SIZE (&vframe->info), &media_buffer);
  }

  if (!gst_mf_result (hr))
    goto error;

  if (!need_copy) {
    hr = media_buffer.As (&video_buffer);
    if (!gst_mf_result (hr))
      goto error;
  } else {
    hr = media_buffer->Lock (&data, NULL, NULL);
    if (!gst_mf_result (hr))
      goto error;

    for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
      guint8 *src, *dst;
      gint src_stride, dst_stride;
      gint width;

      src = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (vframe, i);
      dst = data + GST_VIDEO_INFO_PLANE_OFFSET (info, i);

      src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (vframe, i);
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
  }

  hr = media_buffer->SetCurrentLength (GST_VIDEO_INFO_SIZE (info));
  if (!gst_mf_result (hr))
    goto error;

  hr = sample->AddBuffer (media_buffer.Get ());
  if (!gst_mf_result (hr))
    goto error;

  frame_data = g_new0 (GstMFVideoEncFrameData, 1);
  frame_data->mf_pts = frame->pts / 100;

  gst_video_codec_frame_set_user_data (frame,
      frame_data, (GDestroyNotify) g_free);

  hr = sample->SetSampleTime (frame_data->mf_pts);
  if (!gst_mf_result (hr))
    goto error;

  hr = sample->SetSampleDuration (
      GST_CLOCK_TIME_IS_VALID (frame->duration) ? frame->duration / 100 : 0);
  if (!gst_mf_result (hr))
    goto error;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame)) {
    if (klass->device_caps.force_keyframe) {
      unset_force_keyframe =
          gst_mf_transform_set_codec_api_uint32 (self->transform,
          &CODECAPI_AVEncVideoForceKeyFrame, TRUE);
    } else {
      GST_WARNING_OBJECT (self, "encoder does not support force keyframe");
    }
  }

  if (!need_copy) {
    /* IGstMFVideoBuffer will hold GstVideoFrame (+ GstBuffer), then it will be
     * cleared when it's no more referenced by Media Foundation internals */
    hr = video_buffer->SetUserData ((gpointer) vframe,
        (GDestroyNotify) gst_mf_video_buffer_free);
    if (!gst_mf_result (hr))
      goto error;
  } else {
    gst_video_frame_unmap (vframe);
    g_free (vframe);
    vframe = NULL;
  }

  /* Unlock temporary so that we can output frame from Media Foundation's
   * worker thread.
   * While we are processing input, MFT might notify
   * METransformHaveOutput event from Media Foundation's internal worker queue
   * thread. Then we will output encoded data from the thread synchroniously,
   * not from streaming (this) thread */
  if (self->async_mft)
    GST_VIDEO_ENCODER_STREAM_UNLOCK (self);
  res = gst_mf_transform_process_input (self->transform, sample.Get ());
  if (self->async_mft)
      GST_VIDEO_ENCODER_STREAM_LOCK (self);

  if (unset_force_keyframe) {
    gst_mf_transform_set_codec_api_uint32 (self->transform,
        &CODECAPI_AVEncVideoForceKeyFrame, FALSE);
  }

  if (!res) {
    GST_ERROR_OBJECT (self, "Failed to process input");
    goto error;
  }

  return TRUE;

error:
  if (vframe) {
    gst_video_frame_unmap (vframe);
    g_free (vframe);
  }

  return FALSE;
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

static HRESULT
gst_mf_video_enc_finish_sample (GstMFVideoEnc * self, IMFSample * sample)
{
  HRESULT hr = S_OK;
  BYTE *data;
  ComPtr<IMFMediaBuffer> media_buffer;
  GstBuffer *buffer;
  GstFlowReturn res = GST_FLOW_ERROR;
  GstVideoCodecFrame *frame;
  LONGLONG sample_timestamp;
  LONGLONG sample_duration;
  UINT32 keyframe = FALSE;
  UINT64 mf_dts = GST_CLOCK_TIME_NONE;
  DWORD buffer_len;

  hr = sample->GetBufferByIndex (0, media_buffer.GetAddressOf ());
  if (!gst_mf_result (hr))
    goto done;

  hr = media_buffer->Lock (&data, NULL, &buffer_len);
  if (!gst_mf_result (hr))
    goto done;

  buffer = gst_buffer_new_allocate (NULL, buffer_len, NULL);
  gst_buffer_fill (buffer, 0, data, buffer_len);
  media_buffer->Unlock ();

  sample->GetSampleTime (&sample_timestamp);
  sample->GetSampleDuration (&sample_duration);
  sample->GetUINT32 (MFSampleExtension_CleanPoint, &keyframe);

  hr = sample->GetUINT64 (MFSampleExtension_DecodeTimestamp, &mf_dts);
  if (FAILED (hr)) {
    mf_dts = sample_timestamp;
    hr = S_OK;
  }

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

done:
  self->last_ret = res;

  return hr;
}

static GstFlowReturn
gst_mf_video_enc_process_output (GstMFVideoEnc * self)
{
  ComPtr<IMFSample> sample;
  GstFlowReturn res = GST_FLOW_ERROR;

  res = gst_mf_transform_get_output (self->transform, &sample);

  if (res != GST_FLOW_OK)
    return res;

  gst_mf_video_enc_finish_sample (self, sample.Get ());

  return self->last_ret;
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

  /* Don't call process_output for async (hardware) MFT. We will output
   * encoded data from gst_mf_video_on_new_sample() callback which is called
   * from Media Foundation's internal worker queue thread */
  if (!self->async_mft) {
    do {
      ret = gst_mf_video_enc_process_output (self);
    } while (ret == GST_FLOW_OK);
  }

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

  /* Unlock temporary so that we can output frame from Media Foundation's
   * worker thread */
  if (self->async_mft)
    GST_VIDEO_ENCODER_STREAM_UNLOCK (enc);

  gst_mf_transform_drain (self->transform);

  if (self->async_mft)
    GST_VIDEO_ENCODER_STREAM_LOCK (enc);

  if (!self->async_mft) {
    do {
      ret = gst_mf_video_enc_process_output (self);
    } while (ret == GST_FLOW_OK);
  }

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

  /* Unlock while flushing, while flushing, new sample callback might happen */
  if (self->async_mft)
    GST_VIDEO_ENCODER_STREAM_UNLOCK (enc);

  gst_mf_transform_flush (self->transform);

  if (self->async_mft)
    GST_VIDEO_ENCODER_STREAM_LOCK (enc);

  return TRUE;
}
static HRESULT
gst_mf_video_on_new_sample (GstMFTransform * object,
    IMFSample * sample, GstMFVideoEnc * self)
{
  GST_LOG_OBJECT (self, "New Sample callback");

  /* NOTE: this callback will be called from Media Foundation's internal
   * worker queue thread */
  GST_VIDEO_ENCODER_STREAM_LOCK (self);
  gst_mf_video_enc_finish_sample (self, sample);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (self);

  return S_OK;
}

typedef struct
{
  guint profile;
  const gchar *profile_str;
} GstMFVideoEncProfileMap;

static void
gst_mf_video_enc_enum_internal (GstMFTransform * transform, GUID &subtype,
    GstMFVideoEncDeviceCaps * device_caps, GstCaps ** sink_template,
    GstCaps ** src_template)
{
  HRESULT hr;
  MFT_REGISTER_TYPE_INFO *infos;
  UINT32 info_size;
  gint i;
  GstCaps *src_caps = NULL;
  GstCaps *sink_caps = NULL;
  GValue *supported_formats = NULL;
  GValue *profiles = NULL;
  gboolean have_I420 = FALSE;
  gchar *device_name = NULL;
  IMFActivate *activate;
  IMFTransform *encoder;
  ICodecAPI *codec_api;
  ComPtr<IMFMediaType> out_type;
  GstMFVideoEncProfileMap h264_profile_map[] = {
    { eAVEncH264VProfile_High, "high" },
    { eAVEncH264VProfile_Main, "main" },
    { eAVEncH264VProfile_Base, "baseline" },
    { 0, NULL },
  };
  GstMFVideoEncProfileMap hevc_profile_map[] = {
    { eAVEncH265VProfile_Main_420_8, "main" },
    { eAVEncH265VProfile_Main_420_10, "main-10" },
    { 0, NULL },
  };
  GstMFVideoEncProfileMap *profile_to_check = NULL;
  static gchar *h264_caps_str =
      "video/x-h264, stream-format=(string) byte-stream, alignment=(string) au";
  static gchar *hevc_caps_str =
      "video/x-h265, stream-format=(string) byte-stream, alignment=(string) au";
  static gchar *vp9_caps_str = "video/x-vp9";
  static gchar *codec_caps_str = NULL;

  /* NOTE: depending on environment,
   * some enumerated h/w MFT might not be usable (e.g., multiple GPU case) */
  if (!gst_mf_transform_open (transform))
    return;

  activate = gst_mf_transform_get_activate_handle (transform);
  if (!activate) {
    GST_WARNING_OBJECT (transform, "No IMFActivate interface available");
    return;
  }

  encoder = gst_mf_transform_get_transform_handle (transform);
  if (!encoder) {
    GST_WARNING_OBJECT (transform, "No IMFTransform interface available");
    return;
  }

  codec_api = gst_mf_transform_get_codec_api_handle (transform);
  if (!codec_api) {
    GST_WARNING_OBJECT (transform, "No ICodecAPI interface available");
    return;
  }

  g_object_get (transform, "device-name", &device_name, NULL);
  if (!device_name) {
    GST_WARNING_OBJECT (transform, "Unknown device name");
    return;
  }
  g_free (device_name);

  hr = activate->GetAllocatedBlob (MFT_INPUT_TYPES_Attributes,
      (UINT8 **) & infos, &info_size);
  if (!gst_mf_result (hr))
    return;

  for (i = 0; i < info_size / sizeof (MFT_REGISTER_TYPE_INFO); i++) {
    GstVideoFormat format;
    GValue val = G_VALUE_INIT;

    format = gst_mf_video_subtype_to_video_format (&infos[i].guidSubtype);
    if (format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

    if (!supported_formats) {
      supported_formats = g_new0 (GValue, 1);
      g_value_init (supported_formats, GST_TYPE_LIST);
    }

    /* media foundation has duplicated formats IYUV and I420 */
    if (format == GST_VIDEO_FORMAT_I420) {
      if (have_I420)
        continue;

      have_I420 = TRUE;
    }

    g_value_init (&val, G_TYPE_STRING);
    g_value_set_static_string (&val, gst_video_format_to_string (format));
    gst_value_list_append_and_take_value (supported_formats, &val);
  }
  CoTaskMemFree (infos);

  if (!supported_formats) {
    GST_WARNING_OBJECT (transform, "Couldn't figure out supported format");
    return;
  }

  if (IsEqualGUID (MFVideoFormat_H264, subtype)) {
    profile_to_check = h264_profile_map;
    codec_caps_str = h264_caps_str;
  } else if (IsEqualGUID (MFVideoFormat_HEVC, subtype)) {
    profile_to_check = hevc_profile_map;
    codec_caps_str = hevc_caps_str;
  } else if (IsEqualGUID (MFVideoFormat_VP90, subtype)) {
    codec_caps_str = vp9_caps_str;
  } else {
    g_assert_not_reached ();
    return;
  }

  if (profile_to_check) {
    hr = MFCreateMediaType (&out_type);
    if (!gst_mf_result (hr))
      return;

    hr = out_type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (!gst_mf_result (hr))
      return;

    hr = out_type->SetGUID (MF_MT_SUBTYPE, subtype);
    if (!gst_mf_result (hr))
      return;

    hr = out_type->SetUINT32 (MF_MT_AVG_BITRATE, 2048000);
    if (!gst_mf_result (hr))
      return;

    hr = MFSetAttributeRatio (out_type.Get (), MF_MT_FRAME_RATE, 30, 1);
    if (!gst_mf_result (hr))
      return;

    hr = out_type->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (!gst_mf_result (hr))
      return;

    hr = MFSetAttributeSize (out_type.Get (), MF_MT_FRAME_SIZE, 1920, 1080);
    if (!gst_mf_result (hr))
      return;

    i = 0;
    do {
      GValue profile_val = G_VALUE_INIT;
      guint mf_profile = profile_to_check[i].profile;
      const gchar *profile_str = profile_to_check[i].profile_str;

      i++;

      if (mf_profile == 0)
        break;

      g_assert (profile_str != NULL);

      hr = out_type->SetUINT32 (MF_MT_MPEG2_PROFILE, mf_profile);
      if (!gst_mf_result (hr))
        return;

      if (!gst_mf_transform_set_output_type (transform, out_type.Get ()))
        continue;

      if (!profiles) {
        profiles = g_new0 (GValue, 1);
        g_value_init (profiles, GST_TYPE_LIST);
      }

      g_value_init (&profile_val, G_TYPE_STRING);
      g_value_set_static_string (&profile_val, profile_str);
      gst_value_list_append_and_take_value (profiles, &profile_val);
    } while (1);

    if (!profiles) {
      GST_WARNING_OBJECT (transform, "Couldn't query supported profile");
      return;
    }
  }

  src_caps = gst_caps_from_string (codec_caps_str);
  if (profiles) {
    gst_caps_set_value (src_caps, "profile", profiles);
    g_value_unset (profiles);
    g_free (profiles);
  }

  sink_caps = gst_caps_new_empty_simple ("video/x-raw");
  gst_caps_set_value (sink_caps, "format", supported_formats);
  g_value_unset (supported_formats);
  g_free (supported_formats);

  /* FIXME: don't hardcode max resolution, but MF doesn't provide
   * API for querying supported max resolution... */
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, 8192,
      "height", GST_TYPE_INT_RANGE, 64, 8192, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, 8192,
      "height", GST_TYPE_INT_RANGE, 64, 8192, NULL);

  *sink_template = sink_caps;
  *src_template = src_caps;

#define CHECK_DEVICE_CAPS(codec_obj,api,val) \
  if (SUCCEEDED((codec_obj)->IsSupported(&(api)))) {\
    (device_caps)->val = TRUE; \
  }

  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonRateControlMode, rc_mode);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonQuality, quality);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncAdaptiveMode, adaptive_mode);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonBufferSize, buffer_size);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncCommonMaxBitRate, max_bitrate);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncCommonQualityVsSpeed, quality_vs_speed);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264CABACEnable, cabac);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264SPSID, sps_id);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncH264PPSID, pps_id);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncMPVDefaultBPictureCount, bframes);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncMPVGOPSize, gop_size);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncNumWorkerThreads, threads);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoContentType, content_type);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoEncodeQP, qp);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncVideoForceKeyFrame, force_keyframe);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVLowLatencyMode, low_latency);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMinQP, min_qp);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMaxQP, max_qp);
  CHECK_DEVICE_CAPS (codec_api,
      CODECAPI_AVEncVideoEncodeFrameTypeQP, frame_type_qp);
  CHECK_DEVICE_CAPS (codec_api, CODECAPI_AVEncVideoMaxNumRefFrame, max_num_ref);
  if (device_caps->max_num_ref) {
    VARIANT min;
    VARIANT max;
    VARIANT step;

    hr = codec_api->GetParameterRange (&CODECAPI_AVEncVideoMaxNumRefFrame,
        &min, &max, &step);
    if (SUCCEEDED (hr)) {
      device_caps->max_num_ref_high = max.uiVal;
      device_caps->max_num_ref_low = min.uiVal;
      VariantClear (&min);
      VariantClear (&max);
      VariantClear (&step);
    } else {
      device_caps->max_num_ref = FALSE;
    }
  }
#undef CHECK_DEVICE_CAPS

  return;
}

static GstMFTransform *
gst_mf_video_enc_enum (guint enum_flags, GUID * subtype, guint device_index,
    GstMFVideoEncDeviceCaps * device_caps, GstCaps ** sink_template,
    GstCaps ** src_template)
{
  GstMFTransformEnumParams enum_params = { 0, };
  MFT_REGISTER_TYPE_INFO output_type;
  GstMFTransform *transform;

  *sink_template = NULL;
  *src_template = NULL;
  memset (device_caps, 0, sizeof (GstMFVideoEncDeviceCaps));

  if (!IsEqualGUID (MFVideoFormat_H264, *subtype) &&
      !IsEqualGUID (MFVideoFormat_HEVC, *subtype) &&
      !IsEqualGUID (MFVideoFormat_VP90, *subtype)) {
    GST_ERROR ("Unknown subtype GUID");

    return NULL;
  }

  output_type.guidMajorType = MFMediaType_Video;
  output_type.guidSubtype = *subtype;

  enum_params.category = MFT_CATEGORY_VIDEO_ENCODER;
  enum_params.output_typeinfo = &output_type;
  enum_params.device_index = device_index;
  enum_params.enum_flags = enum_flags;

  transform = gst_mf_transform_new (&enum_params);
  if (!transform)
    return NULL;

  gst_mf_video_enc_enum_internal (transform, output_type.guidSubtype,
      device_caps, sink_template, src_template);

  return transform;
}

static void
gst_mf_video_enc_register_internal (GstPlugin * plugin, guint rank,
    GUID * subtype, GTypeInfo * type_info,
    const GstMFVideoEncDeviceCaps * device_caps,
    guint32 enum_flags, guint device_index, GstMFTransform * transform,
    GstCaps * sink_caps, GstCaps * src_caps)
{
  GType type;
  GTypeInfo local_type_info;
  gchar *type_name;
  gchar *feature_name;
  gint i;
  GstMFVideoEncClassData *cdata;
  gboolean is_default = TRUE;
  gchar *device_name = NULL;
  static gchar *type_name_prefix = NULL;
  static gchar *feature_name_prefix = NULL;

  if (IsEqualGUID (MFVideoFormat_H264, *subtype)) {
    type_name_prefix = "H264";
    feature_name_prefix = "h264";
  } else if (IsEqualGUID (MFVideoFormat_HEVC, *subtype)) {
    type_name_prefix = "H265";
    feature_name_prefix = "h265";
  } else if (IsEqualGUID (MFVideoFormat_VP90, *subtype)) {
    type_name_prefix = "VP9";
    feature_name_prefix = "vp9";
  } else {
    g_assert_not_reached ();
    return;
  }

  /* Must be checked already */
  g_object_get (transform, "device-name", &device_name, NULL);
  g_assert (device_name != NULL);

  cdata = g_new0 (GstMFVideoEncClassData, 1);
  cdata->sink_caps = gst_caps_copy (sink_caps);
  cdata->src_caps = gst_caps_copy (src_caps);
  cdata->device_name = device_name;
  cdata->device_caps = *device_caps;
  cdata->enum_flags = enum_flags;
  cdata->device_index = device_index;

  local_type_info = *type_info;
  local_type_info.class_data = cdata;

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  type_name = g_strdup_printf ("GstMF%sEnc", type_name_prefix);
  feature_name = g_strdup_printf ("mf%senc", feature_name_prefix);

  i = 1;
  while (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstMF%sDevice%dEnc", type_name_prefix, i);
    feature_name = g_strdup_printf ("mf%sdevice%denc", feature_name_prefix, i);
    is_default = FALSE;
    i++;
  }

  cdata->is_default = is_default;

  type =
      g_type_register_static (GST_TYPE_MF_VIDEO_ENC, type_name,
      &local_type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && !is_default)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}

void
gst_mf_video_enc_register (GstPlugin * plugin, guint rank, GUID * subtype,
    GTypeInfo * type_info)
{
  GstMFTransform *transform = NULL;
  GstCaps *sink_template = NULL;
  GstCaps *src_template = NULL;
  guint enum_flags;
  GstMFVideoEncDeviceCaps device_caps;
  guint i;

  /* register hardware encoders first */
  enum_flags = (MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_ASYNCMFT |
      MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SORTANDFILTER_APPROVED_ONLY);

  /* AMD seems to be able to support up to 12 GPUs */
  for (i = 0; i < 12 ; i++) {
    transform = gst_mf_video_enc_enum (enum_flags, subtype, i, &device_caps,
        &sink_template, &src_template);

    /* No more MFT to enumerate */
    if (!transform)
      break;

    /* Failed to open MFT */
    if (!sink_template) {
      gst_clear_object (&transform);
      continue;
    }

    gst_mf_video_enc_register_internal (plugin, rank, subtype,
        type_info, &device_caps, enum_flags, i, transform,
        sink_template, src_template);
    gst_clear_object (&transform);
    gst_clear_caps (&sink_template);
    gst_clear_caps (&src_template);
  }

  /* register software encoders */
  enum_flags = (MFT_ENUM_FLAG_SYNCMFT |
      MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_SORTANDFILTER_APPROVED_ONLY);

  transform = gst_mf_video_enc_enum (enum_flags, subtype, 0, &device_caps,
      &sink_template, &src_template);

  if (!transform)
    goto done;

  if (!sink_template)
    goto done;

  gst_mf_video_enc_register_internal (plugin, rank, subtype, type_info,
      &device_caps, enum_flags, i, transform, sink_template, src_template);

done:
  gst_clear_object (&transform);
  gst_clear_caps (&sink_template);
  gst_clear_caps (&src_template);
}