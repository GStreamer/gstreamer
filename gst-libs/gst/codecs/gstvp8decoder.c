/* GStreamer
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

/**
 * SECTION:gstvp8decoder
 * @title: GstVP8Decoder
 * @short_description: Base class to implement stateless VP8 decoders
 * @sources:
 * - gstvp8picture.h
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstvp8decoder.h"

GST_DEBUG_CATEGORY (gst_vp8_decoder_debug);
#define GST_CAT_DEFAULT gst_vp8_decoder_debug

struct _GstVp8DecoderPrivate
{
  gint width;
  gint height;

  gboolean had_sequence;
  GstVp8Parser parser;
  gboolean wait_keyframe;
};

#define parent_class gst_vp8_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVp8Decoder, gst_vp8_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstVp8Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_vp8_decoder_debug, "vp8decoder", 0,
        "VP8 Video Decoder"));

static gboolean gst_vp8_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_vp8_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_vp8_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_vp8_decoder_finish (GstVideoDecoder * decoder);
static gboolean gst_vp8_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_vp8_decoder_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_vp8_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static void
gst_vp8_decoder_class_init (GstVp8DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_vp8_decoder_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vp8_decoder_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vp8_decoder_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_vp8_decoder_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_vp8_decoder_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_vp8_decoder_drain);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp8_decoder_handle_frame);
}

static void
gst_vp8_decoder_init (GstVp8Decoder * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = gst_vp8_decoder_get_instance_private (self);
}

static gboolean
gst_vp8_decoder_start (GstVideoDecoder * decoder)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);
  GstVp8DecoderPrivate *priv = self->priv;

  gst_vp8_parser_init (&priv->parser);
  priv->wait_keyframe = TRUE;

  return TRUE;
}

static void
gst_vp8_decoder_reset (GstVp8Decoder * self)
{
  GstVp8DecoderPrivate *priv = self->priv;

  gst_vp8_picture_clear (&self->last_picture);
  gst_vp8_picture_clear (&self->golden_ref_picture);
  gst_vp8_picture_clear (&self->alt_ref_picture);

  priv->wait_keyframe = TRUE;
}

static gboolean
gst_vp8_decoder_stop (GstVideoDecoder * decoder)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  gst_vp8_decoder_reset (self);

  return TRUE;
}

static gboolean
gst_vp8_decoder_check_codec_change (GstVp8Decoder * self,
    const GstVp8FrameHdr * frame_hdr)
{
  GstVp8DecoderPrivate *priv = self->priv;
  gboolean ret = TRUE;
  gboolean changed = FALSE;

  if (priv->width != frame_hdr->width || priv->height != frame_hdr->height) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d", frame_hdr->width,
        frame_hdr->height);
    priv->width = frame_hdr->width;
    priv->height = frame_hdr->height;
    changed = TRUE;
  }

  if (changed || !priv->had_sequence) {
    GstVp8DecoderClass *klass = GST_VP8_DECODER_GET_CLASS (self);

    priv->had_sequence = TRUE;

    if (klass->new_sequence)
      ret = klass->new_sequence (self, frame_hdr);
  }

  return ret;
}

static gboolean
gst_vp8_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);
  GstVp8DecoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (decoder, "Set format");

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);

  self->input_state = gst_video_codec_state_ref (state);

  priv->width = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->height = GST_VIDEO_INFO_HEIGHT (&state->info);

  return TRUE;
}

static gboolean
gst_vp8_decoder_update_reference (GstVp8Decoder * self, GstVp8Picture * picture)
{
  GstVp8FrameHdr *frame_hdr = &picture->frame_hdr;

  if (frame_hdr->key_frame) {
    gst_vp8_picture_replace (&self->last_picture, picture);
    gst_vp8_picture_replace (&self->golden_ref_picture, picture);
    gst_vp8_picture_replace (&self->alt_ref_picture, picture);

    goto done;
  }

  if (frame_hdr->refresh_alternate_frame) {
    gst_vp8_picture_replace (&self->alt_ref_picture, picture);
  } else {
    switch (frame_hdr->copy_buffer_to_alternate) {
      case 0:
        /* do nothing */
        break;
      case 1:
        gst_vp8_picture_replace (&self->alt_ref_picture, self->last_picture);
        break;
      case 2:
        gst_vp8_picture_replace (&self->alt_ref_picture,
            self->golden_ref_picture);
        break;
      default:
        GST_WARNING_OBJECT (self, "unrecognized copy_buffer_to_alternate %d",
            frame_hdr->copy_buffer_to_alternate);
        break;
    }
  }

  if (frame_hdr->refresh_golden_frame) {
    gst_vp8_picture_replace (&self->golden_ref_picture, picture);
  } else {
    switch (frame_hdr->copy_buffer_to_golden) {
      case 0:
        /* do nothing */
        break;
      case 1:
        gst_vp8_picture_replace (&self->golden_ref_picture, self->last_picture);
        break;
      case 2:
        gst_vp8_picture_replace (&self->golden_ref_picture,
            self->alt_ref_picture);
        break;
      default:
        GST_WARNING_OBJECT (self, "unrecognized copy_buffer_to_golden %d",
            frame_hdr->copy_buffer_to_alternate);
        break;
    }
  }

  if (frame_hdr->refresh_last)
    gst_vp8_picture_replace (&self->last_picture, picture);

done:
  gst_vp8_picture_unref (picture);

  return TRUE;
}

static GstFlowReturn
gst_vp8_decoder_finish (GstVideoDecoder * decoder)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "finish");

  gst_vp8_decoder_reset (self);

  return GST_FLOW_OK;
}

static gboolean
gst_vp8_decoder_flush (GstVideoDecoder * decoder)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "flush");

  gst_vp8_decoder_reset (self);

  return TRUE;
}

static GstFlowReturn
gst_vp8_decoder_drain (GstVideoDecoder * decoder)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "drain");

  gst_vp8_decoder_reset (self);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vp8_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVp8Decoder *self = GST_VP8_DECODER (decoder);
  GstVp8DecoderClass *klass = GST_VP8_DECODER_GET_CLASS (self);
  GstVp8DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;
  GstMapInfo map;
  GstVp8FrameHdr frame_hdr;
  GstVp8ParserResult pres;
  GstVp8Picture *picture = NULL;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  if (!gst_buffer_map (in_buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Cannot map buffer");

    goto error;
  }

  pres = gst_vp8_parser_parse_frame_header (&priv->parser,
      &frame_hdr, map.data, map.size);

  if (pres != GST_VP8_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Cannot parser frame header");

    goto unmap_and_error;
  }

  if (priv->wait_keyframe) {
    if (!frame_hdr.key_frame) {
      GST_DEBUG_OBJECT (self, "Waiting initial keyframe, drop buffer %"
          GST_PTR_FORMAT, in_buf);

      gst_buffer_unmap (in_buf, &map);
      gst_video_decoder_drop_frame (decoder, frame);

      return GST_FLOW_OK;
    }
  }

  priv->wait_keyframe = FALSE;

  if (frame_hdr.key_frame &&
      !gst_vp8_decoder_check_codec_change (self, &frame_hdr)) {
    GST_ERROR_OBJECT (self, "Subclass cannot handle codec change");
    goto unmap_and_error;
  }

  picture = gst_vp8_picture_new ();
  picture->frame_hdr = frame_hdr;
  picture->pts = GST_BUFFER_PTS (in_buf);
  picture->data = map.data;
  picture->size = map.size;
  picture->system_frame_number = frame->system_frame_number;

  if (klass->new_picture) {
    if (!klass->new_picture (self, frame, picture)) {
      GST_ERROR_OBJECT (self, "subclass cannot handle new picture");
      goto unmap_and_error;
    }
  }

  if (klass->start_picture) {
    if (!klass->start_picture (self, picture)) {
      GST_ERROR_OBJECT (self, "subclass cannot handle start picture");
      goto unmap_and_error;
    }
  }

  if (klass->decode_picture) {
    if (!klass->decode_picture (self, picture, &priv->parser)) {
      GST_ERROR_OBJECT (self, "subclass cannot decode current picture");
      goto unmap_and_error;
    }
  }

  if (klass->end_picture) {
    if (!klass->end_picture (self, picture)) {
      GST_ERROR_OBJECT (self, "subclass cannot handle end picture");
      goto unmap_and_error;
    }
  }

  gst_buffer_unmap (in_buf, &map);

  gst_vp8_decoder_update_reference (self, gst_vp8_picture_ref (picture));

  g_assert (klass->output_picture);
  return klass->output_picture (self, frame, picture);

unmap_and_error:
  {
    gst_buffer_unmap (in_buf, &map);
    goto error;
  }

error:
  {
    GstFlowReturn ret;

    if (picture)
      gst_vp8_picture_unref (picture);

    gst_video_decoder_drop_frame (decoder, frame);
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), ret);

    return ret;
  }
}
