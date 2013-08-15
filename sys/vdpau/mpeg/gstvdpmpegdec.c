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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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
#include <gst/codecparsers/gstmpegvideoparser.h>
#include <gst/codecparsers/gstmpegvideometa.h>
#include <string.h>

#include "gstvdpmpegdec.h"
#include "gstvdpvideomemory.h"

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

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg_dec_debug, "vdpaumpegdec", 0, \
    "VDPAU mpeg decoder");
#define gst_vdp_mpeg_dec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVdpMpegDec, gst_vdp_mpeg_dec, GST_TYPE_VDP_DECODER,
    DEBUG_INIT);

static void gst_vdp_mpeg_dec_init_info (VdpPictureInfoMPEG1Or2 * vdp_info);

#define SYNC_CODE_SIZE 3

static VdpDecoderProfile
gst_vdp_mpeg_dec_get_profile (GstMpegVideoSequenceExt * hdr)
{
  VdpDecoderProfile profile;

  switch (hdr->profile) {
    case GST_MPEG_VIDEO_PROFILE_SIMPLE:
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
    GstMpegVideoPictureExt * pic_ext, GstVideoCodecFrame * frame)
{
  VdpPictureInfoMPEG1Or2 *info;
#if 0
  gint fields;
#endif

  GST_DEBUG_OBJECT (mpeg_dec, "Handling GstMpegVideoPictureExt");

  info = &mpeg_dec->vdp_info;

  /* FIXME : Set defaults when pic_ext isn't present */

  memcpy (&mpeg_dec->vdp_info.f_code, &pic_ext->f_code, 4);

  info->intra_dc_precision = pic_ext->intra_dc_precision;
  info->picture_structure = pic_ext->picture_structure;
  info->top_field_first = pic_ext->top_field_first;
  info->frame_pred_frame_dct = pic_ext->frame_pred_frame_dct;
  info->concealment_motion_vectors = pic_ext->concealment_motion_vectors;
  info->q_scale_type = pic_ext->q_scale_type;
  info->intra_vlc_format = pic_ext->intra_vlc_format;
  info->alternate_scan = pic_ext->alternate_scan;

#if 0
  fields = 2;
  if (pic_ext->picture_structure == 3) {
    if (mpeg_dec->stream_info.interlaced) {
      if (pic_ext->progressive_frame == 0)
        fields = 2;
      if (pic_ext->progressive_frame == 0 && pic_ext->repeat_first_field == 0)
        fields = 2;
      if (pic_ext->progressive_frame == 1 && pic_ext->repeat_first_field == 1)
        fields = 3;
    } else {
      if (pic_ext->repeat_first_field == 0)
        fields = 2;
      if (pic_ext->repeat_first_field == 1 && pic_ext->top_field_first == 0)
        fields = 4;
      if (pic_ext->repeat_first_field == 1 && pic_ext->top_field_first == 1)
        fields = 6;
    }
  } else
    fields = 1;
#endif

  if (pic_ext->top_field_first)
    GST_FIXME ("Set TFF on outgoing buffer");
#if 0
  GST_VIDEO_FRAME_FLAG_SET (frame, GST_VIDEO_FRAME_FLAG_TFF);
#endif

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_handle_picture (GstVdpMpegDec * mpeg_dec,
    GstMpegVideoPictureHdr * pic_hdr)
{
  GST_DEBUG_OBJECT (mpeg_dec, "Handling GstMpegVideoPictureHdr");

  mpeg_dec->vdp_info.picture_coding_type = pic_hdr->pic_type;

  if (mpeg_dec->stream_info.version == 1) {
    mpeg_dec->vdp_info.full_pel_forward_vector =
        pic_hdr->full_pel_forward_vector;
    mpeg_dec->vdp_info.full_pel_backward_vector =
        pic_hdr->full_pel_backward_vector;
    memcpy (&mpeg_dec->vdp_info.f_code, &pic_hdr->f_code, 4);
  }

  mpeg_dec->frame_nr = mpeg_dec->gop_frame + pic_hdr->tsn;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstVdpMpegDec *mpeg_dec = (GstVdpMpegDec *) decoder;

  /* FIXME : Check the hardware can handle the level/profile */
  if (mpeg_dec->input_state)
    gst_video_codec_state_unref (mpeg_dec->input_state);
  mpeg_dec->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

#if 0
static gboolean
gst_vdp_mpeg_dec_handle_gop (GstVdpMpegDec * mpeg_dec, const guint8 * data,
    gsize size, guint offset)
{
  GstMpegVideoGop gop;
  GstClockTime time;

  if (!gst_mpeg_video_parse_gop (&gop, data, size, offset))
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
#endif

static gboolean
gst_vdp_mpeg_dec_handle_quant_matrix (GstVdpMpegDec * mpeg_dec,
    GstMpegVideoQuantMatrixExt * qm)
{
  GST_DEBUG_OBJECT (mpeg_dec, "Handling GstMpegVideoQuantMatrixExt");

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &qm->intra_quantiser_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &qm->non_intra_quantiser_matrix, 64);

  return TRUE;
}

static GstFlowReturn
gst_vdp_mpeg_dec_handle_sequence (GstVdpMpegDec * mpeg_dec,
    GstMpegVideoSequenceHdr * hdr, GstMpegVideoSequenceExt * ext)
{
  GstFlowReturn ret;
  GstVideoDecoder *video_decoder = GST_VIDEO_DECODER (mpeg_dec);
  GstVdpMpegStreamInfo stream_info;

  GST_DEBUG_OBJECT (mpeg_dec, "Handling GstMpegVideoSequenceHdr");

  memcpy (&mpeg_dec->vdp_info.intra_quantizer_matrix,
      &hdr->intra_quantizer_matrix, 64);
  memcpy (&mpeg_dec->vdp_info.non_intra_quantizer_matrix,
      &hdr->non_intra_quantizer_matrix, 64);

  stream_info.width = hdr->width;
  stream_info.height = hdr->height;

  stream_info.fps_n = hdr->fps_n;
  stream_info.fps_d = hdr->fps_d;

  stream_info.par_n = hdr->par_w;
  stream_info.par_d = hdr->par_h;

  stream_info.interlaced = FALSE;
  stream_info.version = 1;
  stream_info.profile = VDP_DECODER_PROFILE_MPEG1;

  if (ext) {
    GST_DEBUG_OBJECT (mpeg_dec, "Handling GstMpegVideoSequenceExt");

    /* FIXME : isn't this already processed by mpegvideoparse ? */
    stream_info.fps_n *= (ext->fps_n_ext + 1);
    stream_info.fps_d *= (ext->fps_d_ext + 1);

    stream_info.width += (ext->horiz_size_ext << 12);
    stream_info.height += (ext->vert_size_ext << 12);

    stream_info.interlaced = !ext->progressive;
    stream_info.version = 2;
    stream_info.profile = gst_vdp_mpeg_dec_get_profile (ext);
  }

  GST_DEBUG_OBJECT (mpeg_dec, "Setting output state to %dx%d",
      stream_info.width, stream_info.height);
  mpeg_dec->output_state =
      gst_video_decoder_set_output_state (video_decoder, GST_VIDEO_FORMAT_YV12,
      stream_info.width, stream_info.height, mpeg_dec->input_state);
  if (stream_info.interlaced)
    mpeg_dec->output_state->info.interlace_mode =
        GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
  gst_video_decoder_negotiate (video_decoder);

  ret = gst_vdp_decoder_init_decoder (GST_VDP_DECODER (mpeg_dec),
      stream_info.profile, 2, mpeg_dec->output_state);
  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_DATA;

  return ret;
}

static GstFlowReturn
gst_vdp_mpeg_dec_handle_frame (GstVideoDecoder * video_decoder,
    GstVideoCodecFrame * frame)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (video_decoder);

  VdpPictureInfoMPEG1Or2 *info;
  GstMpegVideoMeta *mpeg_meta;
  GstVdpVideoMemory *vmem;

  GstFlowReturn ret = GST_FLOW_OK;
  VdpBitstreamBuffer vbit[1];
  GstMapInfo mapinfo;

  /* FIXME : Specify in sink query that we need the mpeg video meta */

  /* Parse all incoming data from the frame */
  mpeg_meta = gst_buffer_get_mpeg_video_meta (frame->input_buffer);
  if (!mpeg_meta)
    goto no_meta;

  /* GST_MPEG_VIDEO_PACKET_SEQUENCE */
  if (mpeg_meta->sequencehdr) {
    ret =
        gst_vdp_mpeg_dec_handle_sequence (mpeg_dec, mpeg_meta->sequencehdr,
        mpeg_meta->sequenceext);
    if (ret != GST_FLOW_OK)
      goto sequence_parse_fail;
  }

  if (mpeg_dec->state == GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE)
    goto need_sequence;

  /* GST_MPEG_VIDEO_PACKET_PICTURE */
  if (mpeg_meta->pichdr)
    gst_vdp_mpeg_dec_handle_picture (mpeg_dec, mpeg_meta->pichdr);

  /* GST_MPEG_VIDEO_PACKET_EXT_PICTURE_CODING */
  if (mpeg_meta->picext)
    gst_vdp_mpeg_dec_handle_picture_coding (mpeg_dec, mpeg_meta->picext, frame);

  /* GST_MPEG_VIDEO_PACKET_GOP */
  /* if (mpeg_meta->gop) */
  /*   GST_FIXME_OBJECT (mpeg_dec, "Handle GOP !"); */
  /*   gst_vdp_mpeg_dec_handle_gop (mpeg_dec, mpeg_frame.gop); */

  /* GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX */
  if (mpeg_meta->quantext)
    gst_vdp_mpeg_dec_handle_quant_matrix (mpeg_dec, mpeg_meta->quantext);

  info = &mpeg_dec->vdp_info;

  info->slice_count = mpeg_meta->num_slices;

  GST_DEBUG_OBJECT (mpeg_dec, "picture coding type %d",
      info->picture_coding_type);

  /* check if we can decode the frame */
  if (info->picture_coding_type != GST_MPEG_VIDEO_PICTURE_TYPE_I
      && info->backward_reference == VDP_INVALID_HANDLE)
    goto need_i_frame;

  if (info->picture_coding_type == GST_MPEG_VIDEO_PICTURE_TYPE_B
      && info->forward_reference == VDP_INVALID_HANDLE)
    goto need_non_b_frame;

  if (info->picture_coding_type != GST_MPEG_VIDEO_PICTURE_TYPE_B) {
    if (info->backward_reference != VDP_INVALID_HANDLE) {
      GST_DEBUG_OBJECT (mpeg_dec, "Pushing B frame");
      ret = gst_video_decoder_finish_frame (video_decoder, mpeg_dec->b_frame);
    }

    if (info->forward_reference != VDP_INVALID_HANDLE) {
      GST_DEBUG_OBJECT (mpeg_dec, "Releasing no-longer needed forward frame");
      gst_video_codec_frame_unref (mpeg_dec->f_frame);
      info->forward_reference = VDP_INVALID_HANDLE;
    }

    info->forward_reference = info->backward_reference;
    mpeg_dec->f_frame = mpeg_dec->b_frame;

    info->backward_reference = VDP_INVALID_HANDLE;
  }

  if (ret != GST_FLOW_OK)
    goto exit_after_b_frame;

  /* decode */
  if (!gst_buffer_map (frame->input_buffer, &mapinfo, GST_MAP_READ))
    goto map_fail;

  vbit[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  vbit[0].bitstream = mapinfo.data + mpeg_meta->slice_offset;
  vbit[0].bitstream_bytes = mapinfo.size - mpeg_meta->slice_offset;

  ret = gst_vdp_decoder_render (GST_VDP_DECODER (mpeg_dec),
      (VdpPictureInfo *) info, 1, vbit, frame);

  gst_buffer_unmap (frame->input_buffer, &mapinfo);

  if (ret != GST_FLOW_OK)
    goto render_fail;

  vmem = (GstVdpVideoMemory *) gst_buffer_get_memory (frame->output_buffer, 0);

  if (info->picture_coding_type == GST_MPEG_VIDEO_PICTURE_TYPE_B) {
    ret = gst_video_decoder_finish_frame (video_decoder, frame);
  } else {
    info->backward_reference = vmem->surface;
    mpeg_dec->b_frame = gst_video_codec_frame_ref (frame);
  }

  return ret;

  /* EARLY EXIT */
need_sequence:
  {
    GST_DEBUG_OBJECT (mpeg_dec, "Drop frame since we haven't found a "
        "GST_MPEG_VIDEO_PACKET_SEQUENCE yet");

    gst_video_decoder_finish_frame (video_decoder, frame);
    return GST_FLOW_OK;
  }

need_i_frame:
  {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got an I_FRAME yet");

    gst_video_decoder_finish_frame (video_decoder, frame);
    return GST_FLOW_OK;
  }

need_non_b_frame:
  {
    GST_DEBUG_OBJECT (mpeg_dec,
        "Drop frame since we haven't got two non B_FRAME yet");

    gst_video_decoder_finish_frame (video_decoder, frame);
    return GST_FLOW_OK;
  }


  /* ERRORS */
no_meta:
  {
    GST_ERROR_OBJECT (video_decoder,
        "Input buffer does not have MpegVideo GstMeta");
    gst_video_decoder_drop_frame (video_decoder, frame);
    return GST_FLOW_ERROR;
  }

sequence_parse_fail:
  {
    GST_ERROR_OBJECT (video_decoder, "Failed to handle sequence header");
    gst_video_decoder_finish_frame (video_decoder, frame);
    return ret;
  }

exit_after_b_frame:
  {
    GST_WARNING_OBJECT (video_decoder, "Leaving after pushing B frame");
    gst_video_decoder_finish_frame (video_decoder, frame);
    return ret;
  }

map_fail:
  {
    GST_ERROR_OBJECT (video_decoder, "Failed to map input buffer");
    gst_video_decoder_drop_frame (video_decoder, frame);
    return GST_FLOW_ERROR;
  }

render_fail:
  {
    GST_ERROR_OBJECT (video_decoder, "Error when rendering the frame");
    gst_video_decoder_drop_frame (video_decoder, frame);
    return ret;
  }
}

static gboolean
gst_vdp_mpeg_dec_flush (GstVideoDecoder * video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (video_decoder);

  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    gst_video_codec_frame_unref (mpeg_dec->f_frame);
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    gst_video_codec_frame_unref (mpeg_dec->b_frame);

  gst_vdp_mpeg_dec_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->prev_packet = -1;

  return TRUE;
}

static gboolean
gst_vdp_mpeg_dec_start (GstVideoDecoder * video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (video_decoder);

  GST_DEBUG_OBJECT (video_decoder, "Starting");

  gst_vdp_mpeg_dec_init_info (&mpeg_dec->vdp_info);

  mpeg_dec->decoder = VDP_INVALID_HANDLE;
  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE;

  memset (&mpeg_dec->stream_info, 0, sizeof (GstVdpMpegStreamInfo));

  return GST_VIDEO_DECODER_CLASS (parent_class)->start (video_decoder);
}

static gboolean
gst_vdp_mpeg_dec_stop (GstVideoDecoder * video_decoder)
{
  GstVdpMpegDec *mpeg_dec = GST_VDP_MPEG_DEC (video_decoder);

  if (mpeg_dec->vdp_info.forward_reference != VDP_INVALID_HANDLE)
    mpeg_dec->vdp_info.forward_reference = VDP_INVALID_HANDLE;
  if (mpeg_dec->vdp_info.backward_reference != VDP_INVALID_HANDLE)
    mpeg_dec->vdp_info.backward_reference = VDP_INVALID_HANDLE;

  mpeg_dec->state = GST_VDP_MPEG_DEC_STATE_NEED_SEQUENCE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (video_decoder);
}

/* initialize the vdpaumpegdecoder's class */
static void
gst_vdp_mpeg_dec_class_init (GstVdpMpegDecClass * klass)
{
  GstElementClass *element_class;
  GstVideoDecoderClass *video_decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "VDPAU Mpeg Decoder",
      "Decoder",
      "Decode mpeg stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  video_decoder_class->start = gst_vdp_mpeg_dec_start;
  video_decoder_class->stop = gst_vdp_mpeg_dec_stop;
  video_decoder_class->flush = gst_vdp_mpeg_dec_flush;

  video_decoder_class->handle_frame = gst_vdp_mpeg_dec_handle_frame;
  video_decoder_class->set_format = gst_vdp_mpeg_dec_set_format;
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
gst_vdp_mpeg_dec_init (GstVdpMpegDec * mpeg_dec)
{
}
