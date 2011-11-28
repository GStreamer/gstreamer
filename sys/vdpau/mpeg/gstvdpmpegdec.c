/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-vdpaumpegdec
 *
 * FIXME:Describe vdpaumpegdec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! vdpaumpegdec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <string.h>

#include "mpegutil.h"

#include "gstvdpmpegdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_mpeg_dec_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg_dec_debug

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) [ 1, 2 ], "
        "systemstream = (boolean) false")
    );

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg_dec_debug, "vdpaumpegdec", 0, \
    "VDPAU mpeg decoder");

GST_BOILERPLATE_FULL (GstVdpMpegDec, gst_vdp_mpeg_dec,
    GstVdpDecoder, GST_TYPE_VDP_DECODER, DEBUG_INIT);

static void gst_vdp_mpeg_dec_init_info (VdpPictureInfoMPEG1Or2 * vdp_info);

#define SYNC_CODE_SIZE 3

static VdpDecoderProfile
gst_vdp_mpeg_dec_get_profile (MPEGSeqExtHdr * hdr)
{
  VdpDecoderProfile profile;

  switch (hdr->profile) {
    case 5:
      profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
      break;
    default:
      profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
      break;
  }

  return profile;
}

static gboolean
gst_vdp_mpeg_dec_handle_picture_coding (GstVdpMpegDec * mpeg_dec,
    GstBuffer * buffer, GstVideoFrame * frame)
{
  MPEGPictureExt pic_ext;
  VdpPictureInfoMPEG1Or2 *info;
  gint fields;

  info = &mpeg_dec->vdp_info;

  if (!mpeg_util_parse_picture_coding_extension (&pic_ext, buffer))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.f_code, &pic_ext.f_code, 4);

  info->intra_dc_precision = pic_ext.intra_dc_precision;
  info->picture_structure = pic_ext.picture_structure;
  info->top_field_first = pic_ext.top_field_first;
  info->frame_pred_frame_dct = pic_ext.frame_pred_frame_dct;
  info->concealment_motion_vectors = pic_ext.concealment_motion_vectors;
  info->q_scale_type = pic_ext.q_scale_type;
  info->intra_vlc_format = pic_ext.intra_vlc_format;
  info->alternate_scan = pic_ext.alternate_scan;

  fields = 2;
  if (pic_ext.picture_structure == 3) {
    if (mpeg_dec->stream_info.interlaced) {
      if (pic_ext.progressive_frame == 0)
        fields = 2;
      if (pic_ext.progressive_frame == 0 && pic_ext.repeat_first_field == 0)
        fields = 2;
      if (pic_ext.progressive_frame == 1 && pic_ext.repeat_first_field == 1)
        fields = 3;
    } else {
      if (pic_ext.repeat_first_field == 0)
        fields = 2;
      if (pic_ext.repeat_first_field == 1 && pic_ext.top_field_first == 0)
        fields = 4;
      if (pic_ext.repeat_first_field == 1 && pic_ext.top_field_first == 1)
        fields = 6;
    }
  } else
    fields = 1;

  frame->n_fields = fields;

  if (pic_ext.top_field_first)
    GST_VIDEO_FRAME_FLAG_SET (frame, GST_VIDEO_FRAME_FLAG_TFF);

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_handle_picture (GstVdpMpegDec * mpeg_dec, GstBuffer * buffer)
{
  MPEGPictureHdr pic_hdr;

  if (!mpeg_util_parse_picture_hdr (&pic_hdr, buffer))
    return FALSE;

  mpeg_dec->vdp_info.picture_coding_type = pic_hdr.pic_type;

  if (mpeg_dec->stream_info.version == 1) {
    mpeg_dec->vdp_info.full_pel_forward_vector =
        pic_hdr.full_pel_forward_vector;
    mpeg_dec->vdp_info.full_pel_backward_vector =
        pic_hdr.full_pel_backward_vector;
    memcpy (&mpeg_dec->vdp_info.f_code, &pic_hdr.f_code, 4);
  }

  mpeg_dec->frame_nr = mpeg_dec->gop_frame + pic_hdr.tsn;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_handle_gop (GstVdpMpegDec * mpeg_dec, GstBuffer * buffer)
{
  MPEGGop gop;
  GstClockTime time;

  if (!mpeg_util_parse_gop (&gop, buffer))
    return FALSE;

  time = GST_SECOND * (gop.hour * 3600 + gop.minute * 60 + gop.second);

  GST_DEBUG ("gop timestamp: %" GST_TIME_FORMAT, GST_TIME_ARGS (time));

  mpeg_dec->gop_frame =
      gst_util_uint64_scale (time, mpeg_dec->stream_info.fps_n,
      mpeg_dec->stream_info.fps_d * GST_SECOND) + gop.frame;

  if (mpeg_dec->state == GST_VDP_MPEG_DEC_STATE_NEED_GOP)
    mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_DATA;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_handle_quant_matrix (GstVdpMpegDec * mpeg_dec,
    GstBuffer * buffer)
{
  MPEGQuantMatrix qm;

  if (!mpeg_util_parse_quant_matrix (&qm, buffer))
    return FALSE;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &qm.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &qm.non_intra_quantizer_matrix, 64);
  return TRUE;
}

static GstFlowReturn
gst_vdp_mpeg_dec_handle_sequence (GstVdpMpegDec * mpeg_dec,
    GstBuffer * seq, GstBuffer * seq_ext)
{
  GstBaseVideoDecoder *base_video_decoder = GST_BASE_VIDEO_DECODER (mpeg_dec);

  MPEGSeqHdr hdr;
  GstVdpMpegStreamInfo stream_info;

  if (!mpeg_util_parse_sequence_hdr (&hdr, seq))
    return GST_FLOW_CUSTOM_ERROR;

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr.intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr.non_intra_quantizer_matrix, 64);

  stream_info.width = hdr.width;
  stream_info.height = hdr.height;

  stream_info.fps_n = hdr.fps_n;
  stream_info.fps_d = hdr.fps_d;

  stream_info.par_n = hdr.par_w;
  stream_info.par_d = hdr.par_h;

  stream_info.interlaced = FALSE;
  stream_info.version = 1;
  stream_info.profile = VDP_DECODER_PROFILE_MPEG1;

  if (seq_ext) {
    MPEGSeqExtHdr ext;

    if (!mpeg_util_parse_sequence_extension (&ext, seq_ext))
      return GST_FLOW_CUSTOM_ERROR;

    stream_info.fps_n *= (ext.fps_n_ext + 1);
    stream_info.fps_d *= (ext.fps_d_ext + 1);

    stream_info.width += (ext.horiz_size_ext << 12);
    stream_info.height += (ext.vert_size_ext << 12);

    stream_info.interlaced = !ext.progressive;
    stream_info.version = 2;
    stream_info.profile = gst_vdp_mpeg_dec_get_profile (&ext);
  }

  if (memcmp (&mpeg_dec->stream_info, &stream_info,
          sizeof (GstVdpMpegStreamInfo)) != 0) {
    GstVideoState state;
    GstFlowReturn ret;

    state = gst_base_video_decoder_get_state (base_video_decoder);

    state.width = stream_info.width;
    state.height = stream_info.height;

    state.fps_n = stream_info.fps_n;
    state.fps_d = stream_info.fps_d;

    state.par_n = stream_info.par_n;
    state.par_d = stream_info.par_d;

    state.interlaced = stream_info.interlaced;

    gst_base_video_decoder_set_state (base_video_decoder, state);

    ret = gst_vdp_decoder_init_decoder (GST_VDP_DECODER (mpeg_dec),
        stream_info.profile, 2);
    if (ret != GST_FLOW_OK)
      return ret;

    memcpy (&mpeg_dec->stream_info, &stream_info,
        sizeof (GstVdpMpegStreamInfo));
  }

  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_DATA;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vdp_mpeg_dec_handle_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame, GstClockTimeDiff deadline)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (base_video_decoder);

  VdpPictureInfoMPEG1Or2 *info;
  GstVdpMpegFrame *mpeg_frame;

  GstFlowReturn ret = GST_FLOW_OK;
  VdpBitstreamBuffer vbit[1];
  GstVdpVideoBuffer *outbuf;

  /* MPEG_PACKET_SEQUENCE */
  mpeg_frame = GST_VDP_MPEG_FRAME (frame);
  if (mpeg_frame->seq) {
    ret = gst_vdp_mpeg_dec_handle_sequence (mpeg_dec, mpeg_frame->seq,
        mpeg_frame->seq_ext);
    if (ret != GST_FLOW_OK) {
      gst_base_video_decoder_skip_frame (base_video_decoder, frame);
      return ret;
    }
  }

  if (mpeg_dec->state == GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE) {
    GST_DEBUG_OBJECT (mpeg_dec, "Drop frame since we haven't found a "
        "MPEG_PACKET_SEQUENCE yet");

    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return GST_FLOW_OK;
  }

  /* MPEG_PACKET_PICTURE */
  if (mpeg_frame->pic)
    gst_vdp_mpeg_dec_handle_picture (mpeg_dec, mpeg_frame->pic);

  /* MPEG_PACKET_EXT_PICTURE_CODING */
  if (mpeg_frame->pic_ext)
    gst_vdp_mpeg_dec_handle_picture_coding (mpeg_dec, mpeg_frame->pic_ext,
        frame);

  /* MPEG_PACKET_GOP */
  if (mpeg_frame->gop)
    gst_vdp_mpeg_dec_handle_gop (mpeg_dec, mpeg_frame->gop);

  /* MPEG_PACKET_EXT_QUANT_MATRIX */
  if (mpeg_frame->qm_ext)
    gst_vdp_mpeg_dec_handle_quant_matrix (mpeg_dec, mpeg_frame->qm_ext);


  info = &mpeg_dec->vdp_info;

  info->slice_count = mpeg_frame->n_slices;

  /* check if we can decode the frame */
  if (info->picture_coding_type != I_FRAME
      && info->backward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got an I_FRAME yet");

    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return GST_FLOW_OK;
  }
  if (info->picture_coding_type == B_FRAME
      && info->forward_reference == VDP_INVALID_HANDLE) {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got two non B_FRAMES yet");

    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return GST_FLOW_OK;
  }


  if (info->picture_coding_type != B_FRAME) {
    if (info->backward_reference != VDP_INVALID_HANDLE) {
      ret = gst_base_video_decoder_finish_frame (base_video_decoder,
          mpeg_dec->b_frame);
    }

    if (info->forward_reference != VDP_INVALID_HANDLE) {
      gst_video_frame_unref (mpeg_dec->f_frame);
      info->forward_reference = VDP_INVALID_HANDLE;
    }

    info->forward_reference = info->backward_reference;
    mpeg_dec->f_frame = mpeg_dec->b_frame;

    info->backward_reference = VDP_INVALID_HANDLE;
  }

  if (ret != GST_FLOW_OK) {
    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return ret;
  }

  /* decode */
  vbit[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit[0].bitstream = GST_BUFFER_DATA (mpeg_frame->slices);
  vbit[0].bitstream_bytes = GST_BUFFER_SIZE (mpeg_frame->slices);

  ret = gst_vdp_decoder_render (GST_VDP_DECODER (mpeg_dec),
      (VdpPictureInfo *) info, 1, vbit, &outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  frame->src_buffer = GST_BUFFER_CAST (outbuf);

  if (info->picture_coding_type == B_FRAME) {
    ret = gst_base_video_decoder_finish_frame (base_video_decoder, frame);
  } else {
    info->backward_reference = GST_VDP_VIDEO_BUFFER (outbuf)->surface;
    mpeg_dec->b_frame = gst_video_frame_ref (frame);
  }

  return ret;
}

static GstVideoFrame *
gst_vdp_mpeg_dec_create_frame (GstBaseVideoDecoder * base_video_decoder)
{
  return GST_VIDEO_FRAME (gst_vdp_mpeg_frame_new ());
}

static GstFlowReturn
gst_vdp_mpeg_dec_parse_data (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf, gboolean at_eos, GstVideoFrame * frame)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (base_video_decoder);

  GstVdpMpegFrame *mpeg_frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBitReader b_reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);
  guint8 start_code;

  /* skip sync_code */
  gst_bit_reader_skip (&b_reader, 8 * 3);

  /* start_code */
  if (!gst_bit_reader_get_bits_uint8 (&b_reader, &start_code, 8))
    return GST_FLOW_ERROR;

  mpeg_frame = GST_VDP_MPEG_FRAME_CAST (frame);

  if (start_code >= MPEG_PACKET_SLICE_MIN
      && start_code <= MPEG_PACKET_SLICE_MAX) {
    GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SLICE");

    gst_vdp_mpeg_frame_add_slice (mpeg_frame, buf);
    goto done;
  }

  switch (start_code) {
    case MPEG_PACKET_SEQUENCE:
      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_SEQUENCE");

      if (mpeg_dec->prev_packet != -1)
        ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
            (GstVideoFrame **) & mpeg_frame);

      mpeg_frame->seq = buf;
      break;

    case MPEG_PACKET_PICTURE:
      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_PICTURE");

      if (mpeg_dec->prev_packet != MPEG_PACKET_SEQUENCE &&
          mpeg_dec->prev_packet != MPEG_PACKET_GOP)
        ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
            (GstVideoFrame **) & mpeg_frame);

      mpeg_frame->pic = buf;
      break;

    case MPEG_PACKET_GOP:
      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_GOP");

      if (mpeg_dec->prev_packet != MPEG_PACKET_SEQUENCE)
        ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
            (GstVideoFrame **) & mpeg_frame);

      mpeg_frame->gop = buf;
      break;

    case MPEG_PACKET_EXTENSION:
    {
      guint8 ext_code;

      /* ext_code */
      if (!gst_bit_reader_get_bits_uint8 (&b_reader, &ext_code, 4)) {
        ret = GST_FLOW_ERROR;
        gst_buffer_unref (buf);
        goto done;
      }

      GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXTENSION: %d", ext_code);

      switch (ext_code) {
        case MPEG_PACKET_EXT_SEQUENCE:
          GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_SEQUENCE");


          mpeg_frame->seq_ext = buf;

          /* so that we don't finish the frame if we get a MPEG_PACKET_PICTURE
           * or MPEG_PACKET_GOP after this */
          start_code = MPEG_PACKET_SEQUENCE;
          break;

        case MPEG_PACKET_EXT_SEQUENCE_DISPLAY:
          GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_SEQUENCE_DISPLAY");

          /* so that we don't finish the frame if we get a MPEG_PACKET_PICTURE
           * or MPEG_PACKET_GOP after this */
          start_code = MPEG_PACKET_SEQUENCE;
          break;

        case MPEG_PACKET_EXT_PICTURE_CODING:
          GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_PICTURE_CODING");

          mpeg_frame->pic_ext = buf;
          break;

        case MPEG_PACKET_EXT_QUANT_MATRIX:
          GST_DEBUG_OBJECT (mpeg_dec, "MPEG_PACKET_EXT_QUANT_MATRIX");

          mpeg_frame->qm_ext = buf;
          break;

        default:
          gst_buffer_unref (buf);
      }
      break;
    }

    default:
      gst_buffer_unref (buf);
  }

  if (at_eos && mpeg_frame->slices)
    ret = gst_base_video_decoder_have_frame (base_video_decoder, TRUE, NULL);

done:
  mpeg_dec->prev_packet = start_code;

  return ret;
}

static gint
gst_vdp_mpeg_dec_scan_for_sync (GstBaseVideoDecoder * base_video_decoder,
    GstAdapter * adapter)
{
  gint m;

  m = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100, 0,
      gst_adapter_available (adapter));
  if (m == -1)
    return gst_adapter_available (adapter) - SYNC_CODE_SIZE;

  return m;
}

static GstBaseVideoDecoderScanResult
gst_vdp_mpeg_dec_scan_for_packet_end (GstBaseVideoDecoder * base_video_decoder,
    GstAdapter * adapter, guint * size, gboolean at_eos)
{
  guint8 *data;
  guint32 sync_code;

  data = g_slice_alloc (SYNC_CODE_SIZE);
  gst_adapter_copy (adapter, data, 0, SYNC_CODE_SIZE);
  sync_code = ((data[0] << 16) | (data[1] << 8) | data[2]);

  if (sync_code != 0x000001)
    return GST_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC;

  *size = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
      SYNC_CODE_SIZE, gst_adapter_available (adapter) - SYNC_CODE_SIZE);

  if (*size == -1)
    return GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA;

  return GST_BASE_VIDEO_DECODER_SCAN_RESULT_OK;
}

static gboolean
gst_vdp_mpeg_dec_flush (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (base_video_decoder);

  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    gst_video_frame_unref (mpeg_dec->f_frame);
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    gst_video_frame_unref (mpeg_dec->b_frame);

  gst_vdp_mpeg_dec_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->prev_packet = -1;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_start (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (base_video_decoder);

  gst_vdp_mpeg_dec_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE;

  memset (&mpeg_dec->stream_info, 0, sizeof (GstVdpMpegStreamInfo));

  return GST_BASE_VIDEO_DECODER_CLASS
      (parent_class)->start (base_video_decoder);
}

static gboolean
gst_vdp_mpeg_dec_stop (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (base_video_decoder);

  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    mpeg_dec->vdp_info.forward_reference = VDP_INVALID_HANDLE;
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    mpeg_dec->vdp_info.backward_reference = VDP_INVALID_HANDLE;

  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE;

  return GST_BASE_VIDEO_DECODER_CLASS (parent_class)->stop (base_video_decoder);
}

static void
gst_vdp_mpeg_dec_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "VDPAU Mpeg Decoder",
      "Decoder",
      "Decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_static_pad_template (element_class,
      &sink_template);
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_mpeg_dec_class_init (GstVdpMpegDecClass * klass)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  base_video_decoder_class = GST_BASE_VIDEO_DECODER_CLASS (klass);

  base_video_decoder_class->start = gst_vdp_mpeg_dec_start;
  base_video_decoder_class->stop = gst_vdp_mpeg_dec_stop;
  base_video_decoder_class->flush = gst_vdp_mpeg_dec_flush;

  base_video_decoder_class->scan_for_sync = gst_vdp_mpeg_dec_scan_for_sync;
  base_video_decoder_class->scan_for_packet_end =
      gst_vdp_mpeg_dec_scan_for_packet_end;
  base_video_decoder_class->parse_data = gst_vdp_mpeg_dec_parse_data;

  base_video_decoder_class->handle_frame = gst_vdp_mpeg_dec_handle_frame;
  base_video_decoder_class->create_frame = gst_vdp_mpeg_dec_create_frame;
}

static void
gst_vdp_mpeg_dec_init_info (VdpPictureInfoMPEG1Or2 * vdp_info)
{
  vdp_info->forward_reference = VDP_INVALID_HANDLE;
  vdp_info->backward_reference = VDP_INVALID_HANDLE;
  vdp_info->slice_count = 0;
  vdp_info->picture_structure = 3;
  vdp_info->picture_coding_type = 0;
  vdp_info->intra_dc_precision = 0;
  vdp_info->frame_pred_frame_dct = 1;
  vdp_info->concealment_motion_vectors = 0;
  vdp_info->intra_vlc_format = 0;
  vdp_info->alternate_scan = 0;
  vdp_info->q_scale_type = 0;
  vdp_info->top_field_first = 1;
}

static void
gst_vdp_mpeg_dec_init (GstVdpMpegDec * mpeg_dec, GstVdpMpegDecClass * gclass)
{
}
