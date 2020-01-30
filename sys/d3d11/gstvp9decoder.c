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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstvp9decoder.h"

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_vp9_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_vp9_dec_debug

struct _GstVp9DecoderPrivate
{
  gint width;
  gint height;
  GstVP9Profile profile;

  gboolean had_sequence;

  GstVp9Parser *parser;
  GstVp9Dpb *dpb;

  GstVp9Picture *current_picture;

  guint num_frames;             /* number of frames in a super frame */
  gsize frame_sizes[8];         /* size of frames in a super frame */
  guint frame_cnt;              /* frame count variable for super frame */
  guint total_idx_size;         /* super frame index size (full block size) */
  gboolean had_superframe_hdr;  /* indicate the presense of super frame */
};

#define parent_class gst_vp9_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstVp9Decoder, gst_vp9_decoder,
    GST_TYPE_VIDEO_DECODER);

static gboolean gst_vp9_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_vp9_decoder_stop (GstVideoDecoder * decoder);

static GstFlowReturn gst_vp9_decoder_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos);
static gboolean gst_vp9_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
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
  decoder_class->parse = GST_DEBUG_FUNCPTR (gst_vp9_decoder_parse);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vp9_decoder_set_format);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vp9_decoder_handle_frame);

  klass->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_vp9_decoder_duplicate_picture_default);
}

static void
gst_vp9_decoder_init (GstVp9Decoder * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), FALSE);

  self->priv = gst_vp9_decoder_get_instance_private (self);
}

static gboolean
gst_vp9_decoder_start (GstVideoDecoder * decoder)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderPrivate *priv = self->priv;

  priv->parser = gst_vp9_parser_new ();
  priv->dpb = gst_vp9_dpb_new ();

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
      ret = klass->new_sequence (self, frame_hdr);
  }

  return ret;
}

static gboolean
gst_vp9_decoder_parse_super_frame (GstVp9Decoder * self, const guint8 * data,
    gsize size, gsize * frame_sizes, guint * frame_count,
    guint * total_idx_size)
{
  guint8 marker;
  guint32 num_frames = 1, frame_size_length, total_index_size;
  guint i, j;

  if (size <= 0)
    return FALSE;

  marker = data[size - 1];

  if ((marker & 0xe0) == 0xc0) {

    GST_DEBUG_OBJECT (self, "Got VP9-Super Frame, size %" G_GSIZE_FORMAT, size);

    num_frames = (marker & 0x7) + 1;
    frame_size_length = ((marker >> 3) & 0x3) + 1;
    total_index_size = 2 + num_frames * frame_size_length;

    if ((size >= total_index_size)
        && (data[size - total_index_size] == marker)) {
      const guint8 *x = &data[size - total_index_size + 1];

      for (i = 0; i < num_frames; i++) {
        guint32 cur_frame_size = 0;

        for (j = 0; j < frame_size_length; j++)
          cur_frame_size |= (*x++) << (j * 8);

        frame_sizes[i] = cur_frame_size;
      }

      *frame_count = num_frames;
      *total_idx_size = total_index_size;
    } else {
      GST_ERROR_OBJECT (self, "Failed to parse Super-frame");
      return FALSE;
    }
  } else {
    *frame_count = num_frames;
    frame_sizes[0] = size;
    *total_idx_size = 0;
  }

  return TRUE;
}

static GstFlowReturn
gst_vp9_decoder_parse (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame, GstAdapter * adapter, gboolean at_eos)
{
  GstVp9Decoder *self = GST_VP9_DECODER (decoder);
  GstVp9DecoderClass *klass = GST_VP9_DECODER_GET_CLASS (self);
  GstVp9DecoderPrivate *priv = self->priv;
  GstVp9ParserResult pres;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstVp9FrameHdr frame_hdr;
  GstVp9Picture *picture = NULL;
  const guint8 *data;
  gsize size;
  guint buf_size;
  GstClockTime pts;

  size = gst_adapter_available (adapter);

  if (size < 1) {
    GST_LOG_OBJECT (self, "need more data");
    return GST_VIDEO_DECODER_FLOW_NEED_DATA;
  }

  pts = gst_adapter_prev_pts (adapter, NULL);
  data = (const guint8 *) gst_adapter_map (adapter, size);

  if (!priv->had_superframe_hdr) {
    if (!gst_vp9_decoder_parse_super_frame (self, data, size, priv->frame_sizes,
            &priv->num_frames, &priv->total_idx_size)) {
      goto unmap_and_error;
    }

    if (priv->num_frames > 1)
      priv->had_superframe_hdr = TRUE;
  }

  buf_size = priv->frame_sizes[priv->frame_cnt++];

  pres = gst_vp9_parser_parse_frame_header (priv->parser, &frame_hdr,
      data, buf_size);

  if (priv->frame_cnt == priv->num_frames) {
    priv->num_frames = 0;
    priv->frame_cnt = 0;
    priv->had_superframe_hdr = FALSE;
    buf_size += priv->total_idx_size;
  }

  if (pres != GST_VP9_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parsing frame header");
    goto unmap_and_error;
  }

  if (frame_hdr.show_existing_frame) {
    GstVp9Picture *pic_to_dup;

    gst_adapter_unmap (adapter);

    if (frame_hdr.frame_to_show >= GST_VP9_REF_FRAMES ||
        !priv->dpb->pic_list[frame_hdr.frame_to_show]) {
      GST_ERROR_OBJECT (self, "Invalid frame_to_show %d",
          frame_hdr.frame_to_show);
      goto error;
    }

    g_assert (klass->duplicate_picture);
    pic_to_dup = priv->dpb->pic_list[frame_hdr.frame_to_show];
    picture = klass->duplicate_picture (self, pic_to_dup);

    if (!picture) {
      GST_ERROR_OBJECT (self, "subclass didn't provide duplicated picture");
      goto error;
    }

    picture->pts = pts;
    picture->size = buf_size;

    gst_video_decoder_add_to_frame (GST_VIDEO_DECODER (self), picture->size);

    /* hold pointer to picture. default handle_frame implementation uses it */
    priv->current_picture = picture;
    flow_ret = gst_video_decoder_have_frame (GST_VIDEO_DECODER (self));

    if (flow_ret == GST_FLOW_OK) {
      if (klass->output_picture)
        flow_ret = klass->output_picture (self, picture);
    }

    gst_vp9_picture_unref (picture);
    priv->current_picture = NULL;

    return flow_ret;
  }

  if (!gst_vp9_decoder_check_codec_change (self, &frame_hdr)) {
    GST_ERROR_OBJECT (self, "codec change error");
    goto unmap_and_error;
  }

  picture = gst_vp9_picture_new ();
  picture->frame_hdr = frame_hdr;
  picture->pts = pts;

  picture->data = data;
  picture->size = buf_size;

  picture->subsampling_x = priv->parser->subsampling_x;
  picture->subsampling_y = priv->parser->subsampling_y;
  picture->bit_depth = priv->parser->bit_depth;

  if (klass->new_picture) {
    if (!klass->new_picture (self, picture)) {
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

  gst_adapter_unmap (adapter);

  gst_video_decoder_add_to_frame (GST_VIDEO_DECODER (self), picture->size);

  /* hold pointer to picture. default handle_frame implementation uses it */
  priv->current_picture = picture;
  flow_ret = gst_video_decoder_have_frame (GST_VIDEO_DECODER (self));

  if (flow_ret == GST_FLOW_OK && klass->output_picture) {
    flow_ret = klass->output_picture (self, picture);
  }

  picture->data = NULL;

  gst_vp9_dpb_add (priv->dpb, picture);
  priv->current_picture = NULL;

  return flow_ret;

unmap_and_error:
  {
    gst_adapter_unmap (adapter);
    goto error;
  }

error:
  {
    if (picture)
      gst_vp9_picture_unref (picture);

    if (size)
      gst_adapter_flush (adapter, size);

    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), flow_ret);

    return flow_ret;
  }
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
  GstVp9DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  if (!priv->current_picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    gst_video_decoder_drop_frame (decoder, frame);

    return GST_FLOW_ERROR;
  }

  gst_video_codec_frame_set_user_data (frame,
      gst_vp9_picture_ref (priv->current_picture),
      (GDestroyNotify) gst_vp9_picture_unref);

  gst_video_codec_frame_unref (frame);

  return GST_FLOW_OK;
}
