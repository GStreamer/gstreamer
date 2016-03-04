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
 * SECTION:element-vdpaumpeg4dec
 *
 * FIXME:Describe vdpaumpeg4dec here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v -m fakesrc ! vdpaumpeg4dec ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include <gst/gst.h>
#include <vdpau/vdpau.h>
#include <string.h>

#include "gstvdpmpeg4dec.h"

GST_DEBUG_CATEGORY (gst_vdp_mpeg4_dec_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg4_dec_debug

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, mpegversion = (int) 4, "
        "systemstream = (boolean) false; "
        "video/x-divx, divxversion = (int) [4, 5]; " "video/x-xvid"));

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg4_dec_debug, "vdpaumpeg4dec", 0, \
    "VDPAU mpeg4 decoder");

G_DEFINE_TYPE_FULL (GstVdpMpeg4Dec, gst_vdp_mpeg4_dec, GST_TYPE_VDP_DECODER,
    DEBUG_INIT);

#define SYNC_CODE_SIZE 3

static VdpPictureInfoMPEG4Part2
gst_vdp_mpeg4_dec_fill_info (GstVdpMpeg4Dec * mpeg4_dec,
    GstMpeg4Frame * mpeg4_frame, Mpeg4VideoObjectPlane * vop)
{
  Mpeg4VideoObjectLayer *vol;
  VdpPictureInfoMPEG4Part2 info;

  vol = &mpeg4_dec->vol;

  info.forward_reference = VDP_INVALID_HANDLE;
  info.backward_reference = VDP_INVALID_HANDLE;

  /* forward reference */
  if (vop->coding_type != I_VOP && mpeg4_dec->f_frame) {
    info.forward_reference =
        GST_VDP_VIDEO_BUFFER (GST_VIDEO_FRAME (mpeg4_dec->f_frame)->
        src_buffer)->surface;
  }

  if (vop->coding_type == B_VOP) {
    guint32 trd_time, trb_time;

    trd_time = mpeg4_dec->b_frame->vop_time - mpeg4_dec->f_frame->vop_time;
    trb_time = mpeg4_frame->vop_time - mpeg4_dec->f_frame->vop_time;

    info.trd[0] = trd_time;
    info.trb[0] = trb_time;

    info.trd[1] = round ((double) trd_time / (double) mpeg4_dec->tframe);
    info.trb[1] = round ((double) trb_time / (double) mpeg4_dec->tframe);

    /* backward reference */
    if (mpeg4_dec->b_frame) {
      info.backward_reference =
          GST_VDP_VIDEO_BUFFER (GST_VIDEO_FRAME (mpeg4_dec->b_frame)->
          src_buffer)->surface;
    }
  }

  memcpy (info.intra_quantizer_matrix, vol->intra_quant_mat, 64);
  memcpy (info.non_intra_quantizer_matrix, vol->non_intra_quant_mat, 64);

  info.vop_time_increment_resolution = vol->vop_time_increment_resolution;
  info.resync_marker_disable = vol->resync_marker_disable;
  info.interlaced = vol->interlaced;
  info.quant_type = vol->quant_type;
  info.quarter_sample = vol->quarter_sample;
  /* FIXME: support short video header */
  info.short_video_header = FALSE;

  info.vop_coding_type = vop->coding_type;
  info.vop_fcode_forward = vop->fcode_forward;
  info.vop_fcode_backward = vop->fcode_backward;
  info.rounding_control = vop->rounding_type;
  info.alternate_vertical_scan_flag = vop->alternate_vertical_scan_flag;
  info.top_field_first = vop->top_field_first;

  return info;
}

static gboolean
gst_vdp_mpeg4_dec_handle_configuration (GstVdpMpeg4Dec * mpeg4_dec,
    GstMpeg4Frame * mpeg4_frame)
{
  Mpeg4VisualObjectSequence vos;
  Mpeg4VisualObject vo;
  Mpeg4VideoObjectLayer vol;

  GstVideoState state;
  guint8 profile_indication;
  VdpDecoderProfile profile;

  GstFlowReturn ret;

  if (mpeg4_dec->is_configured)
    return GST_FLOW_OK;

  if (!mpeg4_frame->vos_buf || !mpeg4_frame->vo_buf || !mpeg4_frame->vol_buf)
    goto skip_frame;

  if (!mpeg4_util_parse_VOS (mpeg4_frame->vos_buf, &vos))
    goto skip_frame;

  if (!mpeg4_util_parse_VO (mpeg4_frame->vo_buf, &vo))
    goto skip_frame;

  if (!mpeg4_util_parse_VOL (mpeg4_frame->vol_buf, &vo, &vol))
    goto skip_frame;

  state = gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER (mpeg4_dec));

  state.width = vol.width;
  state.height = vol.height;

  if (vol.fixed_vop_rate) {
    state.fps_n = vol.vop_time_increment_resolution;
    state.fps_d = vol.fixed_vop_time_increment;
  }

  state.par_n = vol.par_n;
  state.par_d = vol.par_d;

  gst_base_video_decoder_set_state (GST_BASE_VIDEO_DECODER (mpeg4_dec), state);

  profile_indication = vos.profile_and_level_indication >> 4;
  switch (profile_indication) {
    case 0x0:
      profile = VDP_DECODER_PROFILE_MPEG4_PART2_SP;
      break;

    case 0xf:
      profile = VDP_DECODER_PROFILE_MPEG4_PART2_ASP;
      break;

    default:
      goto unsupported_profile;
  }
  ret = gst_vdp_decoder_init_decoder (GST_VDP_DECODER (mpeg4_dec), profile, 2);
  if (ret != GST_FLOW_OK)
    return ret;

  mpeg4_dec->vol = vol;
  mpeg4_dec->is_configured = TRUE;

  return GST_FLOW_OK;

skip_frame:
  GST_WARNING ("Skipping frame since we're not configured yet");
  gst_base_video_decoder_skip_frame (GST_BASE_VIDEO_DECODER (mpeg4_dec),
      GST_VIDEO_FRAME (mpeg4_frame));
  return GST_FLOW_CUSTOM_ERROR;

unsupported_profile:
  GST_ELEMENT_ERROR (mpeg4_dec, STREAM, WRONG_TYPE,
      ("vdpaumpeg4dec doesn't support this streams profile"),
      ("profile_and_level_indication: %d", vos.profile_and_level_indication));
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_vdp_mpeg4_dec_handle_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame, GstClockTimeDiff deadline)
{
  GstVdpMpeg4Dec *mpeg4_dec = GST_VDP_MPEG4_DEC (base_video_decoder);

  GstMpeg4Frame *mpeg4_frame;
  GstFlowReturn ret;

  Mpeg4VideoObjectLayer *vol;
  Mpeg4VideoObjectPlane vop;

  VdpPictureInfoMPEG4Part2 info;
  VdpBitstreamBuffer bufs[1];
  GstVdpVideoBuffer *video_buf;

  mpeg4_frame = GST_MPEG4_FRAME (frame);

  ret = gst_vdp_mpeg4_dec_handle_configuration (mpeg4_dec, mpeg4_frame);
  if (ret != GST_FLOW_OK)
    return ret;

  vol = &mpeg4_dec->vol;
  if (!mpeg4_util_parse_VOP (mpeg4_frame->vop_buf, vol, &vop)) {
    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return GST_FLOW_CUSTOM_ERROR;
  }

  /* calculate vop time */
  mpeg4_frame->vop_time =
      vop.modulo_time_base * vol->vop_time_increment_resolution +
      vop.time_increment;

  if (mpeg4_dec->tframe == -1 && vop.coding_type == B_VOP)
    mpeg4_dec->tframe = mpeg4_frame->vop_time - mpeg4_dec->f_frame->vop_time;

  if (vop.coding_type != B_VOP) {
    if (mpeg4_dec->b_frame) {

      ret = gst_base_video_decoder_finish_frame (base_video_decoder,
          GST_VIDEO_FRAME_CAST (mpeg4_dec->b_frame));

      if (mpeg4_dec->f_frame)
        gst_video_frame_unref (GST_VIDEO_FRAME_CAST (mpeg4_dec->f_frame));

      mpeg4_dec->f_frame = mpeg4_dec->b_frame;
      mpeg4_dec->b_frame = NULL;
    }
  }

  info = gst_vdp_mpeg4_dec_fill_info (mpeg4_dec, mpeg4_frame, &vop);
  bufs[0].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  bufs[0].bitstream = GST_BUFFER_DATA (mpeg4_frame->vop_buf);
  bufs[0].bitstream_bytes = GST_BUFFER_SIZE (mpeg4_frame->vop_buf);

  ret = gst_vdp_decoder_render (GST_VDP_DECODER (base_video_decoder),
      (VdpPictureInfo *) & info, 1, bufs, &video_buf);
  if (ret != GST_FLOW_OK) {
    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return ret;
  }

  frame->src_buffer = GST_BUFFER_CAST (video_buf);

  if (vop.coding_type == B_VOP)
    ret = gst_base_video_decoder_finish_frame (base_video_decoder, frame);
  else {
    gst_video_frame_ref (GST_VIDEO_FRAME_CAST (mpeg4_frame));
    mpeg4_dec->b_frame = mpeg4_frame;
    ret = GST_FLOW_OK;
  }

  return ret;
}

static GstFlowReturn
gst_vdp_mpeg4_dec_parse_data (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf, gboolean at_eos, GstVideoFrame * frame)
{
  GstBitReader reader = GST_BIT_READER_INIT_FROM_BUFFER (buf);
  guint8 start_code;
  GstMpeg4Frame *mpeg4_frame;

  GstFlowReturn ret = GST_FLOW_OK;

  /* start code prefix */
  SKIP (&reader, 24);

  /* start_code */
  READ_UINT8 (&reader, start_code, 8);

  mpeg4_frame = GST_MPEG4_FRAME_CAST (frame);

  /* collect packages */
  if (start_code == MPEG4_PACKET_VOS) {
    if (mpeg4_frame->vop_buf)
      ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
          (GstVideoFrame **) & mpeg4_frame);

    gst_buffer_replace (&mpeg4_frame->vos_buf, buf);
  }

  else if (start_code == MPEG4_PACKET_EVOS) {
    if (mpeg4_frame->vop_buf)
      ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
          (GstVideoFrame **) & mpeg4_frame);
  }

  else if (start_code == MPEG4_PACKET_VO)
    gst_buffer_replace (&mpeg4_frame->vo_buf, buf);

  else if (start_code >= MPEG4_PACKET_VOL_MIN &&
      start_code <= MPEG4_PACKET_VOL_MAX)
    gst_buffer_replace (&mpeg4_frame->vol_buf, buf);

  else if (start_code == MPEG4_PACKET_GOV) {
    if (mpeg4_frame->vop_buf)
      ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
          (GstVideoFrame **) & mpeg4_frame);

    gst_buffer_replace (&mpeg4_frame->gov_buf, buf);
  }

  else if (start_code == MPEG4_PACKET_VOP) {
    if (mpeg4_frame->vop_buf)
      ret = gst_base_video_decoder_have_frame (base_video_decoder, FALSE,
          (GstVideoFrame **) & mpeg4_frame);

    mpeg4_frame->vop_buf = buf;
  }

  else
    gst_buffer_unref (buf);


  if (at_eos && mpeg4_frame->vop_buf)
    ret = gst_base_video_decoder_have_frame (base_video_decoder, TRUE,
        (GstVideoFrame **) & mpeg4_frame);

  return ret;

error:
  gst_buffer_unref (buf);
  GST_WARNING ("error parsing packet");
  return GST_FLOW_OK;
}

static gint
gst_vdp_mpeg4_dec_scan_for_sync (GstBaseVideoDecoder * base_video_decoder,
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
gst_vdp_mpeg4_dec_scan_for_packet_end (GstBaseVideoDecoder * base_video_decoder,
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

static GstVideoFrame *
gst_vdp_mpeg4_dec_create_frame (GstBaseVideoDecoder * base_video_decoder)
{
  return GST_VIDEO_FRAME_CAST (gst_mpeg4_frame_new ());
}

static gboolean
gst_vdp_mpeg4_dec_flush (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpMpeg4Dec *mpeg4_dec = GST_VDP_MPEG4_DEC (base_video_decoder);

  if (mpeg4_dec->b_frame) {
    gst_video_frame_unref (GST_VIDEO_FRAME_CAST (mpeg4_dec->b_frame));
    mpeg4_dec->b_frame = NULL;
  }

  if (mpeg4_dec->f_frame) {
    gst_video_frame_unref (GST_VIDEO_FRAME_CAST (mpeg4_dec->f_frame));
    mpeg4_dec->f_frame = NULL;
  }

  return TRUE;
}

static gboolean
gst_vdp_mpeg4_dec_start (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpMpeg4Dec *mpeg4_dec = GST_VDP_MPEG4_DEC (base_video_decoder);

  mpeg4_dec->is_configured = FALSE;
  mpeg4_dec->tframe = -1;

  mpeg4_dec->b_frame = NULL;
  mpeg4_dec->f_frame = NULL;

  return GST_BASE_VIDEO_DECODER_CLASS
      (parent_class)->start (base_video_decoder);
}

static gboolean
gst_vdp_mpeg4_dec_stop (GstBaseVideoDecoder * base_video_decoder)
{
  return GST_BASE_VIDEO_DECODER_CLASS (parent_class)->stop (base_video_decoder);
}

static void
gst_vdp_mpeg4_dec_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_static_metadata (element_class,
      "VDPAU Mpeg4 Decoder",
      "Decoder",
      "Decode mpeg4 stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
}

/* initialize the vdpaumpeg4decoder's class */
static void
gst_vdp_mpeg4_dec_class_init (GstVdpMpeg4DecClass * klass)
{
  GstBaseVideoDecoderClass *base_video_decoder_class;

  base_video_decoder_class = GST_BASE_VIDEO_DECODER_CLASS (klass);

  base_video_decoder_class->start = gst_vdp_mpeg4_dec_start;
  base_video_decoder_class->stop = gst_vdp_mpeg4_dec_stop;
  base_video_decoder_class->flush = gst_vdp_mpeg4_dec_flush;

  base_video_decoder_class->create_frame = gst_vdp_mpeg4_dec_create_frame;

  base_video_decoder_class->scan_for_sync = gst_vdp_mpeg4_dec_scan_for_sync;
  base_video_decoder_class->scan_for_packet_end =
      gst_vdp_mpeg4_dec_scan_for_packet_end;
  base_video_decoder_class->parse_data = gst_vdp_mpeg4_dec_parse_data;

  base_video_decoder_class->handle_frame = gst_vdp_mpeg4_dec_handle_frame;
}

static void
gst_vdp_mpeg4_dec_init (GstVdpMpeg4Dec * mpeg4_dec,
    GstVdpMpeg4DecClass * gclass)
{
}
