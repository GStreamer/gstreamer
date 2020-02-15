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
 *
 * NOTE: some of implementations are copied/modified from Chromium code
 *
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

#include "gsth264decoder.h"

GST_DEBUG_CATEGORY (gst_h264_decoder_debug);
#define GST_CAT_DEFAULT gst_h264_decoder_debug

typedef enum
{
  GST_H264_DECODER_FORMAT_NONE,
  GST_H264_DECODER_FORMAT_AVC,
  GST_H264_DECODER_FORMAT_BYTE
} GstH264DecoderFormat;

typedef enum
{
  GST_H264_DECODER_ALIGN_NONE,
  GST_H264_DECODER_ALIGN_NAL,
  GST_H264_DECODER_ALIGN_AU
} GstH264DecoderAlign;

struct _GstH264DecoderPrivate
{
  gint width, height;
  gint fps_num, fps_den;
  gint upstream_par_n, upstream_par_d;
  gint parsed_par_n, parsed_par_d;
  gint parsed_fps_n, parsed_fps_d;
  GstVideoColorimetry parsed_colorimetry;
  /* input codec_data, if any */
  GstBuffer *codec_data;
  guint nal_length_size;

  /* state */
  GstH264DecoderFormat in_format;
  GstH264DecoderAlign align;
  GstH264NalParser *parser;
  GstH264Dpb *dpb;
  GstFlowReturn last_ret;

  /* sps/pps of the current slice */
  const GstH264SPS *active_sps;
  const GstH264PPS *active_pps;

  /* Picture currently being processed/decoded */
  GstH264Picture *current_picture;
  GstVideoCodecFrame *current_frame;

  /* Slice (slice header + nalu) currently being processed/decodec */
  GstH264Slice current_slice;

  gint max_frame_num;
  gint max_pic_num;
  gint max_long_term_frame_idx;
  gsize max_num_reorder_frames;

  gint prev_frame_num;
  gint prev_ref_frame_num;
  gint prev_frame_num_offset;
  gboolean prev_has_memmgmnt5;

  /* Values related to previously decoded reference picture */
  gboolean prev_ref_has_memmgmnt5;
  gint prev_ref_top_field_order_cnt;
  gint prev_ref_pic_order_cnt_msb;
  gint prev_ref_pic_order_cnt_lsb;

  GstH264PictureField prev_ref_field;

  /* PicOrderCount of the previously outputted frame */
  gint last_output_poc;
};

#define parent_class gst_h264_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH264Decoder, gst_h264_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstH264Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_h264_decoder_debug, "h264decoder", 0,
        "H.264 Video Decoder"));

static gboolean gst_h264_decoder_start (GstVideoDecoder * decoder);
static gboolean gst_h264_decoder_stop (GstVideoDecoder * decoder);
static gboolean gst_h264_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_h264_decoder_finish (GstVideoDecoder * decoder);
static gboolean gst_h264_decoder_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_h264_decoder_drain (GstVideoDecoder * decoder);
static GstFlowReturn gst_h264_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

/* codec spcific functions */
static gboolean gst_h264_decoder_process_sps (GstH264Decoder * self,
    GstH264SPS * sps);
static gboolean gst_h264_decoder_decode_slice (GstH264Decoder * self);
static gboolean gst_h264_decoder_decode_nal (GstH264Decoder * self,
    GstH264NalUnit * nalu, GstClockTime pts);
static gboolean gst_h264_decoder_fill_picture_from_slice (GstH264Decoder * self,
    const GstH264Slice * slice, GstH264Picture * picture);
static gboolean gst_h264_decoder_calculate_poc (GstH264Decoder * self,
    GstH264Picture * picture);
static gboolean gst_h264_decoder_init_gap_picture (GstH264Decoder * self,
    GstH264Picture * picture, gint frame_num);
static gboolean
gst_h264_decoder_output_all_remaining_pics (GstH264Decoder * self);
static gboolean gst_h264_decoder_finish_current_picture (GstH264Decoder * self);
static gboolean gst_h264_decoder_finish_picture (GstH264Decoder * self,
    GstH264Picture * picture);

static void
gst_h264_decoder_class_init (GstH264DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  decoder_class->start = GST_DEBUG_FUNCPTR (gst_h264_decoder_start);
  decoder_class->stop = GST_DEBUG_FUNCPTR (gst_h264_decoder_stop);
  decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_h264_decoder_set_format);
  decoder_class->finish = GST_DEBUG_FUNCPTR (gst_h264_decoder_finish);
  decoder_class->flush = GST_DEBUG_FUNCPTR (gst_h264_decoder_flush);
  decoder_class->drain = GST_DEBUG_FUNCPTR (gst_h264_decoder_drain);
  decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_h264_decoder_handle_frame);
}

static void
gst_h264_decoder_init (GstH264Decoder * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = gst_h264_decoder_get_instance_private (self);
}

static gboolean
gst_h264_decoder_start (GstVideoDecoder * decoder)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);
  GstH264DecoderPrivate *priv = self->priv;

  priv->parser = gst_h264_nal_parser_new ();
  priv->dpb = gst_h264_dpb_new ();

  return TRUE;
}

static gboolean
gst_h264_decoder_stop (GstVideoDecoder * decoder)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);
  GstH264DecoderPrivate *priv = self->priv;

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  gst_clear_buffer (&priv->codec_data);

  if (priv->parser) {
    gst_h264_nal_parser_free (priv->parser);
    priv->parser = NULL;
  }

  if (priv->dpb) {
    gst_h264_dpb_free (priv->dpb);
    priv->dpb = NULL;
  }

  return TRUE;
}

static void
gst_h264_decoder_clear_dpb (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;

  gst_h264_dpb_clear (priv->dpb);
  priv->last_output_poc = -1;
}

static gboolean
gst_h264_decoder_flush (GstVideoDecoder * decoder)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);

  gst_h264_decoder_clear_dpb (self);

  return TRUE;
}

static GstFlowReturn
gst_h264_decoder_drain (GstVideoDecoder * decoder)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);
  GstH264DecoderPrivate *priv = self->priv;

  priv->last_ret = GST_FLOW_OK;
  gst_h264_decoder_output_all_remaining_pics (self);
  gst_h264_decoder_clear_dpb (self);

  return priv->last_ret;
}

static GstFlowReturn
gst_h264_decoder_finish (GstVideoDecoder * decoder)
{
  return gst_h264_decoder_drain (decoder);
}

static GstFlowReturn
gst_h264_decoder_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);
  GstH264DecoderPrivate *priv = self->priv;
  GstBuffer *in_buf = frame->input_buffer;
  GstH264NalUnit nalu;
  GstH264ParserResult pres;
  GstMapInfo map;
  gboolean decode_ret = TRUE;

  GST_LOG_OBJECT (self,
      "handle frame, PTS: %" GST_TIME_FORMAT ", DTS: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (in_buf)),
      GST_TIME_ARGS (GST_BUFFER_DTS (in_buf)));

  priv->current_frame = frame;
  priv->last_ret = GST_FLOW_OK;

  gst_buffer_map (in_buf, &map, GST_MAP_READ);
  if (priv->in_format == GST_H264_DECODER_FORMAT_AVC) {
    pres = gst_h264_parser_identify_nalu_avc (priv->parser,
        map.data, 0, map.size, priv->nal_length_size, &nalu);

    while (pres == GST_H264_PARSER_OK && decode_ret) {
      decode_ret = gst_h264_decoder_decode_nal (self,
          &nalu, GST_BUFFER_PTS (in_buf));

      pres = gst_h264_parser_identify_nalu_avc (priv->parser,
          map.data, nalu.offset + nalu.size, map.size, priv->nal_length_size,
          &nalu);
    }
  } else {
    pres = gst_h264_parser_identify_nalu (priv->parser,
        map.data, 0, map.size, &nalu);

    if (pres == GST_H264_PARSER_NO_NAL_END)
      pres = GST_H264_PARSER_OK;

    while (pres == GST_H264_PARSER_OK && decode_ret) {
      decode_ret = gst_h264_decoder_decode_nal (self,
          &nalu, GST_BUFFER_PTS (in_buf));

      pres = gst_h264_parser_identify_nalu (priv->parser,
          map.data, nalu.offset + nalu.size, map.size, &nalu);

      if (pres == GST_H264_PARSER_NO_NAL_END)
        pres = GST_H264_PARSER_OK;
    }
  }

  gst_buffer_unmap (in_buf, &map);
  priv->current_frame = NULL;

  if (!decode_ret) {
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), priv->last_ret);
    gst_video_decoder_drop_frame (decoder, frame);

    gst_h264_picture_clear (&priv->current_picture);

    return priv->last_ret;
  }

  gst_h264_decoder_finish_current_picture (self);
  gst_video_codec_frame_unref (frame);

  return priv->last_ret;
}

static gboolean
gst_h264_decoder_parse_sps (GstH264Decoder * self, GstH264NalUnit * nalu)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264SPS sps;
  GstH264ParserResult pres;
  gboolean ret = TRUE;

  pres = gst_h264_parser_parse_sps (priv->parser, nalu, &sps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "SPS parsed");

  if (!gst_h264_decoder_process_sps (self, &sps))
    ret = FALSE;

  gst_h264_sps_clear (&sps);

  return ret;
}

static gboolean
gst_h264_decoder_parse_pps (GstH264Decoder * self, GstH264NalUnit * nalu)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264PPS pps;
  GstH264ParserResult pres;

  pres = gst_h264_parser_parse_pps (priv->parser, nalu, &pps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse PPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "PPS parsed");
  gst_h264_pps_clear (&pps);

  return TRUE;
}

static gboolean
gst_h264_decoder_parse_codec_data (GstH264Decoder * self, const guint8 * data,
    gsize size)
{
  GstH264DecoderPrivate *priv = self->priv;
  guint num_sps, num_pps;
  guint off;
  gint i;
  GstH264ParserResult pres;
  GstH264NalUnit nalu;
#ifndef GST_DISABLE_GST_DEBUG
  guint profile;
#endif

  /* parse the avcC data */
  if (size < 7) {               /* when numSPS==0 and numPPS==0, length is 7 bytes */
    return FALSE;
  }

  /* parse the version, this must be 1 */
  if (data[0] != 1) {
    return FALSE;
  }
#ifndef GST_DISABLE_GST_DEBUG
  /* AVCProfileIndication */
  /* profile_compat */
  /* AVCLevelIndication */
  profile = (data[1] << 16) | (data[2] << 8) | data[3];
  GST_DEBUG_OBJECT (self, "profile %06x", profile);
#endif

  /* 6 bits reserved | 2 bits lengthSizeMinusOne */
  /* this is the number of bytes in front of the NAL units to mark their
   * length */
  priv->nal_length_size = (data[4] & 0x03) + 1;
  GST_DEBUG_OBJECT (self, "nal length size %u", priv->nal_length_size);

  num_sps = data[5] & 0x1f;
  off = 6;
  for (i = 0; i < num_sps; i++) {
    pres = gst_h264_parser_identify_nalu_avc (priv->parser,
        data, off, size, 2, &nalu);
    if (pres != GST_H264_PARSER_OK) {
      GST_WARNING_OBJECT (self, "Failed to identify SPS nalu");
      return FALSE;
    }

    gst_h264_decoder_parse_sps (self, &nalu);
    off = nalu.offset + nalu.size;
  }

  if (off >= size) {
    GST_WARNING_OBJECT (self, "Too small avcC");
    return FALSE;
  }

  num_pps = data[off];
  off++;

  for (i = 0; i < num_pps; i++) {
    pres = gst_h264_parser_identify_nalu_avc (priv->parser,
        data, off, size, 2, &nalu);
    if (pres != GST_H264_PARSER_OK) {
      GST_WARNING_OBJECT (self, "Failed to identify PPS nalu");
      return FALSE;
    }

    gst_h264_decoder_parse_pps (self, &nalu);
    off = nalu.offset + nalu.size;
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_preprocess_slice (GstH264Decoder * self, GstH264Slice * slice)
{
  GstH264DecoderPrivate *priv = self->priv;

  if (!priv->current_picture) {
    if (slice->header.first_mb_in_slice != 0) {
      GST_ERROR_OBJECT (self, "Invalid stream, first_mb_in_slice %d",
          slice->header.first_mb_in_slice);
      return FALSE;
    }

    /* If the new picture is an IDR, flush DPB */
    if (slice->nalu.idr_pic_flag) {
      /* Output all remaining pictures, unless we are explicitly instructed
       * not to do so */
      if (!slice->header.dec_ref_pic_marking.no_output_of_prior_pics_flag)
        gst_h264_decoder_drain (GST_VIDEO_DECODER (self));

      gst_h264_dpb_clear (priv->dpb);
    }
  }

  return TRUE;
}

static void
gst_h264_decoder_update_pic_nums (GstH264Decoder * self, gint frame_num)
{
  GstH264DecoderPrivate *priv = self->priv;
  GArray *dpb = gst_h264_dpb_get_pictures_all (priv->dpb);
  gint i;

  for (i = 0; i < dpb->len; i++) {
    GstH264Picture *picture = g_array_index (dpb, GstH264Picture *, i);

    if (picture->field != GST_H264_PICTURE_FIELD_FRAME) {
      GST_FIXME_OBJECT (self, "Interlaced video not supported");
      continue;
    }

    if (!picture->ref)
      continue;

    if (picture->long_term) {
      picture->long_term_pic_num = picture->long_term_frame_idx;
    } else {
      if (picture->frame_num > frame_num)
        picture->frame_num_wrap = picture->frame_num - priv->max_frame_num;
      else
        picture->frame_num_wrap = picture->frame_num;

      picture->pic_num = picture->frame_num_wrap;
    }
  }

  g_array_unref (dpb);
}

static gboolean
gst_h264_decoder_handle_frame_num_gap (GstH264Decoder * self, gint frame_num)
{
  GstH264DecoderPrivate *priv = self->priv;
  const GstH264SPS *sps = priv->active_sps;
  gint unused_short_term_frame_num;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active sps");
    return FALSE;
  }

  if (!sps->gaps_in_frame_num_value_allowed_flag) {
    GST_WARNING_OBJECT (self, "Invalid frame num %d", frame_num);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Handling frame num gap %d -> %d",
      priv->prev_ref_frame_num, frame_num);

  /* 7.4.3/7-23 */
  unused_short_term_frame_num =
      (priv->prev_ref_frame_num + 1) % priv->max_frame_num;
  while (unused_short_term_frame_num != frame_num) {
    GstH264Picture *picture = gst_h264_picture_new ();

    if (!gst_h264_decoder_init_gap_picture (self, picture,
            unused_short_term_frame_num))
      return FALSE;

    gst_h264_decoder_update_pic_nums (self, unused_short_term_frame_num);

    if (!gst_h264_decoder_finish_picture (self, picture)) {
      GST_WARNING ("Failed to finish picture %p", picture);
      return FALSE;
    }

    unused_short_term_frame_num++;
    unused_short_term_frame_num %= priv->max_frame_num;
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_init_current_picture (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;

  if (!gst_h264_decoder_fill_picture_from_slice (self, &priv->current_slice,
          priv->current_picture)) {
    return FALSE;
  }

  if (!gst_h264_decoder_calculate_poc (self, priv->current_picture))
    return FALSE;

  /* If the slice header indicates we will have to perform reference marking
   * process after this picture is decoded, store required data for that
   * purpose */
  if (priv->current_slice.header.
      dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
    priv->current_picture->dec_ref_pic_marking =
        priv->current_slice.header.dec_ref_pic_marking;
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_start_current_picture (GstH264Decoder * self)
{
  GstH264DecoderClass *klass;
  GstH264DecoderPrivate *priv = self->priv;
  const GstH264SPS *sps;
  gint frame_num;
  gboolean ret = TRUE;

  g_assert (priv->current_picture != NULL);
  g_assert (priv->active_sps != NULL);
  g_assert (priv->active_pps != NULL);

  sps = priv->active_sps;

  priv->max_frame_num = 1 << (sps->log2_max_frame_num_minus4 + 4);
  frame_num = priv->current_slice.header.frame_num;
  if (priv->current_slice.nalu.idr_pic_flag)
    priv->prev_ref_frame_num = 0;

  /* 7.4.3 */
  if (frame_num != priv->prev_ref_frame_num &&
      frame_num != (priv->prev_ref_frame_num + 1) % priv->max_frame_num) {
    if (!gst_h264_decoder_handle_frame_num_gap (self, frame_num))
      return FALSE;
  }

  if (!gst_h264_decoder_init_current_picture (self))
    return FALSE;

  gst_h264_decoder_update_pic_nums (self, frame_num);

  klass = GST_H264_DECODER_GET_CLASS (self);
  if (klass->start_picture)
    ret = klass->start_picture (self, priv->current_picture,
        &priv->current_slice, priv->dpb);

  if (!ret) {
    GST_ERROR_OBJECT (self, "subclass does not want to start picture");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_parse_slice (GstH264Decoder * self, GstH264NalUnit * nalu,
    GstClockTime pts)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264ParserResult pres = GST_H264_PARSER_OK;

  memset (&priv->current_slice, 0, sizeof (GstH264Slice));

  pres = gst_h264_parser_parse_slice_hdr (priv->parser, nalu,
      &priv->current_slice.header, TRUE, TRUE);

  if (pres != GST_H264_PARSER_OK) {
    GST_ERROR_OBJECT (self, "Failed to parse slice header, ret %d", pres);
    memset (&priv->current_slice, 0, sizeof (GstH264Slice));

    return FALSE;
  }

  priv->current_slice.nalu = *nalu;

  if (!gst_h264_decoder_preprocess_slice (self, &priv->current_slice))
    return FALSE;

  priv->active_pps = priv->current_slice.header.pps;
  priv->active_sps = priv->active_pps->sequence;

  if (!priv->current_picture) {
    GstH264DecoderClass *klass = GST_H264_DECODER_GET_CLASS (self);
    GstH264Picture *picture;
    gboolean ret = TRUE;

    picture = gst_h264_picture_new ();
    picture->pts = pts;

    if (klass->new_picture)
      ret = klass->new_picture (self, picture);

    if (!ret) {
      GST_ERROR_OBJECT (self, "subclass does not want accept new picture");
      gst_h264_picture_unref (picture);
      return FALSE;
    }

    /* This allows accessing the frame from the picture. */
    picture->system_frame_number = priv->current_frame->system_frame_number;
    priv->current_picture = picture;
    gst_video_codec_frame_set_user_data (priv->current_frame,
        gst_h264_picture_ref (priv->current_picture),
        (GDestroyNotify) gst_h264_picture_unref);

    if (!gst_h264_decoder_start_current_picture (self)) {
      GST_ERROR_OBJECT (self, "start picture failed");
      return FALSE;
    }
  }

  return gst_h264_decoder_decode_slice (self);
}

static gboolean
gst_h264_decoder_decode_nal (GstH264Decoder * self, GstH264NalUnit * nalu,
    GstClockTime pts)
{
  gboolean ret = TRUE;

  GST_LOG_OBJECT (self, "Parsed nal type: %d, offset %d, size %d",
      nalu->type, nalu->offset, nalu->size);

  switch (nalu->type) {
    case GST_H264_NAL_SPS:
      ret = gst_h264_decoder_parse_sps (self, nalu);
      break;
    case GST_H264_NAL_PPS:
      ret = gst_h264_decoder_parse_pps (self, nalu);
      break;
    case GST_H264_NAL_SLICE:
    case GST_H264_NAL_SLICE_DPA:
    case GST_H264_NAL_SLICE_DPB:
    case GST_H264_NAL_SLICE_DPC:
    case GST_H264_NAL_SLICE_IDR:
    case GST_H264_NAL_SLICE_EXT:
      ret = gst_h264_decoder_parse_slice (self, nalu, pts);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_h264_decoder_format_from_caps (GstH264Decoder * self, GstCaps * caps,
    GstH264DecoderFormat * format, GstH264DecoderAlign * align)
{
  if (format)
    *format = GST_H264_DECODER_FORMAT_NONE;

  if (align)
    *align = GST_H264_DECODER_ALIGN_NONE;

  if (!gst_caps_is_fixed (caps)) {
    GST_WARNING_OBJECT (self, "Caps wasn't fixed");
    return;
  }

  GST_DEBUG_OBJECT (self, "parsing caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;

    if (format) {
      if ((str = gst_structure_get_string (s, "stream-format"))) {
        if (strcmp (str, "avc") == 0 || strcmp (str, "avc3") == 0)
          *format = GST_H264_DECODER_FORMAT_AVC;
        else if (strcmp (str, "byte-stream") == 0)
          *format = GST_H264_DECODER_FORMAT_BYTE;
      }
    }

    if (align) {
      if ((str = gst_structure_get_string (s, "alignment"))) {
        if (strcmp (str, "au") == 0)
          *align = GST_H264_DECODER_ALIGN_AU;
        else if (strcmp (str, "nal") == 0)
          *align = GST_H264_DECODER_ALIGN_NAL;
      }
    }
  }
}

static gboolean
gst_h264_decoder_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstH264Decoder *self = GST_H264_DECODER (decoder);
  GstH264DecoderPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (decoder, "Set format");

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);

  self->input_state = gst_video_codec_state_ref (state);

  if (state->caps) {
    GstStructure *str;
    const GValue *codec_data_value;
    GstH264DecoderFormat format;
    GstH264DecoderAlign align;

    gst_h264_decoder_format_from_caps (self, state->caps, &format, &align);

    str = gst_caps_get_structure (state->caps, 0);
    codec_data_value = gst_structure_get_value (str, "codec_data");

    if (GST_VALUE_HOLDS_BUFFER (codec_data_value)) {
      gst_buffer_replace (&priv->codec_data,
          gst_value_get_buffer (codec_data_value));
    } else {
      gst_buffer_replace (&priv->codec_data, NULL);
    }

    if (format == GST_H264_DECODER_FORMAT_NONE) {
      /* codec_data implies avc */
      if (codec_data_value != NULL) {
        GST_WARNING_OBJECT (self,
            "video/x-h264 caps with codec_data but no stream-format=avc");
        format = GST_H264_DECODER_FORMAT_AVC;
      } else {
        /* otherwise assume bytestream input */
        GST_WARNING_OBJECT (self,
            "video/x-h264 caps without codec_data or stream-format");
        format = GST_H264_DECODER_FORMAT_BYTE;
      }
    }

    if (format == GST_H264_DECODER_FORMAT_AVC) {
      /* AVC requires codec_data, AVC3 might have one and/or SPS/PPS inline */
      if (codec_data_value == NULL) {
        /* Try it with size 4 anyway */
        priv->nal_length_size = 4;
        GST_WARNING_OBJECT (self,
            "avc format without codec data, assuming nal length size is 4");
      }

      /* AVC implies alignment=au */
      if (align == GST_H264_DECODER_ALIGN_NONE)
        align = GST_H264_DECODER_ALIGN_AU;
    }

    if (format == GST_H264_DECODER_FORMAT_BYTE) {
      if (codec_data_value != NULL) {
        GST_WARNING_OBJECT (self, "bytestream with codec data");
      }
    }

    priv->in_format = format;
    priv->align = align;
  }

  if (priv->codec_data) {
    GstMapInfo map;

    gst_buffer_map (priv->codec_data, &map, GST_MAP_READ);
    gst_h264_decoder_parse_codec_data (self, map.data, map.size);
    gst_buffer_unmap (priv->codec_data, &map);
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_fill_picture_from_slice (GstH264Decoder * self,
    const GstH264Slice * slice, GstH264Picture * picture)
{
  const GstH264SliceHdr *slice_hdr = &slice->header;
  const GstH264PPS *pps;
  const GstH264SPS *sps;

  pps = slice_hdr->pps;
  if (!pps) {
    GST_ERROR_OBJECT (self, "No pps in slice header");
    return FALSE;
  }

  sps = pps->sequence;
  if (!sps) {
    GST_ERROR_OBJECT (self, "No sps in pps");
    return FALSE;
  }

  picture->idr = slice->nalu.idr_pic_flag;
  if (picture->idr)
    picture->idr_pic_id = slice_hdr->idr_pic_id;

  if (slice_hdr->field_pic_flag)
    picture->field =
        slice_hdr->bottom_field_flag ?
        GST_H264_PICTURE_FIELD_BOTTOM_FIELD : GST_H264_PICTURE_FILED_TOP_FIELD;
  else
    picture->field = GST_H264_PICTURE_FIELD_FRAME;

  if (picture->field != GST_H264_PICTURE_FIELD_FRAME) {
    GST_FIXME ("Interlace video not supported");
    return FALSE;
  }

  picture->nal_ref_idc = slice->nalu.ref_idc;
  picture->ref = slice->nalu.ref_idc != 0;

  /* This assumes non-interlaced stream */
  picture->frame_num = picture->pic_num = slice_hdr->frame_num;

  picture->pic_order_cnt_type = sps->pic_order_cnt_type;
  switch (picture->pic_order_cnt_type) {
    case 0:
      picture->pic_order_cnt_lsb = slice_hdr->pic_order_cnt_lsb;
      picture->delta_pic_order_cnt_bottom =
          slice_hdr->delta_pic_order_cnt_bottom;
      break;
    case 1:
      picture->delta_pic_order_cnt0 = slice_hdr->delta_pic_order_cnt[0];
      picture->delta_pic_order_cnt1 = slice_hdr->delta_pic_order_cnt[1];
      break;
    case 2:
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_calculate_poc (GstH264Decoder * self, GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  const GstH264SPS *sps = priv->active_sps;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active SPS");
    return FALSE;
  }

  switch (picture->pic_order_cnt_type) {
    case 0:{
      /* See spec 8.2.1.1 */
      gint prev_pic_order_cnt_msb, prev_pic_order_cnt_lsb;
      gint max_pic_order_cnt_lsb;

      if (picture->idr) {
        prev_pic_order_cnt_msb = prev_pic_order_cnt_lsb = 0;
      } else {
        if (priv->prev_ref_has_memmgmnt5) {
          if (priv->prev_ref_field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
            prev_pic_order_cnt_msb = 0;
            prev_pic_order_cnt_lsb = priv->prev_ref_top_field_order_cnt;
          } else {
            prev_pic_order_cnt_msb = 0;
            prev_pic_order_cnt_lsb = 0;
          }
        } else {
          prev_pic_order_cnt_msb = priv->prev_ref_pic_order_cnt_msb;
          prev_pic_order_cnt_lsb = priv->prev_ref_pic_order_cnt_lsb;
        }
      }

      max_pic_order_cnt_lsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

      if ((picture->pic_order_cnt_lsb < prev_pic_order_cnt_lsb) &&
          (prev_pic_order_cnt_lsb - picture->pic_order_cnt_lsb >=
              max_pic_order_cnt_lsb / 2)) {
        picture->pic_order_cnt_msb =
            prev_pic_order_cnt_msb + max_pic_order_cnt_lsb;
      } else if ((picture->pic_order_cnt_lsb > prev_pic_order_cnt_lsb)
          && (picture->pic_order_cnt_lsb - prev_pic_order_cnt_lsb >
              max_pic_order_cnt_lsb / 2)) {
        picture->pic_order_cnt_msb =
            prev_pic_order_cnt_msb - max_pic_order_cnt_lsb;
      } else {
        picture->pic_order_cnt_msb = prev_pic_order_cnt_msb;
      }

      if (picture->field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->top_field_order_cnt =
            picture->pic_order_cnt_msb + picture->pic_order_cnt_lsb;
      }

      if (picture->field != GST_H264_PICTURE_FILED_TOP_FIELD) {
        if (picture->field == GST_H264_PICTURE_FIELD_FRAME) {
          picture->bottom_field_order_cnt =
              picture->top_field_order_cnt +
              picture->delta_pic_order_cnt_bottom;
        } else {
          picture->bottom_field_order_cnt =
              picture->pic_order_cnt_msb + picture->pic_order_cnt_lsb;
        }
      }
      break;
    }

    case 1:{
      gint abs_frame_num = 0;
      gint expected_pic_order_cnt = 0;
      gint i;

      /* See spec 8.2.1.2 */
      if (priv->prev_has_memmgmnt5)
        priv->prev_frame_num_offset = 0;

      if (picture->idr)
        picture->frame_num_offset = 0;
      else if (priv->prev_frame_num > picture->frame_num)
        picture->frame_num_offset =
            priv->prev_frame_num_offset + priv->max_frame_num;
      else
        picture->frame_num_offset = priv->prev_frame_num_offset;

      if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
        abs_frame_num = picture->frame_num_offset + picture->frame_num;
      else
        abs_frame_num = 0;

      if (picture->nal_ref_idc == 0 && abs_frame_num > 0)
        --abs_frame_num;

      if (abs_frame_num > 0) {
        gint pic_order_cnt_cycle_cnt, frame_num_in_pic_order_cnt_cycle;
        gint expected_delta_per_pic_order_cnt_cycle = 0;

        if (sps->num_ref_frames_in_pic_order_cnt_cycle == 0) {
          GST_WARNING_OBJECT (self,
              "Invalid num_ref_frames_in_pic_order_cnt_cycle in stream");
          return FALSE;
        }

        pic_order_cnt_cycle_cnt =
            (abs_frame_num - 1) / sps->num_ref_frames_in_pic_order_cnt_cycle;
        frame_num_in_pic_order_cnt_cycle =
            (abs_frame_num - 1) % sps->num_ref_frames_in_pic_order_cnt_cycle;

        for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
          expected_delta_per_pic_order_cnt_cycle +=
              sps->offset_for_ref_frame[i];
        }

        expected_pic_order_cnt = pic_order_cnt_cycle_cnt *
            expected_delta_per_pic_order_cnt_cycle;
        /* frame_num_in_pic_order_cnt_cycle is verified < 255 in parser */
        for (i = 0; i <= frame_num_in_pic_order_cnt_cycle; ++i)
          expected_pic_order_cnt += sps->offset_for_ref_frame[i];
      }

      if (!picture->nal_ref_idc)
        expected_pic_order_cnt += sps->offset_for_non_ref_pic;

      if (picture->field == GST_H264_PICTURE_FIELD_FRAME) {
        picture->top_field_order_cnt =
            expected_pic_order_cnt + picture->delta_pic_order_cnt0;
        picture->bottom_field_order_cnt = picture->top_field_order_cnt +
            sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt1;
      } else if (picture->field != GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->top_field_order_cnt =
            expected_pic_order_cnt + picture->delta_pic_order_cnt0;
      } else {
        picture->bottom_field_order_cnt = expected_pic_order_cnt +
            sps->offset_for_top_to_bottom_field + picture->delta_pic_order_cnt0;
      }
      break;
    }

    case 2:{
      gint temp_pic_order_cnt;

      /* See spec 8.2.1.3 */
      if (priv->prev_has_memmgmnt5)
        priv->prev_frame_num_offset = 0;

      if (picture->idr)
        picture->frame_num_offset = 0;
      else if (priv->prev_frame_num > picture->frame_num)
        picture->frame_num_offset =
            priv->prev_frame_num_offset + priv->max_frame_num;
      else
        picture->frame_num_offset = priv->prev_frame_num_offset;

      if (picture->idr) {
        temp_pic_order_cnt = 0;
      } else if (!picture->nal_ref_idc) {
        temp_pic_order_cnt =
            2 * (picture->frame_num_offset + picture->frame_num) - 1;
      } else {
        temp_pic_order_cnt =
            2 * (picture->frame_num_offset + picture->frame_num);
      }

      if (picture->field == GST_H264_PICTURE_FIELD_FRAME) {
        picture->top_field_order_cnt = temp_pic_order_cnt;
        picture->bottom_field_order_cnt = temp_pic_order_cnt;
      } else if (picture->field == GST_H264_PICTURE_FIELD_BOTTOM_FIELD) {
        picture->bottom_field_order_cnt = temp_pic_order_cnt;
      } else {
        picture->top_field_order_cnt = temp_pic_order_cnt;
      }
      break;
    }

    default:
      GST_WARNING_OBJECT (self,
          "Invalid pic_order_cnt_type: %d", sps->pic_order_cnt_type);
      return FALSE;
  }

  switch (picture->field) {
    case GST_H264_PICTURE_FIELD_FRAME:
      picture->pic_order_cnt =
          MIN (picture->top_field_order_cnt, picture->bottom_field_order_cnt);
      break;
    case GST_H264_PICTURE_FILED_TOP_FIELD:
      picture->pic_order_cnt = picture->top_field_order_cnt;
      break;
    case GST_H264_PICTURE_FIELD_BOTTOM_FIELD:
      picture->pic_order_cnt = picture->bottom_field_order_cnt;
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static void
gst_h264_decoder_do_output_picture (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264DecoderClass *klass;

  picture->outputted = TRUE;

  if (picture->nonexisting) {
    GST_DEBUG_OBJECT (self, "Skipping output, non-existing frame_num %d",
        picture->frame_num);
    return;
  }

  if (picture->pic_order_cnt < priv->last_output_poc) {
    GST_WARNING_OBJECT (self,
        "Outputting out of order %d -> %d, likely a broken stream",
        priv->last_output_poc, picture->pic_order_cnt);
  }

  priv->last_output_poc = picture->pic_order_cnt;

  klass = GST_H264_DECODER_GET_CLASS (self);

  if (klass->output_picture)
    priv->last_ret = klass->output_picture (self, picture);
}

static gboolean
gst_h264_decoder_finish_current_picture (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264DecoderClass *klass;
  gboolean ret = TRUE;

  if (!priv->current_picture)
    return TRUE;

  klass = GST_H264_DECODER_GET_CLASS (self);

  if (klass->end_picture)
    ret = klass->end_picture (self, priv->current_picture);

  /* finish picture takes ownership of the picture */
  ret = gst_h264_decoder_finish_picture (self, priv->current_picture);
  priv->current_picture = NULL;

  if (!ret) {
    GST_ERROR_OBJECT (self, "Failed to finish picture");
    return FALSE;
  }

  return TRUE;
}

static gint
poc_asc_compare (const GstH264Picture * a, const GstH264Picture * b)
{
  return a->pic_order_cnt > b->pic_order_cnt;
}

static gboolean
gst_h264_decoder_output_all_remaining_pics (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  GList *to_output = NULL;
  GList *iter;

  gst_h264_dpb_get_pictures_not_outputted (priv->dpb, &to_output);

  to_output = g_list_sort (to_output, (GCompareFunc) poc_asc_compare);

  for (iter = to_output; iter; iter = g_list_next (iter)) {
    GstH264Picture *picture = (GstH264Picture *) iter->data;

    GST_LOG_OBJECT (self, "Output picture %p (frame num %d, poc %d)", picture,
        picture->frame_num, picture->pic_order_cnt);
    gst_h264_decoder_do_output_picture (self, picture);
  }

  if (to_output)
    g_list_free_full (to_output, (GDestroyNotify) gst_h264_picture_unref);

  return TRUE;
}

static gboolean
gst_h264_decoder_handle_memory_management_opt (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  gint i;

  for (i = 0; i < G_N_ELEMENTS (picture->dec_ref_pic_marking.ref_pic_marking);
      i++) {
    GstH264RefPicMarking *ref_pic_marking =
        &picture->dec_ref_pic_marking.ref_pic_marking[i];
    GstH264Picture *to_mark;
    gint pic_num_x;

    switch (ref_pic_marking->memory_management_control_operation) {
      case 0:
        /* Normal end of operations' specification */
        return TRUE;
      case 1:
        /* Mark a short term reference picture as unused so it can be removed
         * if outputted */
        pic_num_x =
            picture->pic_num - (ref_pic_marking->difference_of_pic_nums_minus1 +
            1);
        to_mark = gst_h264_dpb_get_short_ref_by_pic_num (priv->dpb, pic_num_x);
        if (to_mark) {
          to_mark->ref = FALSE;
          gst_h264_picture_unref (to_mark);
        } else {
          GST_WARNING_OBJECT (self, "Invalid short term ref pic num to unmark");
          return FALSE;
        }
        break;

      case 2:
        /* Mark a long term reference picture as unused so it can be removed
         * if outputted */
        to_mark = gst_h264_dpb_get_long_ref_by_pic_num (priv->dpb,
            ref_pic_marking->long_term_pic_num);
        if (to_mark) {
          to_mark->ref = FALSE;
          gst_h264_picture_unref (to_mark);
        } else {
          GST_WARNING_OBJECT (self, "Invalid long term ref pic num to unmark");
          return FALSE;
        }
        break;

      case 3:
        /* Mark a short term reference picture as long term reference */
        pic_num_x =
            picture->pic_num - (ref_pic_marking->difference_of_pic_nums_minus1 +
            1);
        to_mark = gst_h264_dpb_get_short_ref_by_pic_num (priv->dpb, pic_num_x);
        if (to_mark) {
          to_mark->long_term = TRUE;
          to_mark->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
          gst_h264_picture_unref (to_mark);
        } else {
          GST_WARNING_OBJECT (self,
              "Invalid short term ref pic num to mark as long ref");
          return FALSE;
        }
        break;

      case 4:{
        GList *long_terms = NULL;
        GList *iter;

        /* Unmark all reference pictures with long_term_frame_idx over new max */
        priv->max_long_term_frame_idx =
            ref_pic_marking->max_long_term_frame_idx_plus1 - 1;

        gst_h264_dpb_get_pictures_long_term_ref (priv->dpb, &long_terms);

        for (iter = long_terms; iter; iter = g_list_next (iter)) {
          GstH264Picture *long_term_picture = (GstH264Picture *) iter->data;
          if (long_term_picture->long_term_frame_idx >
              priv->max_long_term_frame_idx)
            long_term_picture->ref = FALSE;
        }

        if (long_terms)
          g_list_free_full (long_terms,
              (GDestroyNotify) gst_h264_picture_unref);
        break;
      }

      case 5:
        /* Unmark all reference pictures */
        gst_h264_dpb_mark_all_non_ref (priv->dpb);
        priv->max_long_term_frame_idx = -1;
        picture->mem_mgmt_5 = TRUE;
        break;

      case 6:{
        /* Replace long term reference pictures with current picture.
         * First unmark if any existing with this long_term_frame_idx... */
        GList *long_terms = NULL;
        GList *iter;

        gst_h264_dpb_get_pictures_long_term_ref (priv->dpb, &long_terms);

        for (iter = long_terms; iter; iter = g_list_next (iter)) {
          GstH264Picture *long_term_picture = (GstH264Picture *) iter->data;

          if (long_term_picture->long_term_frame_idx ==
              ref_pic_marking->long_term_frame_idx)
            long_term_picture->ref = FALSE;
        }

        if (long_terms)
          g_list_free_full (long_terms,
              (GDestroyNotify) gst_h264_picture_unref);

        /* and mark the current one instead */
        picture->ref = TRUE;
        picture->long_term = TRUE;
        picture->long_term_frame_idx = ref_pic_marking->long_term_frame_idx;
        break;
      }

      default:
        g_assert_not_reached ();
        break;
    }
  }

  return TRUE;
}

static gboolean
gst_h264_decoder_sliding_window_picture_marking (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  const GstH264SPS *sps = priv->active_sps;
  gint num_ref_pics;
  gint max_num_ref_frames;

  if (!sps) {
    GST_ERROR_OBJECT (self, "No active sps");
    return FALSE;
  }

  /* 8.2.5.3. Ensure the DPB doesn't overflow by discarding the oldest picture */
  num_ref_pics = gst_h264_dpb_num_ref_pictures (priv->dpb);
  max_num_ref_frames = MAX (1, sps->num_ref_frames);

  if (num_ref_pics > max_num_ref_frames) {
    GST_WARNING_OBJECT (self,
        "num_ref_pics %d is larger than allowed maximum %d",
        num_ref_pics, max_num_ref_frames);
    return FALSE;
  }

  if (num_ref_pics == max_num_ref_frames) {
    /* Max number of reference pics reached, need to remove one of the short
     * term ones. Find smallest frame_num_wrap short reference picture and mark
     * it as unused */
    GstH264Picture *to_unmark =
        gst_h264_dpb_get_lowest_frame_num_short_ref (priv->dpb);

    if (!to_unmark) {
      GST_WARNING_OBJECT (self, "Could not find a short ref picture to unmark");
      return FALSE;
    }

    to_unmark->ref = FALSE;
    gst_h264_picture_unref (to_unmark);
  }

  return TRUE;
}

/* This method ensures that DPB does not overflow, either by removing
 * reference pictures as specified in the stream, or using a sliding window
 * procedure to remove the oldest one.
 * It also performs marking and unmarking pictures as reference.
 * See spac 8.2.5.1 */
static gboolean
gst_h264_decoder_reference_picture_marking (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;

  /* If the current picture is an IDR, all reference pictures are unmarked */
  if (picture->idr) {
    gst_h264_dpb_mark_all_non_ref (priv->dpb);

    if (picture->dec_ref_pic_marking.long_term_reference_flag) {
      picture->long_term = TRUE;
      picture->long_term_frame_idx = 0;
      priv->max_long_term_frame_idx = 0;
    } else {
      picture->long_term = FALSE;
      priv->max_long_term_frame_idx = -1;
    }

    return TRUE;
  }

  /* Not an IDR. If the stream contains instructions on how to discard pictures
   * from DPB and how to mark/unmark existing reference pictures, do so.
   * Otherwise, fall back to default sliding window process */
  if (picture->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
    return gst_h264_decoder_handle_memory_management_opt (self, picture);
  }

  return gst_h264_decoder_sliding_window_picture_marking (self);
}

static gboolean
gst_h264_decoder_finish_picture (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  GList *not_outputted = NULL;
  guint num_remaining;
  GList *iter;
#ifndef GST_DISABLE_GST_DEBUG
  gint i;
#endif

  /* Finish processing the picture.
   * Start by storing previous picture data for later use */
  if (picture->ref) {
    gst_h264_decoder_reference_picture_marking (self, picture);
    priv->prev_ref_has_memmgmnt5 = picture->mem_mgmt_5;
    priv->prev_ref_top_field_order_cnt = picture->top_field_order_cnt;
    priv->prev_ref_pic_order_cnt_msb = picture->pic_order_cnt_msb;
    priv->prev_ref_pic_order_cnt_lsb = picture->pic_order_cnt_lsb;
    priv->prev_ref_field = picture->field;
    priv->prev_ref_frame_num = picture->frame_num;
  }

  priv->prev_frame_num = picture->frame_num;
  priv->prev_has_memmgmnt5 = picture->mem_mgmt_5;
  priv->prev_frame_num_offset = picture->frame_num_offset;

  /* Remove unused (for reference or later output) pictures from DPB, marking
   * them as such */
  gst_h264_dpb_delete_unused (priv->dpb);

  GST_LOG_OBJECT (self,
      "Finishing picture %p (frame_num %d, poc %d), entries in DPB %d",
      picture, picture->frame_num, picture->pic_order_cnt,
      gst_h264_dpb_get_size (priv->dpb));

  /* The ownership of pic will either be transferred to DPB - if the picture is
   * still needed (for output and/or reference) - or we will release it
   * immediately if we manage to output it here and won't have to store it for
   * future reference */

  /* Get all pictures that haven't been outputted yet */
  gst_h264_dpb_get_pictures_not_outputted (priv->dpb, &not_outputted);
  /* Include the one we've just decoded */
  not_outputted = g_list_append (not_outputted, picture);

  /* for debugging */
#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self, "Before sorting not outputted list");
  i = 0;
  for (iter = not_outputted; iter; iter = g_list_next (iter)) {
    GstH264Picture *tmp = (GstH264Picture *) iter->data;

    GST_TRACE_OBJECT (self,
        "\t%dth picture %p (poc %d)", i, tmp, tmp->pic_order_cnt);
    i++;
  }
#endif

  /* Sort in output order */
  not_outputted = g_list_sort (not_outputted, (GCompareFunc) poc_asc_compare);

#ifndef GST_DISABLE_GST_DEBUG
  GST_TRACE_OBJECT (self,
      "After sorting not outputted list in poc ascending order");
  i = 0;
  for (iter = not_outputted; iter; iter = g_list_next (iter)) {
    GstH264Picture *tmp = (GstH264Picture *) iter->data;

    GST_TRACE_OBJECT (self,
        "\t%dth picture %p (poc %d)", i, tmp, tmp->pic_order_cnt);
    i++;
  }
#endif

  /* Try to output as many pictures as we can. A picture can be output,
   * if the number of decoded and not yet outputted pictures that would remain
   * in DPB afterwards would at least be equal to max_num_reorder_frames.
   * If the outputted picture is not a reference picture, it doesn't have
   * to remain in the DPB and can be removed */
  iter = not_outputted;
  num_remaining = g_list_length (not_outputted);

  while (num_remaining > priv->max_num_reorder_frames ||
      /* If the condition below is used, this is an invalid stream. We should
       * not be forced to output beyond max_num_reorder_frames in order to
       * make room in DPB to store the current picture (if we need to do so).
       * However, if this happens, ignore max_num_reorder_frames and try
       * to output more. This may cause out-of-order output, but is not
       * fatal, and better than failing instead */
      ((gst_h264_dpb_is_full (priv->dpb) && (!picture->outputted
                  || picture->ref))
          && num_remaining)) {
    GstH264Picture *to_output = (GstH264Picture *) iter->data;

    if (num_remaining <= priv->max_num_reorder_frames) {
      GST_WARNING_OBJECT (self,
          "Invalid stream, max_num_reorder_frames not preserved");
    }

    GST_LOG_OBJECT (self,
        "Output picture %p (frame num %d)", to_output, to_output->frame_num);
    gst_h264_decoder_do_output_picture (self, to_output);
    if (!to_output->ref) {
      /* Current picture hasn't been inserted into DPB yet, so don't remove it
       * if we managed to output it immediately */
      gint outputted_poc = to_output->pic_order_cnt;
      if (outputted_poc != picture->pic_order_cnt)
        gst_h264_dpb_delete_by_poc (priv->dpb, outputted_poc);
    }

    iter = g_list_next (iter);
    num_remaining--;
  }

  /* If we haven't managed to output the picture that we just decoded, or if
   * it's a reference picture, we have to store it in DPB */
  if (!picture->outputted || picture->ref) {
    if (gst_h264_dpb_is_full (priv->dpb)) {
      /* If we haven't managed to output anything to free up space in DPB
       * to store this picture, it's an error in the stream */
      GST_WARNING_OBJECT (self, "Could not free up space in DPB");
      return FALSE;
    }

    GST_TRACE_OBJECT (self,
        "Put picture %p (outputted %d, ref %d, frame num %d, poc %d) to dpb",
        picture, picture->outputted, picture->ref, picture->frame_num,
        picture->pic_order_cnt);
    gst_h264_dpb_add (priv->dpb, gst_h264_picture_ref (picture));
  }

  if (not_outputted)
    g_list_free_full (not_outputted, (GDestroyNotify) gst_h264_picture_unref);

  return TRUE;
}

static gboolean
gst_h264_decoder_update_max_num_reorder_frames (GstH264Decoder * self,
    GstH264SPS * sps)
{
  GstH264DecoderPrivate *priv = self->priv;

  if (sps->vui_parameters_present_flag
      && sps->vui_parameters.bitstream_restriction_flag) {
    priv->max_num_reorder_frames = sps->vui_parameters.num_reorder_frames;
    if (priv->max_num_reorder_frames >
        gst_h264_dpb_get_max_num_pics (priv->dpb)) {
      GST_WARNING
          ("max_num_reorder_frames present, but larger than MaxDpbFrames (%d > %d)",
          (gint) priv->max_num_reorder_frames,
          gst_h264_dpb_get_max_num_pics (priv->dpb));

      priv->max_num_reorder_frames = 0;
      return FALSE;
    }

    return TRUE;
  }

  /* max_num_reorder_frames not present, infer from profile/constraints
   * (see VUI semantics in spec) */
  if (sps->constraint_set3_flag) {
    switch (sps->profile_idc) {
      case 44:
      case 86:
      case 100:
      case 110:
      case 122:
      case 244:
        priv->max_num_reorder_frames = 0;
        break;
      default:
        priv->max_num_reorder_frames =
            gst_h264_dpb_get_max_num_pics (priv->dpb);
        break;
    }
  } else {
    priv->max_num_reorder_frames = gst_h264_dpb_get_max_num_pics (priv->dpb);
  }

  return TRUE;
}

typedef enum
{
  GST_H264_LEVEL_L1 = 10,
  GST_H264_LEVEL_L1B = 9,
  GST_H264_LEVEL_L1_1 = 11,
  GST_H264_LEVEL_L1_2 = 12,
  GST_H264_LEVEL_L1_3 = 13,
  GST_H264_LEVEL_L2_0 = 20,
  GST_H264_LEVEL_L2_1 = 21,
  GST_H264_LEVEL_L2_2 = 22,
  GST_H264_LEVEL_L3 = 30,
  GST_H264_LEVEL_L3_1 = 31,
  GST_H264_LEVEL_L3_2 = 32,
  GST_H264_LEVEL_L4 = 40,
  GST_H264_LEVEL_L4_1 = 41,
  GST_H264_LEVEL_L4_2 = 42,
  GST_H264_LEVEL_L5 = 50,
  GST_H264_LEVEL_L5_1 = 51,
  GST_H264_LEVEL_L5_2 = 52,
  GST_H264_LEVEL_L6 = 60,
  GST_H264_LEVEL_L6_1 = 61,
  GST_H264_LEVEL_L6_2 = 62,
} GstD3D11H264Level;

typedef struct
{
  GstD3D11H264Level level;

  guint32 max_mbps;
  guint32 max_fs;
  guint32 max_dpb_mbs;
  guint32 max_main_br;
} LevelLimits;

static const LevelLimits level_limits_map[] = {
  {GST_H264_LEVEL_L1, 1485, 99, 396, 64},
  {GST_H264_LEVEL_L1B, 1485, 99, 396, 128},
  {GST_H264_LEVEL_L1_1, 3000, 396, 900, 192},
  {GST_H264_LEVEL_L1_2, 6000, 396, 2376, 384},
  {GST_H264_LEVEL_L1_3, 11800, 396, 2376, 768},
  {GST_H264_LEVEL_L2_0, 11880, 396, 2376, 2000},
  {GST_H264_LEVEL_L2_1, 19800, 792, 4752, 4000},
  {GST_H264_LEVEL_L2_2, 20250, 1620, 8100, 4000},
  {GST_H264_LEVEL_L3, 40500, 1620, 8100, 10000},
  {GST_H264_LEVEL_L3_1, 108000, 3600, 18000, 14000},
  {GST_H264_LEVEL_L3_2, 216000, 5120, 20480, 20000},
  {GST_H264_LEVEL_L4, 245760, 8192, 32768, 20000},
  {GST_H264_LEVEL_L4_1, 245760, 8192, 32768, 50000},
  {GST_H264_LEVEL_L4_2, 522240, 8704, 34816, 50000},
  {GST_H264_LEVEL_L5, 589824, 22080, 110400, 135000},
  {GST_H264_LEVEL_L5_1, 983040, 36864, 184320, 240000},
  {GST_H264_LEVEL_L5_2, 2073600, 36864, 184320, 240000},
  {GST_H264_LEVEL_L6, 4177920, 139264, 696320, 240000},
  {GST_H264_LEVEL_L6_1, 8355840, 139264, 696320, 480000},
  {GST_H264_LEVEL_L6_2, 16711680, 139264, 696320, 800000}
};

static gint
h264_level_to_max_dpb_mbs (GstD3D11H264Level level)
{
  gint i;
  for (i = 0; i < G_N_ELEMENTS (level_limits_map); i++) {
    if (level == level_limits_map[i].level)
      return level_limits_map[i].max_dpb_mbs;
  }

  return 0;
}

static gboolean
gst_h264_decoder_process_sps (GstH264Decoder * self, GstH264SPS * sps)
{
  GstH264DecoderPrivate *priv = self->priv;
  guint8 level;
  gint max_dpb_mbs;
  gint width_mb, height_mb;
  gint max_dpb_frames;
  gint max_dpb_size;
  gint prev_max_dpb_size;

  if (sps->frame_mbs_only_flag == 0) {
    GST_FIXME_OBJECT (self, "frame_mbs_only_flag != 1 not supported");
    return FALSE;
  }

  /* Spec A.3.1 and A.3.2
   * For Baseline, Constrained Baseline and Main profile, the indicated level is
   * Level 1b if level_idc is equal to 11 and constraint_set3_flag is equal to 1
   */
  level = sps->level_idc;
  if (level == 11 && (sps->profile_idc == 66 || sps->profile_idc == 77) &&
      sps->constraint_set3_flag) {
    /* Leel 1b */
    level = 9;
  }

  max_dpb_mbs = h264_level_to_max_dpb_mbs ((GstD3D11H264Level) level);
  if (!max_dpb_mbs)
    return FALSE;

  width_mb = sps->width / 16;
  height_mb = sps->height / 16;

  max_dpb_frames = MIN (max_dpb_mbs / (width_mb * height_mb),
      GST_H264_DPB_MAX_SIZE);

  max_dpb_size = MAX (max_dpb_frames,
      MAX (sps->num_ref_frames, sps->vui_parameters.max_dec_frame_buffering));

  prev_max_dpb_size = gst_h264_dpb_get_max_num_pics (priv->dpb);
  if (priv->width != sps->width || priv->height != sps->height ||
      prev_max_dpb_size != max_dpb_size) {
    GstH264DecoderClass *klass = GST_H264_DECODER_GET_CLASS (self);

    GST_DEBUG_OBJECT (self,
        "SPS updated, resolution: %dx%d -> %dx%d, dpb size: %d -> %d",
        priv->width, priv->height, sps->width, sps->height,
        prev_max_dpb_size, max_dpb_size);

    if (gst_h264_decoder_drain (GST_VIDEO_DECODER (self)) != GST_FLOW_OK)
      return FALSE;

    g_assert (klass->new_sequence);

    if (!klass->new_sequence (self, sps, max_dpb_size)) {
      GST_ERROR_OBJECT (self, "subclass does not want accept new sequence");
      return FALSE;
    }

    priv->width = sps->width;
    priv->height = sps->height;

    gst_h264_dpb_set_max_num_pics (priv->dpb, max_dpb_size);
  }

  GST_DEBUG_OBJECT (self, "Set DPB max size %d", max_dpb_size);

  return gst_h264_decoder_update_max_num_reorder_frames (self, sps);
}

static gboolean
gst_h264_decoder_init_gap_picture (GstH264Decoder * self,
    GstH264Picture * picture, gint frame_num)
{
  picture->nonexisting = TRUE;
  picture->nal_ref_idc = 1;
  picture->frame_num = picture->pic_num = frame_num;
  picture->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag = FALSE;
  picture->ref = TRUE;
  picture->dec_ref_pic_marking.long_term_reference_flag = FALSE;
  picture->field = GST_H264_PICTURE_FIELD_FRAME;

  return gst_h264_decoder_calculate_poc (self, picture);
}

static gboolean
gst_h264_decoder_decode_slice (GstH264Decoder * self)
{
  GstH264DecoderClass *klass = GST_H264_DECODER_GET_CLASS (self);
  GstH264DecoderPrivate *priv = self->priv;
  GstH264Slice *slice = &priv->current_slice;
  GstH264Picture *picture = priv->current_picture;

  if (!picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    return FALSE;
  }

  if (slice->header.field_pic_flag == 0)
    priv->max_pic_num = priv->max_frame_num;
  else
    priv->max_pic_num = 2 * priv->max_frame_num;

  g_assert (klass->decode_slice);

  return klass->decode_slice (self, picture, slice);
}
