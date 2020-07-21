/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
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

/*
 * Copyright 2015 The Chromium Authors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *    * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * SECTION:gstvp9decoder
 * @title: Gstvp9Decoder
 * @short_description: Base class to implement stateless VP9 decoders
 * @sources:
 * - gstvp9picture.h
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstvp9decoder.h"

GST_DEBUG_CATEGORY (gst_vp9_decoder_debug);
#define GST_CAT_DEFAULT gst_vp9_decoder_debug

struct _GstVp9DecoderPrivate
{
  gint width;
  gint height;
  GstVP9Profile profile;

  gboolean had_sequence;

  GstVp9Parser *parser;
  GstVp9Dpb *dpb;

  gboolean wait_keyframe;
};

#define parent_class gst_vp9_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVp9Decoder, gst_vp9_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstVp9Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_vp9_decoder_debug, "vp9decoder", 0,
        "VP9 Video Decoder"));

static gboolean gst_vp9_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_vp9_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_vp9_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_vp9_decoder_finish (GstVideoDecoder * decoder);
static gboolean gst_vp9_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_vp9_decoder_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_vp9_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static GstVp9Picture *gst_vp9_decoder_duplicate_picture_default (GstVp9Decoder *
    decoder, GstVp9Picture * picture);

static void
gst_vp9_decoder_class_init (GstVp9DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_vp9_decoder_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vp9_decoder_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vp9_decoder_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_vp9_decoder_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_vp9_decoder_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_vp9_decoder_drain);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp9_decoder_handle_frame);

  klass->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_vp9_decoder_duplicate_picture_default);
}

static void
gst_vp9_decoder_init (GstVp9Decoder * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = gst_vp9_decoder_get_instance_private (self);
}

static gboolean
gst_vp9_decoder_start (GstVideoDecoder * decoder)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderPrivate *priv = self->priv;

  priv->parser = gst_vp9_parser_new ();
  priv->dpb = gst_vp9_dpb_new ();
  priv->wait_keyframe = TRUE;

  return TRUE;
}

static gboolean
gst_vp9_decoder_stop (GstVideoDecoder * decoder)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderPrivate *priv = self->priv;

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  if (priv->parser) {
    gst_vp9_parser_free (priv->parser);
    priv->parser = NULL;
  }

  if (priv->dpb) {
    gst_vp9_dpb_free (priv->dpb);
    priv->dpb = NULL;
  }

  return TRUE;
}

static gboolean
gst_vp9_decoder_check_codec_change (GstVp9Decoder * self,
    const GstVp9FrameHdr * frame_hdr)
{
  GstVp9DecoderPrivate *priv = self->priv;
  gboolean ret = TRUE;
  gboolean changed = FALSE;

  if (priv->width != frame_hdr->width || priv->height != frame_hdr->height) {
    GST_INFO_OBJECT (self, "resolution changed %dx%d", frame_hdr->width,
        frame_hdr->height);
    priv->width = frame_hdr->width;
    priv->height = frame_hdr->height;
    changed = TRUE;
  }

  if (priv->profile != frame_hdr->profile) {
    GST_INFO_OBJECT (self, "profile changed %d", frame_hdr->profile);
    priv->profile = frame_hdr->profile;
    changed = TRUE;
  }

  if (changed || !priv->had_sequence) {
    GstVp9DecoderClass *klass = GST_VP9_DECODER_GET_CLASS (self);

    priv->had_sequence = TRUE;
    if (klass->new_sequence)
      priv->had_sequence = klass->new_sequence (self, frame_hdr);

    ret = priv->had_sequence;
  }

  return ret;
}

static gboolean
gst_vp9_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (decoder, "Set format");

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);

  self->input_state = gst_video_codec_state_ref (state);

  priv->width = GST_VIDEO_INFO_WIDTH (&state->info);
  priv->height = GST_VIDEO_INFO_HEIGHT (&state->info);

  return TRUE;
}

static void
gst_vp9_decoder_reset (GstVp9Decoder * self)
{
  GstVp9DecoderPrivate *priv = self->priv;

  if (priv->dpb)
    gst_vp9_dpb_clear (priv->dpb);

  priv->wait_keyframe = TRUE;
}

static GstFlowReturn
gst_vp9_decoder_finish (GstVideoDecoder * decoder)
{
  GST_DEBUG_OBJECT (decoder, "finish");

  gst_vp9_decoder_reset (GST_VP9_DECODER (decoder));

  return GST_FLOW_OK;
}

static gboolean
gst_vp9_decoder_flush (GstVideoDecoder * decoder)
{
  GST_DEBUG_OBJECT (decoder, "flush");

  gst_vp9_decoder_reset (GST_VP9_DECODER (decoder));

  return TRUE;
}

static GstFlowReturn
gst_vp9_decoder_drain (GstVideoDecoder * decoder)
{
  GST_DEBUG_OBJECT (decoder, "drain");

  gst_vp9_decoder_reset (GST_VP9_DECODER (decoder));

  return GST_FLOW_OK;
}

static GstVp9Picture *
gst_vp9_decoder_duplicate_picture_default (GstVp9Decoder * decoder,
    GstVp9Picture * picture)
{
  GstVp9Picture *new_picture;

  new_picture = gst_vp9_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;

  return new_picture;
}

static GstFlowReturn
gst_vp9_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderClass *klass = GST_VP9_DECODER_GET_CLASS (self);
  GstVp9DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;
  GstVp9FrameHdr frame_hdr[GST_VP9_MAX_FRAMES_IN_SUPERFRAME];
  GstVp9Picture *picture = NULL;
  GstVp9FrameHdr *cur_hdr;
  GstVp9ParserResult pres;
  GstVp9SuperframeInfo superframe_info;
  GstMapInfo map;
  GstFlowReturn ret = GST_FLOW_OK;
  gint i;
  gsize offset = 0;
  gint frame_idx_to_consume = 0;

  GST_LOG_OBJECT (self, "handle frame %" GST_PTR_FORMAT, in_buf);

  if (!gst_buffer_map (in_buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Cannot map input buffer");
    goto error;
  }

  pres = gst_vp9_parser_parse_superframe_info (priv->parser,
      &superframe_info, map.data, map.size);
  if (pres != GST_VP9_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parse superframe header");
    goto unmap_and_error;
  }

  if (superframe_info.frames_in_superframe > 1) {
    GST_LOG_OBJECT (self,
        "Have %d frames in superframe", superframe_info.frames_in_superframe);
  }

  for (i = 0; i < superframe_info.frames_in_superframe; i++) {
    pres = gst_vp9_parser_parse_frame_header (priv->parser, &frame_hdr[i],
        map.data + offset, superframe_info.frame_sizes[i]);

    if (pres != GST_VP9_PARSER_OK) {
      GST_ERROR_OBJECT (self, "Failed to parsing frame header %d", i);
      goto unmap_and_error;
    }

    offset += superframe_info.frame_sizes[i];
  }

  /* if we have multiple frames in superframe here,
   * decide which frame should consume given GstVideoCodecFrame.
   * In practice, superframe consists of two frame, one is decode-only frame
   * and the other is normal frame. If it's not the case, existing vp9 decoder
   * implementations (nvdec, vp9dec, d3d11 and so on) would
   * show mismatched number of input and output buffers.
   * To handle it in generic manner, we need vp9parse element to
   * split frames from superframe. */
  if (superframe_info.frames_in_superframe > 1) {
    for (i = 0; i < superframe_info.frames_in_superframe; i++) {
      if (frame_hdr[i].show_frame) {
        frame_idx_to_consume = i;
        break;
      }
    }

    /* if all frames are decode-only, choose the first one
     * (seems to be no possibility) */
    if (i == superframe_info.frames_in_superframe)
      frame_idx_to_consume = 0;
  }

  if (priv->wait_keyframe && frame_hdr[0].frame_type != GST_VP9_KEY_FRAME) {
    GST_DEBUG_OBJECT (self, "Drop frame before initial keyframe");
    gst_buffer_unmap (in_buf, &map);

    return gst_video_decoder_drop_frame (decoder, frame);;
  }

  if (frame_hdr[0].frame_type == GST_VP9_KEY_FRAME &&
      !gst_vp9_decoder_check_codec_change (self, &frame_hdr[0])) {
    GST_ERROR_OBJECT (self, "codec change error");
    goto unmap_and_error;
  }

  if (!priv->had_sequence) {
    GST_WARNING_OBJECT (self, "No handled frame header, drop frame");
    goto unmap_and_error;
  }

  priv->wait_keyframe = FALSE;

  offset = 0;
  for (i = 0; i < superframe_info.frames_in_superframe; i++) {
    GstVideoCodecFrame *cur_frame = NULL;
    cur_hdr = &frame_hdr[i];

    if (cur_hdr->show_existing_frame) {
      GstVp9Picture *pic_to_dup;

      if (cur_hdr->frame_to_show >= GST_VP9_REF_FRAMES ||
          !priv->dpb->pic_list[cur_hdr->frame_to_show]) {
        GST_ERROR_OBJECT (self, "Invalid frame_to_show %d",
            cur_hdr->frame_to_show);
        goto unmap_and_error;
      }

      g_assert (klass->duplicate_picture);
      pic_to_dup = priv->dpb->pic_list[cur_hdr->frame_to_show];
      picture = klass->duplicate_picture (self, pic_to_dup);

      if (!picture) {
        GST_ERROR_OBJECT (self, "subclass didn't provide duplicated picture");
        goto unmap_and_error;
      }

      picture->pts = GST_BUFFER_PTS (in_buf);
      picture->size = 0;

      if (i == frame_idx_to_consume)
        cur_frame = gst_video_codec_frame_ref (frame);

      g_assert (klass->output_picture);

      /* transfer ownership of picture */
      ret = klass->output_picture (self, cur_frame, picture);
      picture = NULL;
    } else {
      picture = gst_vp9_picture_new ();
      picture->frame_hdr = *cur_hdr;
      picture->pts = GST_BUFFER_PTS (in_buf);

      picture->data = map.data + offset;
      picture->size = superframe_info.frame_sizes[i];

      picture->subsampling_x = priv->parser->subsampling_x;
      picture->subsampling_y = priv->parser->subsampling_y;
      picture->bit_depth = priv->parser->bit_depth;

      if (i == frame_idx_to_consume)
        cur_frame = gst_video_codec_frame_ref (frame);

      if (klass->new_picture) {
        if (!klass->new_picture (self, cur_frame, picture)) {
          GST_ERROR_OBJECT (self, "new picture error");
          goto unmap_and_error;
        }
      }

      if (klass->start_picture) {
        if (!klass->start_picture (self, picture)) {
          GST_ERROR_OBJECT (self, "start picture error");
          goto unmap_and_error;
        }
      }

      if (klass->decode_picture) {
        if (!klass->decode_picture (self, picture, priv->dpb)) {
          GST_ERROR_OBJECT (self, "decode picture error");
          goto unmap_and_error;
        }
      }

      if (klass->end_picture) {
        if (!klass->end_picture (self, picture)) {
          GST_ERROR_OBJECT (self, "end picture error");
          goto unmap_and_error;
        }
      }

      /* Just pass our picture to dpb object.
       * Even if this picture does not need to be added to dpb
       * (i.e., not a reference frame), gst_vp9_dpb_add() will take care of
       * the case as well */
      gst_vp9_dpb_add (priv->dpb, gst_vp9_picture_ref (picture));

      g_assert (klass->output_picture);

      /* transfer ownership of picture */
      ret = klass->output_picture (self, cur_frame, picture);
      picture = NULL;
    }

    if (ret != GST_FLOW_OK)
      break;

    offset += superframe_info.frame_sizes[i];
  }

  gst_buffer_unmap (in_buf, &map);
  gst_video_codec_frame_unref (frame);

  return ret;

unmap_and_error:
  {
    gst_buffer_unmap (in_buf, &map);
    goto error;
  }

error:
  {
    if (picture)
      gst_vp9_picture_unref (picture);

    gst_video_decoder_drop_frame (decoder, frame);
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), ret);

    return ret;
  }
}
