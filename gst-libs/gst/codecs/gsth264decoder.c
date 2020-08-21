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
/**
 * SECTION:gsth264decoder
 * @title: GstH264Decoder
 * @short_description: Base class to implement stateless H.264 decoders
 * @sources:
 * - gsth264picture.h
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
  /* used for low-latency vs. high throughput mode decision */
  gboolean is_live;

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

  gboolean process_ref_pic_lists;

  /* Reference picture lists, constructed for each frame */
  GArray *ref_pic_list_p0;
  GArray *ref_pic_list_b0;
  GArray *ref_pic_list_b1;

  /* Reference picture lists, constructed for each slice */
  GArray *ref_pic_list0;
  GArray *ref_pic_list1;

  /* Cached array to handle pictures to be outputed */
  GArray *to_output;
};

#define parent_class gst_h264_decoder_parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstH264Decoder, gst_h264_decoder,
    GST_TYPE_VIDEO_DECODER,
    G_ADD_PRIVATE (GstH264Decoder);
    GST_DEBUG_CATEGORY_INIT (gst_h264_decoder_debug, "h264decoder", 0,
        "H.264 Video Decoder"));

static void gst_h264_decoder_finalize (GObject * object);

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
static gboolean gst_h264_decoder_drain_internal (GstH264Decoder * self);
static gboolean gst_h264_decoder_finish_current_picture (GstH264Decoder * self);
static gboolean gst_h264_decoder_finish_picture (GstH264Decoder * self,
    GstH264Picture * picture);
static void gst_h264_decoder_prepare_ref_pic_lists (GstH264Decoder * self);
static void gst_h264_decoder_clear_ref_pic_lists (GstH264Decoder * self);
static gboolean gst_h264_decoder_modify_ref_pic_lists (GstH264Decoder * self);

static void
gst_h264_decoder_class_init (GstH264DecoderClass * klass)
{
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = GST_DEBUG_FUNCPTR (gst_h264_decoder_finalize);

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
  GstH264DecoderPrivate *priv;

  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->priv = priv = gst_h264_decoder_get_instance_private (self);

  priv->ref_pic_list_p0 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 32);
  g_array_set_clear_func (priv->ref_pic_list_p0,
      (GDestroyNotify) gst_h264_picture_clear);

  priv->ref_pic_list_b0 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 32);
  g_array_set_clear_func (priv->ref_pic_list_b0,
      (GDestroyNotify) gst_h264_picture_clear);

  priv->ref_pic_list_b1 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 32);
  g_array_set_clear_func (priv->ref_pic_list_b1,
      (GDestroyNotify) gst_h264_picture_clear);

  priv->ref_pic_list0 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 32);
  priv->ref_pic_list1 = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 32);

  priv->to_output = g_array_sized_new (FALSE, TRUE,
      sizeof (GstH264Picture *), 16);
  g_array_set_clear_func (priv->to_output,
      (GDestroyNotify) gst_h264_picture_clear);
}

static void
gst_h264_decoder_finalize (GObject * object)
{
  GstH264Decoder *self = GST_H264_DECODER (object);
  GstH264DecoderPrivate *priv = self->priv;

  g_array_unref (priv->ref_pic_list_p0);
  g_array_unref (priv->ref_pic_list_b0);
  g_array_unref (priv->ref_pic_list_b1);
  g_array_unref (priv->ref_pic_list0);
  g_array_unref (priv->ref_pic_list1);
  g_array_unref (priv->to_output);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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

  gst_h264_decoder_clear_ref_pic_lists (self);
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
  /* dpb will be cleared by this method */
  gst_h264_decoder_drain_internal (self);

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

  if (!decode_ret) {
    GST_VIDEO_DECODER_ERROR (self, 1, STREAM, DECODE,
        ("Failed to decode data"), (NULL), priv->last_ret);
    gst_video_decoder_drop_frame (decoder, frame);

    gst_h264_picture_clear (&priv->current_picture);
    priv->current_frame = NULL;

    return priv->last_ret;
  }

  gst_h264_decoder_finish_current_picture (self);
  gst_video_codec_frame_unref (frame);
  priv->current_frame = NULL;

  return priv->last_ret;
}

static gboolean
gst_h264_decoder_parse_sps (GstH264Decoder * self, GstH264NalUnit * nalu)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264SPS sps;
  GstH264ParserResult pres;
  gboolean ret;

  pres = gst_h264_parse_sps (nalu, &sps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse SPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "SPS parsed");

  ret = gst_h264_decoder_process_sps (self, &sps);
  if (!ret) {
    GST_WARNING_OBJECT (self, "Failed to process SPS");
  } else if (gst_h264_parser_update_sps (priv->parser,
          &sps) != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update SPS");
    ret = FALSE;
  }

  gst_h264_sps_clear (&sps);

  return ret;
}

static gboolean
gst_h264_decoder_parse_pps (GstH264Decoder * self, GstH264NalUnit * nalu)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264PPS pps;
  GstH264ParserResult pres;
  gboolean ret = TRUE;

  pres = gst_h264_parse_pps (priv->parser, nalu, &pps);
  if (pres != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to parse PPS, result %d", pres);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "PPS parsed");

  if (pps.num_slice_groups_minus1 > 0) {
    GST_FIXME_OBJECT (self, "FMO is not supported");
    ret = FALSE;
  } else if (gst_h264_parser_update_pps (priv->parser, &pps)
      != GST_H264_PARSER_OK) {
    GST_WARNING_OBJECT (self, "Failed to update PPS");
    ret = FALSE;
  }

  gst_h264_pps_clear (&pps);

  return ret;
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

    if (!gst_h264_decoder_parse_sps (self, &nalu)) {
      GST_WARNING_OBJECT (self, "Failed to parse SPS");
      return FALSE;
    }
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

    if (!gst_h264_decoder_parse_pps (self, &nalu)) {
      GST_WARNING_OBJECT (self, "Failed to parse PPS");
      return FALSE;
    }
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
    /* This is likely the case where some frames were dropped.
     * then we need to keep decoding without error out */
    GST_WARNING_OBJECT (self, "Invalid frame num %d", frame_num);
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

  priv->max_frame_num = sps->max_frame_num;
  frame_num = priv->current_slice.header.frame_num;
  if (priv->current_slice.nalu.idr_pic_flag)
    priv->prev_ref_frame_num = 0;

  /* 7.4.3 */
  if (frame_num != priv->prev_ref_frame_num &&
      frame_num != (priv->prev_ref_frame_num + 1) % priv->max_frame_num &&
      gst_h264_dpb_get_size (priv->dpb) > 0) {
    if (!gst_h264_decoder_handle_frame_num_gap (self, frame_num))
      return FALSE;
  }

  if (!gst_h264_decoder_init_current_picture (self))
    return FALSE;

  gst_h264_decoder_update_pic_nums (self, frame_num);

  if (priv->process_ref_pic_lists)
    gst_h264_decoder_prepare_ref_pic_lists (self);

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
    /* This allows accessing the frame from the picture. */
    picture->system_frame_number = priv->current_frame->system_frame_number;

    priv->current_picture = picture;
    g_assert (priv->current_frame);

    if (klass->new_picture)
      ret = klass->new_picture (self, priv->current_frame, picture);

    if (!ret) {
      GST_ERROR_OBJECT (self, "subclass does not want accept new picture");
      priv->current_picture = NULL;
      gst_h264_picture_unref (picture);
      return FALSE;
    }

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
  GstQuery *query;

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
    if (!gst_h264_decoder_parse_codec_data (self, map.data, map.size)) {
      /* keep going without error.
       * Probably inband SPS/PPS might be valid data */
      GST_WARNING_OBJECT (self, "Failed to handle codec data");
    }
    gst_buffer_unmap (priv->codec_data, &map);
  }

  /* in case live streaming, we will run on low-latency mode */
  priv->is_live = FALSE;
  query = gst_query_new_latency ();
  if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (self), query))
    gst_query_parse_latency (query, &priv->is_live, NULL, NULL);
  gst_query_unref (query);

  if (priv->is_live)
    GST_DEBUG_OBJECT (self, "Live source, will run on low-latency mode");

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
  picture->dec_ref_pic_marking = slice_hdr->dec_ref_pic_marking;
  if (picture->idr)
    picture->idr_pic_id = slice_hdr->idr_pic_id;

  if (slice_hdr->field_pic_flag)
    picture->field =
        slice_hdr->bottom_field_flag ?
        GST_H264_PICTURE_FIELD_BOTTOM_FIELD : GST_H264_PICTURE_FIELD_TOP_FIELD;
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

      if (picture->field != GST_H264_PICTURE_FIELD_TOP_FIELD) {
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
    case GST_H264_PICTURE_FIELD_TOP_FIELD:
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
    GstH264Picture * picture, gboolean clear_dpb)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264DecoderClass *klass;
  GstVideoCodecFrame *frame = NULL;

  picture->outputted = TRUE;

  if (clear_dpb && !picture->ref)
    gst_h264_dpb_delete_by_poc (priv->dpb, picture->pic_order_cnt);

  if (picture->nonexisting) {
    GST_DEBUG_OBJECT (self, "Skipping output, non-existing frame_num %d",
        picture->frame_num);
    gst_h264_picture_unref (picture);
    return;
  }

  GST_LOG_OBJECT (self, "Outputting picture %p (frame_num %d, poc %d)",
      picture, picture->frame_num, picture->pic_order_cnt);

  if (picture->pic_order_cnt < priv->last_output_poc) {
    GST_WARNING_OBJECT (self,
        "Outputting out of order %d -> %d, likely a broken stream",
        priv->last_output_poc, picture->pic_order_cnt);
  }

  priv->last_output_poc = picture->pic_order_cnt;

  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (self),
      picture->system_frame_number);

  if (!frame) {
    GST_ERROR_OBJECT (self,
        "No available codec frame with frame number %d",
        picture->system_frame_number);
    priv->last_ret = GST_FLOW_ERROR;
    gst_h264_picture_unref (picture);

    return;
  }

  klass = GST_H264_DECODER_GET_CLASS (self);

  g_assert (klass->output_picture);
  priv->last_ret = klass->output_picture (self, frame, picture);
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

  if (klass->end_picture) {
    if (!klass->end_picture (self, priv->current_picture)) {
      GST_WARNING_OBJECT (self,
          "end picture failed, marking picture %p non-existing "
          "(frame_num %d, poc %d)", priv->current_picture,
          priv->current_picture->frame_num,
          priv->current_picture->pic_order_cnt);
      priv->current_picture->nonexisting = TRUE;

      /* this fake nonexisting picture will not trigger ouput_picture() */
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self),
          gst_video_codec_frame_ref (priv->current_frame));
    }
  }

  /* We no longer need the per frame reference lists */
  gst_h264_decoder_clear_ref_pic_lists (self);

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
poc_asc_compare (const GstH264Picture ** a, const GstH264Picture ** b)
{
  return (*a)->pic_order_cnt - (*b)->pic_order_cnt;
}

static gint
poc_desc_compare (const GstH264Picture ** a, const GstH264Picture ** b)
{
  return (*b)->pic_order_cnt - (*a)->pic_order_cnt;
}

static gboolean
gst_h264_decoder_drain_internal (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  GArray *to_output = priv->to_output;

  /* We are around to drain, so we can get rist of everything that has been
   * outputed already */
  gst_h264_dpb_delete_outputed (priv->dpb);
  gst_h264_dpb_get_pictures_not_outputted (priv->dpb, to_output);
  g_array_sort (to_output, (GCompareFunc) poc_asc_compare);

  while (to_output->len) {
    GstH264Picture *picture = g_array_index (to_output, GstH264Picture *, 0);

    /* We want the last reference when outputing so take a ref and then remove
     * from both arrays. */
    gst_h264_picture_ref (picture);
    g_array_remove_index (to_output, 0);
    gst_h264_dpb_delete_by_poc (priv->dpb, picture->pic_order_cnt);

    GST_LOG_OBJECT (self, "Output picture %p (frame num %d, poc %d)", picture,
        picture->frame_num, picture->pic_order_cnt);
    gst_h264_decoder_do_output_picture (self, picture, FALSE);
  }

  g_array_set_size (to_output, 0);
  gst_h264_dpb_clear (priv->dpb);
  priv->last_output_poc = 0;
  return TRUE;
}

static gboolean
gst_h264_decoder_handle_memory_management_opt (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  gint i, j;

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
        } else {
          GST_WARNING_OBJECT (self,
              "Invalid short term ref pic num to mark as long ref");
          return FALSE;
        }
        break;

      case 4:{
        GArray *pictures = gst_h264_dpb_get_pictures_all (priv->dpb);

        /* Unmark all reference pictures with long_term_frame_idx over new max */
        priv->max_long_term_frame_idx =
            ref_pic_marking->max_long_term_frame_idx_plus1 - 1;

        for (j = 0; j < pictures->len; j++) {
          GstH264Picture *pic = g_array_index (pictures, GstH264Picture *, j);
          if (pic->long_term &&
              pic->long_term_frame_idx > priv->max_long_term_frame_idx)
            pic->ref = FALSE;
        }

        g_array_unref (pictures);
        break;
      }

      case 5:
        /* Unmark all reference pictures */
        gst_h264_dpb_mark_all_non_ref (priv->dpb);
        priv->max_long_term_frame_idx = -1;
        picture->mem_mgmt_5 = TRUE;
        break;

      case 6:{
        GArray *pictures = gst_h264_dpb_get_pictures_all (priv->dpb);

        /* Replace long term reference pictures with current picture.
         * First unmark if any existing with this long_term_frame_idx... */

        for (j = 0; j < pictures->len; j++) {
          GstH264Picture *pic = g_array_index (pictures, GstH264Picture *, j);

          if (pic->long_term &&
              pic->long_term_frame_idx == ref_pic_marking->long_term_frame_idx)
            pic->ref = FALSE;
        }

        g_array_unref (pictures);

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

    GST_TRACE_OBJECT (self,
        "Unmark reference flag of picture %p (frame_num %d, poc %d)",
        to_unmark, to_unmark->frame_num, to_unmark->pic_order_cnt);

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
    if (picture->nonexisting) {
      GST_WARNING_OBJECT (self,
          "Invalid memory management operation for non-existing picture "
          "%p (frame_num %d, poc %d", picture, picture->frame_num,
          picture->pic_order_cnt);
    }

    return gst_h264_decoder_handle_memory_management_opt (self, picture);
  }

  return gst_h264_decoder_sliding_window_picture_marking (self);
}

static gboolean
gst_h264_decoder_finish_picture (GstH264Decoder * self,
    GstH264Picture * picture)
{
  GstH264DecoderPrivate *priv = self->priv;
  GArray *not_outputted = priv->to_output;
  guint num_remaining;
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
  gst_h264_dpb_get_pictures_not_outputted (priv->dpb, not_outputted);
  /* Include the one we've just decoded */
  g_array_append_val (not_outputted, picture);

  /* for debugging */
#ifndef GST_DISABLE_GST_DEBUG
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    GST_TRACE_OBJECT (self, "Before sorting not outputted list");
    for (i = 0; i < not_outputted->len; i++) {
      GstH264Picture *tmp = g_array_index (not_outputted, GstH264Picture *, i);
      GST_TRACE_OBJECT (self,
          "\t%dth picture %p (frame_num %d, poc %d)", i, tmp,
          tmp->frame_num, tmp->pic_order_cnt);
    }
  }
#endif

  /* Sort in output order */
  g_array_sort (not_outputted, (GCompareFunc) poc_asc_compare);

#ifndef GST_DISABLE_GST_DEBUG
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_TRACE) {
    GST_TRACE_OBJECT (self,
        "After sorting not outputted list in poc ascending order");
    for (i = 0; i < not_outputted->len; i++) {
      GstH264Picture *tmp = g_array_index (not_outputted, GstH264Picture *, i);
      GST_TRACE_OBJECT (self,
          "\t%dth picture %p (frame_num %d, poc %d)", i, tmp,
          tmp->frame_num, tmp->pic_order_cnt);
    }
  }
#endif

  /* Try to output as many pictures as we can. A picture can be output,
   * if the number of decoded and not yet outputted pictures that would remain
   * in DPB afterwards would at least be equal to max_num_reorder_frames.
   * If the outputted picture is not a reference picture, it doesn't have
   * to remain in the DPB and can be removed */
  num_remaining = not_outputted->len;

  while (num_remaining > priv->max_num_reorder_frames ||
      /* If the condition below is used, this is an invalid stream. We should
       * not be forced to output beyond max_num_reorder_frames in order to
       * make room in DPB to store the current picture (if we need to do so).
       * However, if this happens, ignore max_num_reorder_frames and try
       * to output more. This may cause out-of-order output, but is not
       * fatal, and better than failing instead */
      ((gst_h264_dpb_is_full (priv->dpb) && (picture && (!picture->outputted
                      || picture->ref)))
          && num_remaining)) {
    gboolean clear_dpb = TRUE;
    GstH264Picture *to_output =
        g_array_index (not_outputted, GstH264Picture *, 0);

    gst_h264_picture_ref (to_output);
    g_array_remove_index (not_outputted, 0);

    if (num_remaining <= priv->max_num_reorder_frames) {
      GST_WARNING_OBJECT (self,
          "Invalid stream, max_num_reorder_frames not preserved");
    }

    GST_LOG_OBJECT (self,
        "Output picture %p (frame num %d)", to_output, to_output->frame_num);

    /* Current picture hasn't been inserted into DPB yet, so don't remove it
     * if we managed to output it immediately */
    if (picture && to_output == picture) {
      clear_dpb = FALSE;

      if (picture->ref) {
        GST_TRACE_OBJECT (self,
            "Put current picture %p (frame num %d, poc %d) to dpb",
            picture, picture->frame_num, picture->pic_order_cnt);
        gst_h264_dpb_add (priv->dpb, gst_h264_picture_ref (picture));
      }

      /* and mark current picture is handled */
      picture = NULL;
    }

    gst_h264_decoder_do_output_picture (self, to_output, clear_dpb);

    num_remaining--;
  }

  /* If we haven't managed to output the picture that we just decoded, or if
   * it's a reference picture, we have to store it in DPB */
  if (picture && (!picture->outputted || picture->ref)) {
    if (gst_h264_dpb_is_full (priv->dpb)) {
      /* If we haven't managed to output anything to free up space in DPB
       * to store this picture, it's an error in the stream */
      GST_WARNING_OBJECT (self, "Could not free up space in DPB");

      g_array_set_size (not_outputted, 0);
      return FALSE;
    }

    GST_TRACE_OBJECT (self,
        "Put picture %p (outputted %d, ref %d, frame num %d, poc %d) to dpb",
        picture, picture->outputted, picture->ref, picture->frame_num,
        picture->pic_order_cnt);
    gst_h264_dpb_add (priv->dpb, gst_h264_picture_ref (picture));
  }

  /* clear possible reference to the current picture.
   * If *picture* is still non-null, it means that the current picture not
   * outputted yet, and DPB may or may not hold the reference of the picture */
  if (picture)
    gst_h264_picture_ref (picture);

  g_array_set_size (not_outputted, 0);

  /* C.4.5.3 "Bumping" process for non-DPB full case, DPB full cases should be
   * covered above */
  /* FIXME: should cover interlaced streams */
  if (picture && !picture->outputted &&
      picture->field == GST_H264_PICTURE_FIELD_FRAME) {
    gboolean do_output = TRUE;
    if (picture->idr &&
        !picture->dec_ref_pic_marking.no_output_of_prior_pics_flag) {
      /* The current picture is an IDR picture and no_output_of_prior_pics_flag
       * is not equal to 1 and is not inferred to be equal to 1, as specified
       * in clause C.4.4 */
      GST_TRACE_OBJECT (self, "Output IDR picture");
    } else if (picture->mem_mgmt_5) {
      /* The current picture has memory_management_control_operation equal to 5,
       * as specified in clause C.4.4 */
      GST_TRACE_OBJECT (self, "Output mem_mgmt_5 picture");
    } else if (priv->last_output_poc >= 0 &&
        picture->pic_order_cnt > priv->last_output_poc &&
        (picture->pic_order_cnt - priv->last_output_poc) <= 2 &&
        /* NOTE: this might have a negative effect on throughput performance
         * depending on hardware implementation.
         * TODO: Possible solution is threading but it would make decoding flow
         * very complicated. */
        priv->is_live) {
      /* NOTE: this condition is not specified by spec but we can output
       * this picture based on calculated POC and last outputted POC */

      /* NOTE: The assumption here is, every POC of frame will have step of two.
       * however, if the assumption is wrong, (i.e., POC step is one, not two),
       * this would break output order. If this assumption is wrong,
       * please remove this condition.
       */
      GST_LOG_OBJECT (self,
          "Forcing output picture %p (frame num %d, poc %d, last poc %d)",
          picture, picture->frame_num, picture->pic_order_cnt,
          priv->last_output_poc);
    } else {
      do_output = FALSE;
      GST_TRACE_OBJECT (self, "Current picture %p (frame num %d, poc %d) "
          "is not ready to be output picture",
          picture, picture->frame_num, picture->pic_order_cnt);
    }

    if (do_output) {
      /* pass ownership of the current picture. At this point,
       * dpb must be holding a reference of the current picture */
      gst_h264_decoder_do_output_picture (self, picture, TRUE);
      picture = NULL;
    }
  }

  if (picture)
    gst_h264_picture_unref (picture);

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
} GstH264DecoderLevel;

typedef struct
{
  GstH264DecoderLevel level;

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
h264_level_to_max_dpb_mbs (GstH264DecoderLevel level)
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
    /* Level 1b */
    level = 9;
  }

  max_dpb_mbs = h264_level_to_max_dpb_mbs ((GstH264DecoderLevel) level);
  if (!max_dpb_mbs)
    return FALSE;

  width_mb = sps->width / 16;
  height_mb = sps->height / 16;

  max_dpb_frames = MIN (max_dpb_mbs / (width_mb * height_mb),
      GST_H264_DPB_MAX_SIZE);

  if (sps->vui_parameters_present_flag
      && sps->vui_parameters.bitstream_restriction_flag)
    max_dpb_frames = MAX (1, sps->vui_parameters.max_dec_frame_buffering);

  /* Case 1) There might be some non-conforming streams that require more DPB
   * size than that of specified one by SPS
   * Case 2) If bitstream_restriction_flag is not present,
   * max_dec_frame_buffering should be inferred
   * to be equal to MaxDpbFrames, then MaxDpbFrames can exceed num_ref_frames
   * See https://chromium-review.googlesource.com/c/chromium/src/+/760276/
   */
  max_dpb_size = MAX (max_dpb_frames, sps->num_ref_frames);
  if (max_dpb_size > GST_H264_DPB_MAX_SIZE) {
    GST_WARNING_OBJECT (self, "Too large calculated DPB size %d", max_dpb_size);
    max_dpb_size = GST_H264_DPB_MAX_SIZE;
  }

  /* Safety, so that subclass don't need bound checking */
  g_return_val_if_fail (max_dpb_size <= GST_H264_DPB_MAX_SIZE, FALSE);

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
  GArray *ref_pic_list0 = NULL;
  GArray *ref_pic_list1 = NULL;
  gboolean ret = FALSE;

  if (!picture) {
    GST_ERROR_OBJECT (self, "No current picture");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "Decode picture %p (frame_num %d, poc %d)",
      picture, picture->frame_num, picture->pic_order_cnt);

  priv->max_pic_num = slice->header.max_pic_num;

  if (priv->process_ref_pic_lists) {
    if (!gst_h264_decoder_modify_ref_pic_lists (self))
      goto beach;

    ref_pic_list0 = priv->ref_pic_list0;
    ref_pic_list1 = priv->ref_pic_list1;
  }

  g_assert (klass->decode_slice);

  ret = klass->decode_slice (self, picture, slice, ref_pic_list0,
      ref_pic_list1);
  if (!ret) {
    GST_WARNING_OBJECT (self,
        "Subclass didn't want to decode picture %p (frame_num %d, poc %d)",
        picture, picture->frame_num, picture->pic_order_cnt);
  }

beach:
  g_array_set_size (priv->ref_pic_list0, 0);
  g_array_set_size (priv->ref_pic_list1, 0);

  return ret;
}

static gint
pic_num_desc_compare (const GstH264Picture ** a, const GstH264Picture ** b)
{
  return (*b)->pic_num - (*a)->pic_num;
}

static gint
long_term_pic_num_asc_compare (const GstH264Picture ** a,
    const GstH264Picture ** b)
{
  return (*a)->long_term_pic_num - (*b)->long_term_pic_num;
}

static void
construct_ref_pic_lists_p (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  gint pos;

  /* RefPicList0 (8.2.4.2.1) [[1] [2]], where:
   * [1] shortterm ref pics sorted by descending pic_num,
   * [2] longterm ref pics by ascending long_term_pic_num.
   */
  g_array_set_size (priv->ref_pic_list_p0, 0);

  gst_h264_dpb_get_pictures_short_term_ref (priv->dpb, priv->ref_pic_list_p0);
  g_array_sort (priv->ref_pic_list_p0, (GCompareFunc) pic_num_desc_compare);

  pos = priv->ref_pic_list_p0->len;
  gst_h264_dpb_get_pictures_long_term_ref (priv->dpb, priv->ref_pic_list_p0);
  g_qsort_with_data (&g_array_index (priv->ref_pic_list_p0, gpointer, pos),
      priv->ref_pic_list_p0->len - pos, sizeof (gpointer),
      (GCompareDataFunc) long_term_pic_num_asc_compare, NULL);

#ifndef GST_DISABLE_GST_DEBUG
  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) >= GST_LEVEL_DEBUG) {
    GString *str = g_string_new (NULL);
    for (pos = 0; pos < priv->ref_pic_list_p0->len; pos++) {
      GstH264Picture *ref =
          g_array_index (priv->ref_pic_list_p0, GstH264Picture *, pos);
      if (!ref->long_term)
        g_string_append_printf (str, "|%i", ref->pic_num);
      else
        g_string_append_printf (str, "|%is", ref->pic_num);
    }
    GST_DEBUG_OBJECT (self, "ref_pic_list_p0: %s|", str->str);
    g_string_free (str, TRUE);
  }
#endif
}

static gboolean
lists_are_equal (GArray * l1, GArray * l2)
{
  gint i;

  if (l1->len != l2->len)
    return FALSE;

  for (i = 0; i < l1->len; i++)
    if (g_array_index (l1, gpointer, i) != g_array_index (l2, gpointer, i))
      return FALSE;

  return TRUE;
}

static gint
split_ref_pic_list_b (GstH264Decoder * self, GArray * ref_pic_list_b,
    GCompareFunc compare_func)
{
  gint pos;

  for (pos = 0; pos < ref_pic_list_b->len; pos++) {
    GstH264Picture *pic = g_array_index (ref_pic_list_b, GstH264Picture *, pos);
    if (compare_func (&pic, &self->priv->current_picture) > 0)
      break;
  }

  return pos;
}

static void
print_ref_pic_list_b (GstH264Decoder * self, GArray * ref_list_b, gint index)
{
#ifndef GST_DISABLE_GST_DEBUG
  GString *str;
  gint i;

  if (gst_debug_category_get_threshold (GST_CAT_DEFAULT) < GST_LEVEL_DEBUG)
    return;

  str = g_string_new (NULL);

  for (i = 0; i < ref_list_b->len; i++) {
    GstH264Picture *ref = g_array_index (ref_list_b, GstH264Picture *, i);

    if (!ref->long_term)
      g_string_append_printf (str, "|%i", ref->pic_order_cnt);
    else
      g_string_append_printf (str, "|%il", ref->long_term_pic_num);
  }

  GST_DEBUG_OBJECT (self, "ref_pic_list_b%i: %s| curr %i", index, str->str,
      self->priv->current_picture->pic_order_cnt);
  g_string_free (str, TRUE);
#endif
}

static void
construct_ref_pic_lists_b (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  gint pos;

  /* RefPicList0 (8.2.4.2.3) [[1] [2] [3]], where:
   * [1] shortterm ref pics with POC < current_picture's POC sorted by descending POC,
   * [2] shortterm ref pics with POC > current_picture's POC by ascending POC,
   * [3] longterm ref pics by ascending long_term_pic_num.
   */
  g_array_set_size (priv->ref_pic_list_b0, 0);
  g_array_set_size (priv->ref_pic_list_b1, 0);
  gst_h264_dpb_get_pictures_short_term_ref (priv->dpb, priv->ref_pic_list_b0);

  /* First sort ascending, this will put [1] in right place and finish
   * [2]. */
  print_ref_pic_list_b (self, priv->ref_pic_list_b0, 0);
  g_array_sort (priv->ref_pic_list_b0, (GCompareFunc) poc_asc_compare);
  print_ref_pic_list_b (self, priv->ref_pic_list_b0, 0);

  /* Find first with POC > current_picture's POC to get first element
   * in [2]... */
  pos = split_ref_pic_list_b (self, priv->ref_pic_list_b0,
      (GCompareFunc) poc_asc_compare);

  GST_DEBUG_OBJECT (self, "split point %i", pos);

  /* and sort [1] descending, thus finishing sequence [1] [2]. */
  g_qsort_with_data (priv->ref_pic_list_b0->data, pos, sizeof (gpointer),
      (GCompareDataFunc) poc_desc_compare, NULL);

  /* Now add [3] and sort by ascending long_term_pic_num. */
  pos = priv->ref_pic_list_b0->len;
  gst_h264_dpb_get_pictures_long_term_ref (priv->dpb, priv->ref_pic_list_b0);
  g_qsort_with_data (&g_array_index (priv->ref_pic_list_b0, gpointer, pos),
      priv->ref_pic_list_b0->len - pos, sizeof (gpointer),
      (GCompareDataFunc) long_term_pic_num_asc_compare, NULL);

  /* RefPicList1 (8.2.4.2.4) [[1] [2] [3]], where:
   * [1] shortterm ref pics with POC > curr_pic's POC sorted by ascending POC,
   * [2] shortterm ref pics with POC < curr_pic's POC by descending POC,
   * [3] longterm ref pics by ascending long_term_pic_num.
   */
  gst_h264_dpb_get_pictures_short_term_ref (priv->dpb, priv->ref_pic_list_b1);

  /* First sort by descending POC. */
  g_array_sort (priv->ref_pic_list_b1, (GCompareFunc) poc_desc_compare);

  /* Split at first with POC < current_picture's POC to get first element
   * in [2]... */
  pos = split_ref_pic_list_b (self, priv->ref_pic_list_b1,
      (GCompareFunc) poc_desc_compare);

  /* and sort [1] ascending. */
  g_qsort_with_data (priv->ref_pic_list_b1->data, pos, sizeof (gpointer),
      (GCompareDataFunc) poc_asc_compare, NULL);

  /* Now add [3] and sort by ascending long_term_pic_num */
  pos = priv->ref_pic_list_b1->len;
  gst_h264_dpb_get_pictures_long_term_ref (priv->dpb, priv->ref_pic_list_b1);
  g_qsort_with_data (&g_array_index (priv->ref_pic_list_b1, gpointer, pos),
      priv->ref_pic_list_b1->len - pos, sizeof (gpointer),
      (GCompareDataFunc) long_term_pic_num_asc_compare, NULL);

  /* If lists identical, swap first two entries in RefPicList1 (spec
   * 8.2.4.2.3) */
  if (priv->ref_pic_list_b1->len > 1
      && lists_are_equal (priv->ref_pic_list_b0, priv->ref_pic_list_b1)) {
    /* swap */
    GstH264Picture **list = (GstH264Picture **) priv->ref_pic_list_b1->data;
    GstH264Picture *pic = list[0];
    list[0] = list[1];
    list[1] = pic;
  }

  print_ref_pic_list_b (self, priv->ref_pic_list_b0, 0);
  print_ref_pic_list_b (self, priv->ref_pic_list_b1, 1);
}

static void
gst_h264_decoder_prepare_ref_pic_lists (GstH264Decoder * self)
{
  construct_ref_pic_lists_p (self);
  construct_ref_pic_lists_b (self);
}

static void
gst_h264_decoder_clear_ref_pic_lists (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;

  g_array_set_size (priv->ref_pic_list_p0, 0);
  g_array_set_size (priv->ref_pic_list_b0, 0);
  g_array_set_size (priv->ref_pic_list_b1, 0);
}

static gint
long_term_pic_num_f (GstH264Decoder * self, const GstH264Picture * picture)
{
  if (picture->ref && picture->long_term)
    return picture->long_term_pic_num;
  return 2 * (self->priv->max_long_term_frame_idx + 1);
}

static gint
pic_num_f (GstH264Decoder * self, const GstH264Picture * picture)
{
  if (!picture->long_term)
    return picture->pic_num;
  return self->priv->max_pic_num;
}

/* shift elements on the |array| starting from |from| to |to|,
 * inclusive, one position to the right and insert pic at |from| */
static void
shift_right_and_insert (GArray * array, gint from, gint to,
    GstH264Picture * picture)
{
  g_return_if_fail (from <= to);
  g_return_if_fail (array && picture);

  g_array_set_size (array, to + 2);
  g_array_insert_val (array, from, picture);
}

/* This can process either ref_pic_list0 or ref_pic_list1, depending
 * on the list argument. Set up pointers to proper list to be
 * processed here. */
static gboolean
modify_ref_pic_list (GstH264Decoder * self, int list)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264Picture *picture = priv->current_picture;
  GArray *ref_pic_listx;
  const GstH264SliceHdr *slice_hdr = &priv->current_slice.header;
  const GstH264RefPicListModification *list_mod;
  gboolean ref_pic_list_modification_flag_lX;
  gint num_ref_idx_lX_active_minus1;
  guint num_ref_pic_list_modifications;
  gint i;
  gint pic_num_lx_pred = picture->pic_num;
  gint ref_idx_lx = 0, src, dst;
  gint pic_num_lx_no_wrap;
  gint pic_num_lx;
  gboolean done = FALSE;
  GstH264Picture *pic;

  if (list == 0) {
    ref_pic_listx = priv->ref_pic_list0;
    ref_pic_list_modification_flag_lX =
        slice_hdr->ref_pic_list_modification_flag_l0;
    num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l0;
    num_ref_idx_lX_active_minus1 = slice_hdr->num_ref_idx_l0_active_minus1;
    list_mod = slice_hdr->ref_pic_list_modification_l0;
  } else {
    ref_pic_listx = priv->ref_pic_list1;
    ref_pic_list_modification_flag_lX =
        slice_hdr->ref_pic_list_modification_flag_l1;
    num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l1;
    num_ref_idx_lX_active_minus1 = slice_hdr->num_ref_idx_l1_active_minus1;
    list_mod = slice_hdr->ref_pic_list_modification_l1;
  }

  /* Resize the list to the size requested in the slice header.
   *
   * Note that per 8.2.4.2 it's possible for
   * num_ref_idx_lX_active_minus1 to indicate there should be more ref
   * pics on list than we constructed.  Those superfluous ones should
   * be treated as non-reference and will be initialized to null,
   * which must be handled by clients */
  g_assert (num_ref_idx_lX_active_minus1 >= 0);
  if (ref_pic_listx->len > num_ref_idx_lX_active_minus1 + 1)
    g_array_set_size (ref_pic_listx, num_ref_idx_lX_active_minus1 + 1);

  if (!ref_pic_list_modification_flag_lX)
    return TRUE;

  /* Spec 8.2.4.3:
   * Reorder pictures on the list in a way specified in the stream. */
  for (i = 0; i < num_ref_pic_list_modifications && !done; i++) {
    switch (list_mod->modification_of_pic_nums_idc) {
        /* 8.2.4.3.1 - Modify short reference picture position. */
      case 0:
      case 1:
        /* 8-34 */
        if (list_mod->modification_of_pic_nums_idc == 0) {
          /* Substract given value from predicted PicNum. */
          pic_num_lx_no_wrap = pic_num_lx_pred -
              (list_mod->value.abs_diff_pic_num_minus1 + 1);
          /* Wrap around max_pic_num if it becomes < 0 as result of
           * subtraction */
          if (pic_num_lx_no_wrap < 0)
            pic_num_lx_no_wrap += priv->max_pic_num;
        } else {                /* 8-35 */
          /* Add given value to predicted PicNum. */
          pic_num_lx_no_wrap = pic_num_lx_pred +
              (list_mod->value.abs_diff_pic_num_minus1 + 1);
          /* Wrap around max_pic_num if it becomes >= max_pic_num as
           * result of the addition */
          if (pic_num_lx_no_wrap >= priv->max_pic_num)
            pic_num_lx_no_wrap -= priv->max_pic_num;
        }

        /* For use in next iteration */
        pic_num_lx_pred = pic_num_lx_no_wrap;

        /* 8-36 */
        if (pic_num_lx_no_wrap > picture->pic_num)
          pic_num_lx = pic_num_lx_no_wrap - priv->max_pic_num;
        else
          pic_num_lx = pic_num_lx_no_wrap;

        /* 8-37 */
        g_assert (num_ref_idx_lX_active_minus1 + 1 < 32);
        pic = gst_h264_dpb_get_short_ref_by_pic_num (priv->dpb, pic_num_lx);
        if (!pic) {
          GST_WARNING_OBJECT (self, "Malformed stream, no pic num %d",
              pic_num_lx);
          return FALSE;
        }
        shift_right_and_insert (ref_pic_listx, ref_idx_lx,
            num_ref_idx_lX_active_minus1, pic);
        ref_idx_lx++;

        for (src = ref_idx_lx, dst = ref_idx_lx;
            src <= num_ref_idx_lX_active_minus1 + 1; src++) {
          GstH264Picture *src_pic =
              g_array_index (ref_pic_listx, GstH264Picture *, src);
          gint src_pic_num_lx = src_pic ? pic_num_f (self, src_pic) : -1;
          if (src_pic_num_lx != pic_num_lx)
            g_array_index (ref_pic_listx, GstH264Picture *, dst++) = src_pic;
        }

        break;

        /* 8.2.4.3.2 - Long-term reference pictures */
      case 2:
        /* (8-28) */
        g_assert (num_ref_idx_lX_active_minus1 + 1 < 32);
        pic = gst_h264_dpb_get_long_ref_by_pic_num (priv->dpb,
            list_mod->value.long_term_pic_num);
        if (!pic) {
          GST_WARNING_OBJECT (self, "Malformed stream, no pic num %d",
              list_mod->value.long_term_pic_num);
          return FALSE;
        }
        shift_right_and_insert (ref_pic_listx, ref_idx_lx,
            num_ref_idx_lX_active_minus1, pic);
        ref_idx_lx++;

        for (src = ref_idx_lx, dst = ref_idx_lx;
            src <= num_ref_idx_lX_active_minus1 + 1; src++) {
          GstH264Picture *src_pic =
              g_array_index (ref_pic_listx, GstH264Picture *, src);
          if (long_term_pic_num_f (self, src_pic) !=
              list_mod->value.long_term_pic_num)
            g_array_index (ref_pic_listx, GstH264Picture *, dst++) = src_pic;
        }

        break;

        /* End of modification list */
      case 3:
        done = TRUE;
        break;

      default:
        /* may be recoverable */
        GST_WARNING ("Invalid modification_of_pic_nums_idc = %d",
            list_mod->modification_of_pic_nums_idc);
        break;
    }

    list_mod++;
  }

  /* Per NOTE 2 in 8.2.4.3.2, the ref_pic_listx in the above loop is
   * temporarily made one element longer than the required final list.
   * Resize the list back to its required size. */
  if (ref_pic_listx->len > num_ref_idx_lX_active_minus1 + 1)
    g_array_set_size (ref_pic_listx, num_ref_idx_lX_active_minus1 + 1);

  return TRUE;
}

static void
copy_pic_list_into (GArray * dest, GArray * src)
{
  gint i;
  g_array_set_size (dest, 0);

  for (i = 0; i < src->len; i++)
    g_array_append_val (dest, g_array_index (src, gpointer, i));
}

static gboolean
gst_h264_decoder_modify_ref_pic_lists (GstH264Decoder * self)
{
  GstH264DecoderPrivate *priv = self->priv;
  GstH264SliceHdr *slice_hdr = &priv->current_slice.header;

  /* fill reference picture lists for B and S/SP slices */
  if (GST_H264_IS_P_SLICE (slice_hdr) || GST_H264_IS_SP_SLICE (slice_hdr)) {
    copy_pic_list_into (priv->ref_pic_list0, priv->ref_pic_list_p0);
    return modify_ref_pic_list (self, 0);
  } else {
    copy_pic_list_into (priv->ref_pic_list0, priv->ref_pic_list_b0);
    copy_pic_list_into (priv->ref_pic_list1, priv->ref_pic_list_b1);
    return modify_ref_pic_list (self, 0)
        && modify_ref_pic_list (self, 1);
  }

  return TRUE;
}

/**
 * gst_h264_decoder_set_process_ref_pic_lists:
 * @decoder: a #GstH264Decoder
 * @process: whether subclass is requiring reference picture modification process
 *
 * Called to en/disable reference picture modification process.
 *
 * Since: 1.18
 */
void
gst_h264_decoder_set_process_ref_pic_lists (GstH264Decoder * decoder,
    gboolean process)
{
  decoder->priv->process_ref_pic_lists = process;
}

/**
 * gst_h264_decoder_get_picture:
 * @decoder: a #GstH264Decoder
 * @system_frame_number: a target system frame number of #GstH264Picture
 *
 * Retrive DPB and return a #GstH264Picture corresponding to
 * the @system_frame_number
 *
 * Returns: (transfer full): a #GstH264Picture if successful, or %NULL otherwise
 *
 * Since: 1.18
 */
GstH264Picture *
gst_h264_decoder_get_picture (GstH264Decoder * decoder,
    guint32 system_frame_number)
{
  return gst_h264_dpb_get_picture (decoder->priv->dpb, system_frame_number);
}
