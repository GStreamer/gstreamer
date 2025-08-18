/* GStreamer
 *  Copyright (C) <2024> V-Nova International Limited
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gst/codecparsers/gstlcevcmeta.h>

#include "gstlcevcdecutils.h"
#include "gstlcevcdec.h"

enum
{
  PROP_0,
  PROP_VERBOSE,
  PROP_MAX_WIDTH,
  PROP_MAX_HEIGHT,
  PROP_MAX_LATENCY
};

#define DEFAULT_MAX_WIDTH 3840
#define DEFAULT_MAX_HEIGHT 2160
#define DEFAULT_MAX_LATENCY 0

#define GST_CAT_DEFAULT gst_lcevc_dec_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        (GST_LCEVC_DEC_UTILS_SUPPORTED_FORMATS))
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        (GST_LCEVC_DEC_UTILS_SUPPORTED_FORMATS))
    );

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_lcevc_dec_debug, \
    "lcevcdec", 0, "LCEVC Decoder element");

G_DEFINE_TYPE_WITH_CODE (GstLcevcDec, gst_lcevc_dec, GST_TYPE_VIDEO_DECODER,
    DEBUG_INIT);
GST_ELEMENT_REGISTER_DEFINE (lcevcdec, "lcevcdec",
    GST_RANK_MARGINAL, GST_TYPE_LCEVC_DEC);

#define GST_LCEVC_DEC_PICTURE_DATA gst_lcevc_dec_picture_data ()

static GQuark
gst_lcevc_dec_picture_data (void)
{
  static GQuark quark = 0;

  if (quark == 0)
    quark = g_quark_from_string ("GstLcevcDecPictureData");

  return quark;
}

typedef struct
{
  LCEVC_DecoderHandle decoder_handle;
  LCEVC_PictureHandle picture_handle;
  guint width;
  guint height;
} PictureData;

static PictureData *
picture_data_new (LCEVC_DecoderHandle decoder_handle, GstVideoFrame * frame)
{
  PictureData *ret = g_new0 (PictureData, 1);

  ret->decoder_handle = decoder_handle;
  ret->width = GST_VIDEO_FRAME_WIDTH (frame);
  ret->height = GST_VIDEO_FRAME_HEIGHT (frame);

  /* Alloc LCEVC picture handle */
  if (!gst_lcevc_dec_utils_alloc_picture_handle (decoder_handle, frame,
          &ret->picture_handle, LCEVC_Access_Write)) {
    g_free (ret);
    return NULL;
  }

  return ret;
}

static void
picture_data_free (gpointer p)
{
  PictureData *data = p;
  LCEVC_FreePicture (data->decoder_handle, data->picture_handle);
  g_free (data);
}

static void
gst_lcevc_dec_init (GstLcevcDec * lcevc)
{
  lcevc->max_width = DEFAULT_MAX_WIDTH;
  lcevc->max_height = DEFAULT_MAX_HEIGHT;
  lcevc->max_latency = DEFAULT_MAX_LATENCY;
}

static void
gst_lcevc_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (object);

  switch (prop_id) {
    case PROP_VERBOSE:
      lcevc->verbose = g_value_get_boolean (value);
      break;
    case PROP_MAX_WIDTH:
      lcevc->max_width = g_value_get_int (value);
      break;
    case PROP_MAX_HEIGHT:
      lcevc->max_height = g_value_get_int (value);
      break;
    case PROP_MAX_LATENCY:
      lcevc->max_latency = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lcevc_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (object);

  switch (prop_id) {
    case PROP_VERBOSE:
      g_value_set_boolean (value, lcevc->verbose);
      break;
    case PROP_MAX_WIDTH:
      g_value_set_int (value, lcevc->max_width);
      break;
    case PROP_MAX_HEIGHT:
      g_value_set_int (value, lcevc->max_height);
      break;
    case PROP_MAX_LATENCY:
      g_value_set_int (value, lcevc->max_latency);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
event_callback (LCEVC_DecoderHandle dec, LCEVC_Event event,
    LCEVC_PictureHandle pic, const LCEVC_DecodeInformation * info,
    const uint8_t * data, uint32_t size, void *user_data)
{
  GstLcevcDec *lcevc = user_data;

  switch (event) {
    case LCEVC_Log:
      GST_DEBUG_OBJECT (lcevc, "LCEVC Log");
      break;
    case LCEVC_Exit:
      GST_DEBUG_OBJECT (lcevc, "LCEVC Exit");
      break;
    case LCEVC_CanSendBase:
      GST_DEBUG_OBJECT (lcevc, "LCEVC CanSendBase");
      break;
    case LCEVC_CanSendEnhancement:
      GST_DEBUG_OBJECT (lcevc, "LCEVC CanSendEnhancement");
      break;
    case LCEVC_CanSendPicture:
      GST_DEBUG_OBJECT (lcevc, "LCEVC CanSendPicure");
      break;
    case LCEVC_CanReceive:
      GST_DEBUG_OBJECT (lcevc, "LCEVC CanReceive");
      break;
    case LCEVC_BasePictureDone:
      GST_DEBUG_OBJECT (lcevc, "LCEVC Base Picure Done");
      break;
    case LCEVC_OutputPictureDone:
      GST_DEBUG_OBJECT (lcevc, "LCEVC Output Picure Done");
      break;
    default:
      break;
  }
}

static gboolean
initialize_lcevc_decoder (GstLcevcDec * lcevc)
{
  LCEVC_AccelContextHandle accel_context = { 0, };
  int32_t events[] = { LCEVC_Log, LCEVC_Exit, LCEVC_CanSendBase,
    LCEVC_CanSendEnhancement, LCEVC_CanSendPicture, LCEVC_CanReceive,
    LCEVC_BasePictureDone, LCEVC_OutputPictureDone
  };

  if (LCEVC_CreateDecoder (&lcevc->decoder_handle, accel_context) !=
      LCEVC_Success)
    return FALSE;

  if (lcevc->max_width > 0)
    LCEVC_ConfigureDecoderInt (lcevc->decoder_handle, "max_width",
        lcevc->max_width);
  if (lcevc->max_height > 0)
    LCEVC_ConfigureDecoderInt (lcevc->decoder_handle, "max_height",
        lcevc->max_height);
  if (lcevc->max_latency > 0)
    LCEVC_ConfigureDecoderInt (lcevc->decoder_handle, "max_latency",
        lcevc->max_latency);

  if (lcevc->verbose) {
    LCEVC_ConfigureDecoderBool (lcevc->decoder_handle, "log_stdout", TRUE);
    LCEVC_ConfigureDecoderInt (lcevc->decoder_handle, "log_level", 2);
  }

  LCEVC_ConfigureDecoderIntArray (lcevc->decoder_handle, "events", 8, events);

  LCEVC_SetDecoderEventCallback (lcevc->decoder_handle, event_callback, lcevc);

  if (LCEVC_InitializeDecoder (lcevc->decoder_handle) != LCEVC_Success)
    return FALSE;

  return TRUE;
}

static gboolean
gst_lcevc_dec_start (GstVideoDecoder * decoder)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (decoder);

  /* Reset */
  lcevc->out_alloc_width = 0;
  lcevc->out_alloc_height = 0;

  /* Initialize LCEVC decoder */
  if (!initialize_lcevc_decoder (lcevc)) {
    GST_ELEMENT_ERROR (decoder, LIBRARY, INIT, (NULL),
        ("Couldn't initialize LCEVC decoder"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_lcevc_dec_stop (GstVideoDecoder * decoder)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (decoder);

  /* Clear input state */
  g_clear_pointer (&lcevc->input_state, gst_video_codec_state_unref);

  /* Clear output state */
  g_clear_pointer (&lcevc->output_state, gst_video_codec_state_unref);

  /* Destry LCEVC decoder */
  LCEVC_DestroyDecoder (lcevc->decoder_handle);

  return TRUE;
}

static gboolean
gst_lcevc_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (decoder);

  if (!GST_VIDEO_DECODER_CLASS (gst_lcevc_dec_parent_class)->decide_allocation
      (decoder, query))
    return FALSE;

  lcevc->can_crop = gst_query_find_allocation_meta (query,
      GST_VIDEO_CROP_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
ensure_output_resolution (GstLcevcDec * lcevc, guint32 width, guint32 height,
    guint32 alloc_width, guint32 alloc_height)
{
  GstVideoCodecState *curr_s, *new_s;

  curr_s = lcevc->output_state ? lcevc->output_state : lcevc->input_state;
  if (curr_s &&
      width == GST_VIDEO_INFO_WIDTH (&curr_s->info) &&
      height == GST_VIDEO_INFO_HEIGHT (&curr_s->info) &&
      alloc_width == lcevc->out_alloc_width &&
      alloc_height == lcevc->out_alloc_height)
    return TRUE;

  new_s = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (lcevc),
      GST_VIDEO_INFO_FORMAT (&lcevc->input_state->info), width, height, curr_s);
  if (!new_s)
    return FALSE;

  /* Set allocation caps */
  new_s->allocation_caps = gst_video_info_to_caps (&new_s->info);
  gst_caps_set_simple (new_s->allocation_caps, "width", G_TYPE_INT, alloc_width,
      "height", G_TYPE_INT, alloc_height, NULL);
  lcevc->out_alloc_width = alloc_width;
  lcevc->out_alloc_height = alloc_height;

  g_clear_pointer (&lcevc->output_state, gst_video_codec_state_unref);
  lcevc->output_state = new_s;

  GST_INFO_OBJECT (lcevc, "Set output resolution to %dx%d", width, height);

  return TRUE;
}

static gboolean
ensure_output_par (GstLcevcDec * lcevc, guint32 par_n, guint32 par_d)
{
  GstVideoCodecState *curr_s, *new_s;

  curr_s = lcevc->output_state ? lcevc->output_state : lcevc->input_state;
  if (curr_s &&
      par_n == GST_VIDEO_INFO_PAR_N (&curr_s->info) &&
      par_d == GST_VIDEO_INFO_PAR_D (&curr_s->info))
    return TRUE;

  new_s = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (lcevc),
      GST_VIDEO_INFO_FORMAT (&curr_s->info),
      GST_VIDEO_INFO_WIDTH (&curr_s->info),
      GST_VIDEO_INFO_HEIGHT (&curr_s->info), curr_s);
  if (!new_s)
    return FALSE;

  new_s->allocation_caps =
      curr_s->allocation_caps ? gst_caps_ref (curr_s->allocation_caps) : NULL;

  GST_VIDEO_INFO_PAR_N (&new_s->info) = par_n;
  GST_VIDEO_INFO_PAR_D (&new_s->info) = par_d;

  g_clear_pointer (&lcevc->output_state, gst_video_codec_state_unref);
  lcevc->output_state = new_s;

  GST_INFO_OBJECT (lcevc, "Set output par to %d/%d", par_n, par_d);

  return TRUE;
}

static gboolean
gst_lcevc_dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (decoder);
  LCEVC_ColorFormat format;

  /* Make sure format is supported */
  format =
      gst_lcevc_dec_utils_get_color_format (GST_VIDEO_INFO_FORMAT
      (&state->info));
  if (format == LCEVC_ColorFormat_Unknown)
    return FALSE;

  /* Keep input state reference */
  g_clear_pointer (&lcevc->input_state, gst_video_codec_state_unref);
  lcevc->input_state = gst_video_codec_state_ref (state);

  GST_INFO_OBJECT (lcevc, "Input resolution changed to %dx%d",
      GST_VIDEO_INFO_WIDTH (&lcevc->input_state->info),
      GST_VIDEO_INFO_HEIGHT (&lcevc->input_state->info));

  return TRUE;
}

static GstVideoCodecFrame *
find_pending_frame_from_picture_handle (GstLcevcDec * lcevc,
    LCEVC_PictureHandle picture_handle)
{
  GList *iter;
  GList *pending_frames;
  GstVideoCodecFrame *found = NULL;

  pending_frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (lcevc));

  for (iter = pending_frames; iter; iter = g_list_next (iter)) {
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) iter->data;
    PictureData *pd;

    pd = gst_mini_object_get_qdata (GST_MINI_OBJECT (frame->output_buffer),
        GST_LCEVC_DEC_PICTURE_DATA);
    if (pd->picture_handle.hdl == picture_handle.hdl) {
      found = gst_video_codec_frame_ref (frame);
      break;
    }
  }

  g_list_free_full (pending_frames,
      (GDestroyNotify) gst_video_codec_frame_unref);

  return found;
}

static gboolean
receive_enhanced_picture (GstLcevcDec * lcevc)
{
  LCEVC_PictureHandle picture_handle = { 0, };
  LCEVC_DecodeInformation decode_info = { 0, };
  LCEVC_ReturnCode ret;

  while ((ret = LCEVC_ReceiveDecoderPicture (lcevc->decoder_handle,
              &picture_handle, &decode_info)) == LCEVC_Success) {
    LCEVC_PictureDesc pic_desc = { 0, };
    GstVideoCodecFrame *received_frame;

    if (LCEVC_GetPictureDesc (lcevc->decoder_handle, picture_handle,
            &pic_desc) != LCEVC_Success) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Could not get desciption of received enhanced picutre"));
      return FALSE;
    }

    GST_INFO_OBJECT (lcevc,
        "Received enhanced picture: ts=%" G_GINT64_FORMAT " e=%d w=%d h=%d"
        " t=%d b=%d l=%d r=%d par=%d/%d",
        decode_info.timestamp, decode_info.enhanced, pic_desc.width,
        pic_desc.height, pic_desc.cropTop, pic_desc.cropBottom,
        pic_desc.cropLeft, pic_desc.cropRight, pic_desc.sampleAspectRatioNum,
        pic_desc.sampleAspectRatioDen);

    /* Get the pending frame */
    received_frame = find_pending_frame_from_picture_handle (lcevc,
        picture_handle);
    if (!received_frame) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Decoded LCEVC picture has no pending frame"));
      return FALSE;
    }

    /* Make sure enhanced resolution is valid */
    if (pic_desc.width != GST_VIDEO_INFO_WIDTH (&lcevc->output_state->info) ||
        pic_desc.height != GST_VIDEO_INFO_HEIGHT (&lcevc->output_state->info)) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Decoded LCEVC picture has wrong resolution"));
      gst_video_codec_frame_unref (received_frame);
      return FALSE;
    }

    /* Check if decoded picture is cropped */
    if (pic_desc.cropTop > 0 || pic_desc.cropBottom > 0 ||
        pic_desc.cropLeft > 0 || pic_desc.cropRight > 0) {
      guint32 crop_width, crop_height;

      /* Make sure enhanced crop dimensions are valid */
      if (pic_desc.width <= pic_desc.cropLeft + pic_desc.cropRight ||
          pic_desc.height <= pic_desc.cropTop + pic_desc.cropBottom) {
        GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
            ("Decoded LCEVC picture has wrong crop dimensions"));
        gst_video_codec_frame_unref (received_frame);
        return FALSE;
      }

      crop_width = pic_desc.width - (pic_desc.cropLeft + pic_desc.cropRight);
      crop_height = pic_desc.height - (pic_desc.cropTop + pic_desc.cropBottom);

      /* Attach crop meta if downstream can crop */
      if (lcevc->can_crop) {
        GstVideoCropMeta *cmeta;
        cmeta = gst_buffer_add_video_crop_meta (received_frame->output_buffer);
        cmeta->x = pic_desc.cropLeft;
        cmeta->y = pic_desc.cropTop;
        cmeta->width = crop_width;
        cmeta->height = crop_height;

        /* Update the crop resolution */
        if (!ensure_output_resolution (lcevc, crop_width, crop_height,
                lcevc->out_alloc_width, lcevc->out_alloc_width)) {
          GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
              ("Could not update output crop resolution"));
          gst_video_codec_frame_unref (received_frame);
          return FALSE;
        }
      } else {
        /* FIXME: Do a copy of the cropped area instead of error */
        GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
            ("Decoded LCEVC picture is cropped but downstream cannot crop"));
        gst_video_codec_frame_unref (received_frame);
        return FALSE;
      }
    }

    /* Update the pixel aspect ratio */
    if (!ensure_output_par (lcevc, pic_desc.sampleAspectRatioNum,
            pic_desc.sampleAspectRatioDen)) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Could not update output pixel aspect ratio"));
      gst_video_codec_frame_unref (received_frame);
      return FALSE;
    }

    /* Finish frame */
    received_frame->output_buffer->pts = decode_info.timestamp;
    gst_video_decoder_finish_frame (GST_VIDEO_DECODER (lcevc), received_frame);
    gst_video_codec_frame_unref (received_frame);
  }

  /* Make sure no errors happened */
  if (ret == LCEVC_Error) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not receive enhanced picture"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
receive_base_picture (GstLcevcDec * lcevc)
{
  LCEVC_PictureHandle picture_handle = { 0, };
  LCEVC_ReturnCode ret;

  while ((ret = LCEVC_ReceiveDecoderBase (lcevc->decoder_handle,
              &picture_handle)) == LCEVC_Success) {
    GST_DEBUG_OBJECT (lcevc, "Received base picture %" G_GUINTPTR_FORMAT,
        picture_handle.hdl);

    if (LCEVC_FreePicture (lcevc->decoder_handle, picture_handle)
        != LCEVC_Success) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Could not free base picture %" G_GUINTPTR_FORMAT,
              picture_handle.hdl));
      return FALSE;
    }
  }

  /* Make sure no errors happened */
  if (ret == LCEVC_Error) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not receive base picture"));
    return FALSE;
  }

  return TRUE;
}

static gboolean
send_enhancement_data (GstLcevcDec * lcevc, GstBuffer * input_buffer)
{
  gboolean ret = FALSE;
  GstLcevcMeta *lcevc_meta;
  GstMapInfo enhancement_info;
  uint32_t out_w, out_h;

  out_w = GST_VIDEO_INFO_WIDTH (&lcevc->input_state->info);
  out_h = GST_VIDEO_INFO_HEIGHT (&lcevc->input_state->info);

  lcevc_meta = gst_buffer_get_lcevc_meta (input_buffer);
  if (!lcevc_meta) {
    GST_INFO_OBJECT (lcevc,
        "Input buffer %" GST_TIME_FORMAT
        " enhancement data not found, doing passthrough",
        GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer)));
    return ensure_output_resolution (lcevc, out_w, out_h, out_w, out_h);
  }

  if (!gst_buffer_map (lcevc_meta->enhancement_data, &enhancement_info,
          GST_MAP_READ)) {
    GST_INFO_OBJECT (lcevc, "Could not map input buffer %" GST_TIME_FORMAT
        " enhancement data", GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer)));
    goto done;
  }

  if (LCEVC_SendDecoderEnhancementData (lcevc->decoder_handle,
          input_buffer->pts, enhancement_info.data,
          enhancement_info.size) != LCEVC_Success) {
    GST_INFO_OBJECT (lcevc,
        "Could not send input buffer %" GST_TIME_FORMAT
        " enhancement data with size %zu",
        GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer)), enhancement_info.size);
    goto done;
  }

  /* Now peek and update the output resolution */
  if (LCEVC_PeekDecoder (lcevc->decoder_handle, input_buffer->pts,
          &out_w, &out_h) != LCEVC_Success) {
    GST_INFO_OBJECT (lcevc, "Could not peek decoder for output resolution");
    goto done;
  }

  if (!ensure_output_resolution (lcevc, out_w, out_h, out_w, out_h)) {
    GST_INFO_OBJECT (lcevc, "Could not set output resolution to %dx%d", out_w,
        out_h);
    goto done;
  }

  GST_INFO_OBJECT (lcevc,
      "Sent input buffer %" GST_TIME_FORMAT " enhancement data with size %zu",
      GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer)), enhancement_info.size);
  ret = TRUE;

done:
  gst_buffer_unmap (lcevc_meta->enhancement_data, &enhancement_info);
  return ret;
}

static gboolean
send_base_picture (GstLcevcDec * lcevc, GstBuffer * input_buffer)
{
  gboolean ret = FALSE;
  LCEVC_PictureHandle picture_handle;
  GstVideoFrame frame = { 0, };

  if (!gst_video_frame_map (&frame, &lcevc->input_state->info, input_buffer,
          GST_MAP_READ)) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not map input buffer %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer))));
    goto done;
  }

  if (!gst_lcevc_dec_utils_alloc_picture_handle (lcevc->decoder_handle,
          &frame, &picture_handle, LCEVC_Access_Read)) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not allocate input picture handle %" GST_TIME_FORMAT,
            GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer))));
    goto done;
  }

  if (LCEVC_SendDecoderBase (lcevc->decoder_handle, input_buffer->pts,
          picture_handle, 1000000, NULL) != LCEVC_Success) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not send input buffer %" GST_TIME_FORMAT " base picture",
            GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer))));
    goto done;
  }

  GST_INFO_OBJECT (lcevc,
      "Sent input buffer %" GST_TIME_FORMAT " base picture %dx%d",
      GST_TIME_ARGS (GST_BUFFER_PTS (input_buffer)),
      GST_VIDEO_FRAME_WIDTH (&frame), GST_VIDEO_FRAME_HEIGHT (&frame));
  ret = TRUE;

done:
  gst_video_frame_unmap (&frame);
  return ret;
}

static gboolean
send_enhanced_picture (GstLcevcDec * lcevc, GstVideoCodecFrame * frame)
{
  GstVideoCodecState *s = NULL;
  GstVideoFrame map = { 0, };
  PictureData *pd;
  gboolean res = FALSE;

  /* Get output state */
  s = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (lcevc));
  if (!s) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not get output state"));
    goto done;
  }

  /* Map the video frame */
  if (!gst_video_frame_map (&map, &s->info, frame->output_buffer,
          GST_MAP_WRITE)) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not map output buffer for writing"));
    goto done;
  }

  /* Get pic data if any and size didn't change, otherwise create a new one */
  pd = gst_mini_object_get_qdata (GST_MINI_OBJECT (frame->output_buffer),
      GST_LCEVC_DEC_PICTURE_DATA);
  if (!pd || pd->width != lcevc->out_alloc_width ||
      pd->height != lcevc->out_alloc_height) {
    /* Create picture data */
    pd = picture_data_new (lcevc->decoder_handle, &map);
    if (!pd) {
      GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
          ("Could not create output picture data"));
      goto done;
    }

    /* Set picture data on buffer */
    gst_mini_object_set_qdata (GST_MINI_OBJECT (frame->output_buffer),
        GST_LCEVC_DEC_PICTURE_DATA, pd, picture_data_free);
  }

  /* Send enhanced picture */
  if (LCEVC_SendDecoderPicture (lcevc->decoder_handle, pd->picture_handle)
      != LCEVC_Success) {
    GST_ELEMENT_ERROR (lcevc, STREAM, DECODE, (NULL),
        ("Could not send output buffer enhanced picture"));
    goto done;
  }

  res = TRUE;

done:
  gst_video_frame_unmap (&map);
  g_clear_pointer (&s, gst_video_codec_state_unref);
  return res;
}

static GstFlowReturn
gst_lcevc_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstLcevcDec *lcevc = GST_LCEVC_DEC (decoder);

  GST_DEBUG_OBJECT (decoder, "Handling frame %" G_GUINT32_FORMAT
      " with timestamp %" GST_TIME_FORMAT,
      frame->system_frame_number,
      GST_TIME_ARGS (GST_BUFFER_PTS (frame->input_buffer)));

  if (!send_enhancement_data (lcevc, frame->input_buffer))
    goto error;

  if (!send_base_picture (lcevc, frame->input_buffer))
    goto error;

  if (gst_video_decoder_allocate_output_frame (decoder, frame) != GST_FLOW_OK) {
    GST_ELEMENT_ERROR (decoder, STREAM, DECODE, (NULL),
        ("Could not allocate output frame"));
    goto error;
  }

  if (!send_enhanced_picture (lcevc, frame))
    goto error;

  if (!receive_enhanced_picture (lcevc))
    goto error;

  if (!receive_base_picture (lcevc))
    goto error;

  return GST_FLOW_OK;

error:
  gst_video_decoder_drop_frame (decoder, frame);
  return GST_FLOW_ERROR;
}

static void
gst_lcevc_dec_class_init (GstLcevcDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *bt_class = GST_VIDEO_DECODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &sink_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &src_template_factory);
  gst_element_class_set_static_metadata (element_class,
      "LCEVC Decoder", "Codec/Decoder/Video",
      "Enhances video frames using attached LCEVC metadata",
      "Julian Bouzas <julian.bouzas@collabora.com>");

  gobject_class->set_property = gst_lcevc_dec_set_property;
  gobject_class->get_property = gst_lcevc_dec_get_property;

  bt_class->start = gst_lcevc_dec_start;
  bt_class->stop = gst_lcevc_dec_stop;
  bt_class->decide_allocation = gst_lcevc_dec_decide_allocation;
  bt_class->set_format = gst_lcevc_dec_set_format;
  bt_class->handle_frame = gst_lcevc_dec_handle_frame;

  g_object_class_install_property (gobject_class, PROP_VERBOSE,
      g_param_spec_boolean ("verbose", "Verbose",
          "Output status information of the LCEVC Decoder SDK",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_WIDTH,
      g_param_spec_int ("max-width", "Maximum Width",
          "The maximum width for the LCEVC decoder (0 = default)",
          0, G_MAXINT, DEFAULT_MAX_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_HEIGHT,
      g_param_spec_int ("max-height", "Maximum Height",
          "The maximum height for the LCEVC decoder (0 = default)",
          0, G_MAXINT, DEFAULT_MAX_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_LATENCY,
      g_param_spec_int ("max-latency", "Maximum Latency",
          "The maximum latency in frames for the LCEVC decoder (0 = default)",
          0, G_MAXINT, DEFAULT_MAX_LATENCY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
