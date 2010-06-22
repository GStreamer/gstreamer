/* GStreamer
*
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/gstadapter.h>
#include <gst/base/gstbitreader.h>
#include <string.h>

#include "../gstvdp/gstvdpvideosrcpad.h"

#include "gstvdph264frame.h"

#include "gstvdph264dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_h264_dec_debug);
#define GST_CAT_DEFAULT gst_vdp_h264_dec_debug

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_BASE_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, " "parsed = (boolean) false")
    );

#define DEBUG_INIT(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_h264_dec_debug, "vdpauh264dec", 0, \
    "VDPAU h264 decoder");

GST_BOILERPLATE_FULL (GstVdpH264Dec, gst_vdp_h264_dec, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

#define SYNC_CODE_SIZE 3

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    return FALSE; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint16 (reader, &val, nbits)) { \
  GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    return FALSE; \
  } \
}

#define SKIP(reader, nbits) { \
  if (!gst_bit_reader_skip (reader, nbits)) { \
  GST_WARNING ("failed to skip nbits: %d", nbits); \
    return FALSE; \
  } \
}

static GstFlowReturn
gst_vdp_h264_dec_alloc_buffer (GstVdpH264Dec * h264_dec,
    GstVdpVideoBuffer ** outbuf)
{
  GstVdpVideoSrcPad *vdp_pad;
  GstFlowReturn ret = GST_FLOW_OK;

  vdp_pad = (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (h264_dec);
  ret = gst_vdp_video_src_pad_alloc_buffer (vdp_pad, outbuf);
  if (ret != GST_FLOW_OK)
    return ret;

  return GST_FLOW_OK;
}

static gboolean
gst_vdp_h264_dec_set_sink_caps (GstBaseVideoDecoder * base_video_decoder,
    GstCaps * caps)
{
  GstVdpH264Dec *h264_dec;
  GstStructure *structure;
  const GValue *value;

  h264_dec = GST_VDP_H264_DEC (base_video_decoder);

  structure = gst_caps_get_structure (caps, 0);
  /* packetized video has a codec_data */
  if ((value = gst_structure_get_value (structure, "codec_data"))) {
    GstBuffer *buf;
    GstBitReader reader;
    guint8 version;
    guint8 n_sps, n_pps;
    gint i;

    GST_DEBUG_OBJECT (h264_dec, "have packetized h264");
    h264_dec->packetized = TRUE;

    buf = gst_value_get_buffer (value);
    GST_MEMDUMP ("avcC:", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    /* parse the avcC data */
    if (GST_BUFFER_SIZE (buf) < 7) {
      GST_ERROR_OBJECT (h264_dec, "avcC size %u < 7", GST_BUFFER_SIZE (buf));
      return FALSE;
    }

    gst_bit_reader_init_from_buffer (&reader, buf);

    READ_UINT8 (&reader, version, 8);
    if (version != 1)
      return FALSE;

    SKIP (&reader, 30);

    READ_UINT8 (&reader, h264_dec->nal_length_size, 2);
    h264_dec->nal_length_size += 1;
    GST_DEBUG_OBJECT (h264_dec, "nal length %u", h264_dec->nal_length_size);

    SKIP (&reader, 3);

    READ_UINT8 (&reader, n_sps, 5);
    for (i = 0; i < n_sps; i++) {
      guint16 sps_length;
      guint8 *data;

      READ_UINT16 (&reader, sps_length, 16);
      sps_length -= 1;
      SKIP (&reader, 8);

      data = GST_BUFFER_DATA (buf) + gst_bit_reader_get_pos (&reader) / 8;
      if (!gst_h264_parser_parse_sequence (h264_dec->parser, data, sps_length))
        return FALSE;

      SKIP (&reader, sps_length * 8);
    }

    READ_UINT8 (&reader, n_pps, 8);
    for (i = 0; i < n_pps; i++) {
      guint16 pps_length;
      guint8 *data;

      READ_UINT16 (&reader, pps_length, 16);
      pps_length -= 1;
      SKIP (&reader, 8);

      data = GST_BUFFER_DATA (buf) + gst_bit_reader_get_pos (&reader) / 8;
      if (!gst_h264_parser_parse_picture (h264_dec->parser, data, pps_length))
        return FALSE;

      SKIP (&reader, pps_length * 8);
    }
  }

  return TRUE;
}

static GstFlowReturn
gst_vdp_h264_dec_shape_output (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf)
{
  GstVdpVideoSrcPad *vdp_pad;

  vdp_pad =
      (GstVdpVideoSrcPad *) GST_BASE_VIDEO_DECODER_SRC_PAD (base_video_decoder);

  return gst_vdp_video_src_pad_push (vdp_pad, GST_VDP_VIDEO_BUFFER (buf));
}

static void
gst_vdp_h264_dec_output (GstH264DPB * dpb, GstVdpH264Frame * h264_frame)
{
  GstBaseVideoDecoder *base_video_decoder;

  GST_DEBUG ("poc: %d", h264_frame->poc);

  base_video_decoder = g_object_get_data (G_OBJECT (dpb), "decoder");
  gst_base_video_decoder_finish_frame (base_video_decoder,
      GST_VIDEO_FRAME_CAST (h264_frame));
}

static guint
gst_vdp_h264_dec_calculate_poc (GstVdpH264Dec * h264_dec, GstH264Slice * slice)
{
  GstH264Picture *pic;
  GstH264Sequence *seq;

  guint poc;

  pic = slice->picture;
  seq = pic->sequence;

  if (seq->pic_order_cnt_type == 0) {
    guint32 max_poc_cnt_lsb = 1 << (seq->log2_max_pic_order_cnt_lsb_minus4 + 4);

    if ((slice->pic_order_cnt_lsb < h264_dec->prev_poc_lsb) &&
        ((h264_dec->prev_poc_lsb - slice->pic_order_cnt_lsb) >=
            (max_poc_cnt_lsb / 2)))
      h264_dec->poc_msb = h264_dec->poc_msb + max_poc_cnt_lsb;

    else if ((slice->pic_order_cnt_lsb > h264_dec->prev_poc_lsb) &&
        ((slice->pic_order_cnt_lsb - h264_dec->prev_poc_lsb) >
            (max_poc_cnt_lsb / 2)))
      h264_dec->poc_msb = h264_dec->poc_msb - max_poc_cnt_lsb;

    poc = h264_dec->poc_msb + slice->pic_order_cnt_lsb;

    h264_dec->prev_poc_lsb = slice->pic_order_cnt_lsb;
  }

  return poc;
}

static void
gst_vdp_h264_dec_init_frame_info (GstVdpH264Dec * h264_dec,
    GstVdpH264Frame * h264_frame)
{
  GstH264Slice *slice;

  slice = &h264_frame->slice_hdr;

  h264_frame->poc = gst_vdp_h264_dec_calculate_poc (h264_dec, slice);

  h264_frame->output_needed = TRUE;
  h264_frame->is_long_term = FALSE;
  h264_frame->frame_idx = slice->frame_num;

  /* is reference */
  if (slice->nal_unit.ref_idc == 0)
    h264_frame->is_reference = FALSE;
  else if (slice->nal_unit.IdrPicFlag) {
    h264_frame->is_reference = TRUE;
    if (slice->dec_ref_pic_marking.long_term_reference_flag) {
      h264_frame->is_long_term = TRUE;
      h264_frame->frame_idx = 0;
    }
  } else {
    h264_frame->is_reference = TRUE;

    if (slice->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
      GstH264RefPicMarking *marking;
      guint i;

      marking = slice->dec_ref_pic_marking.ref_pic_marking;
      for (i = 0; i < slice->dec_ref_pic_marking.n_ref_pic_marking; i++) {
        if (marking[i].memory_management_control_operation == 6) {
          h264_frame->is_long_term = TRUE;
          h264_frame->frame_idx = marking[i].long_term_frame_idx;
          break;
        }
      }
    }
  }

}

static gboolean
gst_vdp_h264_dec_idr (GstVdpH264Dec * h264_dec, GstVdpH264Frame * h264_frame)
{
  GstH264Slice *slice;
  GstH264Sequence *seq;

  h264_dec->poc_msb = 0;
  h264_dec->prev_poc_lsb = 0;

  slice = &h264_frame->slice_hdr;
  if (slice->dec_ref_pic_marking.no_output_of_prior_pics_flag)
    gst_h264_dpb_flush (h264_dec->dpb, FALSE);
  else
    gst_h264_dpb_flush (h264_dec->dpb, TRUE);

  if (slice->dec_ref_pic_marking.long_term_reference_flag)
    g_object_set (h264_dec->dpb, "max-longterm-frame-idx", 0, NULL);
  else
    g_object_set (h264_dec->dpb, "max-longterm-frame-idx", -1, NULL);

  seq = slice->picture->sequence;
  if (seq != h264_dec->sequence) {
    GstFlowReturn ret;
    GstVdpDevice *device;

    gst_base_video_decoder_update_src_caps (GST_BASE_VIDEO_DECODER (h264_dec));

    ret = gst_vdp_video_src_pad_get_device
        (GST_VDP_VIDEO_SRC_PAD (GST_BASE_VIDEO_DECODER_SRC_PAD (h264_dec)),
        &device, NULL);

    if (ret == GST_FLOW_OK) {
      GstVideoState *state;
      VdpDecoderProfile profile;
      VdpStatus status;

      if (h264_dec->decoder != VDP_INVALID_HANDLE) {
        device->vdp_decoder_destroy (h264_dec->decoder);
        h264_dec->decoder = VDP_INVALID_HANDLE;
      }

      state =
          gst_base_video_decoder_get_state (GST_BASE_VIDEO_DECODER (h264_dec));

      switch (seq->profile_idc) {
        case 66:
          profile = VDP_DECODER_PROFILE_H264_BASELINE;
          break;

        case 77:
          profile = VDP_DECODER_PROFILE_H264_MAIN;
          break;

        case 100:
          profile = VDP_DECODER_PROFILE_H264_HIGH;
          break;

        default:
          return FALSE;
      }

      status = device->vdp_decoder_create (device->device, profile,
          state->width, state->height, seq->num_ref_frames, &h264_dec->decoder);
      if (status != VDP_STATUS_OK) {
        GST_ELEMENT_ERROR (h264_dec, RESOURCE, READ,
            ("Could not create vdpau decoder"),
            ("Error returned from vdpau was: %s",
                device->vdp_get_error_string (status)));

        return FALSE;
      }
    } else
      return FALSE;

    g_object_set (h264_dec->dpb, "num-ref-frames", seq->num_ref_frames, NULL);

    h264_dec->sequence = seq;
  }

  return TRUE;
}

static VdpPictureInfoH264
gst_vdp_h264_dec_fill_info (GstVdpH264Dec * h264_dec,
    GstVdpH264Frame * h264_frame)
{
  GstH264Slice *slice;
  GstH264Picture *pic;
  GstH264Sequence *seq;
  VdpPictureInfoH264 info;

  slice = &h264_frame->slice_hdr;
  pic = slice->picture;
  seq = pic->sequence;

  info.slice_count = h264_frame->slices->len;

  /* FIXME: we only handle frames for now */
  info.field_order_cnt[0] = h264_frame->poc;
  info.field_order_cnt[1] = h264_frame->poc;

  info.is_reference = h264_frame->is_reference;
  info.frame_num = slice->frame_num;

  info.field_pic_flag = slice->field_pic_flag;
  info.bottom_field_flag = slice->bottom_field_flag;
  info.num_ref_idx_l0_active_minus1 = slice->num_ref_idx_l0_active_minus1;
  info.num_ref_idx_l1_active_minus1 = slice->num_ref_idx_l1_active_minus1;

  info.num_ref_frames = seq->num_ref_frames;
  info.frame_mbs_only_flag = seq->frame_mbs_only_flag;
  info.mb_adaptive_frame_field_flag = seq->mb_adaptive_frame_field_flag;
  info.log2_max_frame_num_minus4 = seq->log2_max_frame_num_minus4;
  info.pic_order_cnt_type = seq->pic_order_cnt_type;
  info.log2_max_pic_order_cnt_lsb_minus4 =
      seq->log2_max_pic_order_cnt_lsb_minus4;
  info.delta_pic_order_always_zero_flag = seq->delta_pic_order_always_zero_flag;
  info.direct_8x8_inference_flag = seq->direct_8x8_inference_flag;


  info.constrained_intra_pred_flag = pic->constrained_intra_pred_flag;
  info.weighted_pred_flag = pic->weighted_pred_flag;
  info.weighted_bipred_idc = pic->weighted_bipred_idc;
  info.transform_8x8_mode_flag = pic->transform_8x8_mode_flag;
  info.chroma_qp_index_offset = pic->chroma_qp_index_offset;
  info.second_chroma_qp_index_offset = pic->second_chroma_qp_index_offset;
  info.pic_init_qp_minus26 = pic->pic_init_qp_minus26;
  info.entropy_coding_mode_flag = pic->entropy_coding_mode_flag;
  info.pic_order_present_flag = pic->pic_order_present_flag;
  info.deblocking_filter_control_present_flag =
      pic->deblocking_filter_control_present_flag;
  info.redundant_pic_cnt_present_flag = pic->redundant_pic_cnt_present_flag;

  memcpy (&info.scaling_lists_4x4, &pic->scaling_lists_4x4, 96);
  memcpy (&info.scaling_lists_8x8, &pic->scaling_lists_8x8, 128);

  gst_h264_dpb_fill_reference_frames (h264_dec->dpb, info.referenceFrames);

  return info;
}

static VdpBitstreamBuffer *
gst_vdp_h264_dec_create_bitstream_buffers (GstVdpH264Dec * h264_dec,
    GstVdpH264Frame * h264_frame, guint * n_bufs)
{
  VdpBitstreamBuffer *bufs;

  if (h264_dec->packetized) {
    guint i;

    bufs = g_new (VdpBitstreamBuffer, h264_frame->slices->len * 2);
    *n_bufs = h264_frame->slices->len * 2;

    for (i = 0; i < h264_frame->slices->len; i++) {
      static const guint8 start_code[] = { 0x00, 0x00, 0x01 };
      guint idx;
      GstBuffer *buf;

      idx = i * 2;
      bufs[idx].bitstream = start_code;
      bufs[idx].bitstream_bytes = 3;
      bufs[idx].struct_version = VDP_BITSTREAM_BUFFER_VERSION;

      idx = idx + 1;
      buf = GST_BUFFER_CAST (g_ptr_array_index (h264_frame->slices, i));
      bufs[idx].bitstream = GST_BUFFER_DATA (buf) + h264_dec->nal_length_size;
      bufs[idx].bitstream_bytes = GST_BUFFER_SIZE (buf) -
          h264_dec->nal_length_size;
      bufs[idx].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
    }
  }

  else {
    guint i;

    bufs = g_new (VdpBitstreamBuffer, h264_frame->slices->len * 2);
    *n_bufs = h264_frame->slices->len * 2;

    for (i = 0; i < h264_frame->slices->len; i++) {
      GstBuffer *buf;

      buf = GST_BUFFER_CAST (g_ptr_array_index (h264_frame->slices, i));
      bufs[i].bitstream = GST_BUFFER_DATA (buf);
      bufs[i].bitstream_bytes = GST_BUFFER_SIZE (buf);
      bufs[i].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
    }
  }

  return bufs;
}

static GstFlowReturn
gst_vdp_h264_dec_handle_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame, GstClockTimeDiff deadline)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);

  GstVdpH264Frame *h264_frame;
  GstH264Slice *slice;
  GstH264Picture *pic;
  GstH264Sequence *seq;

  GstFlowReturn ret;
  GstVdpVideoBuffer *outbuf;
  VdpPictureInfoH264 info;
  GstVdpDevice *device;
  VdpVideoSurface surface;
  VdpBitstreamBuffer *bufs;
  guint n_bufs;
  VdpStatus status;

  GST_DEBUG ("handle_frame");

  h264_frame = (GstVdpH264Frame *) frame;

  slice = &h264_frame->slice_hdr;
  pic = slice->picture;
  seq = pic->sequence;


  if (slice->nal_unit.IdrPicFlag) {
    if (gst_vdp_h264_dec_idr (h264_dec, h264_frame))
      h264_dec->got_idr = TRUE;
    else {
      gst_base_video_decoder_skip_frame (base_video_decoder, frame);
      return GST_FLOW_OK;
    }
  }

  /* check if we've got a IDR frame yet */
  if (!h264_dec->got_idr) {
    gst_base_video_decoder_skip_frame (base_video_decoder, frame);
    return GST_FLOW_OK;
  }



  gst_vdp_h264_dec_init_frame_info (h264_dec, h264_frame);



  /* decoding */
  if ((ret = gst_vdp_h264_dec_alloc_buffer (h264_dec, &outbuf) != GST_FLOW_OK))
    goto alloc_error;

  device = GST_VDP_VIDEO_BUFFER (outbuf)->device;
  surface = GST_VDP_VIDEO_BUFFER (outbuf)->surface;

  info = gst_vdp_h264_dec_fill_info (h264_dec, h264_frame);
  bufs = gst_vdp_h264_dec_create_bitstream_buffers (h264_dec, h264_frame,
      &n_bufs);

  status = device->vdp_decoder_render (h264_dec->decoder, surface,
      (VdpPictureInfo *) & info, n_bufs, bufs);

  g_free (bufs);
  if (status != VDP_STATUS_OK)
    goto decode_error;

  frame->src_buffer = GST_BUFFER_CAST (outbuf);


  /* DPB handling */
  if (slice->nal_unit.ref_idc != 0 && !slice->nal_unit.IdrPicFlag) {
    if (slice->dec_ref_pic_marking.adaptive_ref_pic_marking_mode_flag) {
      GstH264RefPicMarking *marking;
      guint i;

      marking = slice->dec_ref_pic_marking.ref_pic_marking;
      for (i = 0; i < slice->dec_ref_pic_marking.n_ref_pic_marking; i++) {

        switch (marking[i].memory_management_control_operation) {
          case 1:
          {
            guint16 pic_num;

            pic_num = slice->frame_num -
                (marking[i].difference_of_pic_nums_minus1 + 1);
            gst_h264_dpb_mark_short_term_unused (h264_dec->dpb, pic_num);
            break;
          }

          case 2:
          {
            gst_h264_dpb_mark_long_term_unused (h264_dec->dpb,
                marking[i].long_term_pic_num);
            break;
          }

          case 3:
          {
            guint16 pic_num;

            pic_num = slice->frame_num -
                (marking[i].difference_of_pic_nums_minus1 + 1);
            gst_h264_dpb_mark_long_term (h264_dec->dpb, pic_num,
                marking[i].long_term_frame_idx);
            break;
          }

          case 4:
          {
            g_object_set (h264_dec->dpb, "max-longterm-frame-idx",
                marking[i].max_long_term_frame_idx_plus1 - 1, NULL);
            break;
          }

          case 5:
          {
            gst_h264_dpb_mark_all_unused (h264_dec->dpb);
            g_object_set (h264_dec->dpb, "max-longterm-frame-idx", -1, NULL);
            break;
          }

          default:
            break;
        }
      }
    } else
      gst_h264_dpb_mark_sliding (h264_dec->dpb);
  }

  gst_h264_dpb_add (h264_dec->dpb, h264_frame);

  return GST_FLOW_OK;

alloc_error:
  gst_base_video_decoder_skip_frame (base_video_decoder, frame);
  return ret;

decode_error:
  GST_ELEMENT_ERROR (h264_dec, RESOURCE, READ,
      ("Could not decode"),
      ("Error returned from vdpau was: %s",
          device->vdp_get_error_string (status)));

  gst_buffer_unref (GST_BUFFER_CAST (outbuf));
  gst_base_video_decoder_skip_frame (base_video_decoder, frame);

  return GST_FLOW_ERROR;
}

static gint
gst_vdp_h264_dec_scan_for_sync (GstBaseVideoDecoder * base_video_decoder,
    GstAdapter * adapter)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);
  gint m;

  if (h264_dec->packetized)
    return 0;

  m = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
      0, gst_adapter_available (adapter));
  if (m == -1)
    return gst_adapter_available (adapter) - SYNC_CODE_SIZE;

  return m;
}

static GstBaseVideoDecoderScanResult
gst_vdp_h264_dec_scan_for_packet_end (GstBaseVideoDecoder * base_video_decoder,
    GstAdapter * adapter, guint * size, gboolean at_eos)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);
  guint avail;

  avail = gst_adapter_available (adapter);
  if (avail < h264_dec->nal_length_size)
    return GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA;

  if (h264_dec->packetized) {
    guint8 *data;
    gint i;
    guint32 nal_length;

    data = g_slice_alloc (h264_dec->nal_length_size);
    gst_adapter_copy (adapter, data, 0, h264_dec->nal_length_size);
    for (i = 0; i < h264_dec->nal_length_size; i++)
      nal_length = (nal_length << 8) | data[i];

    g_slice_free1 (h264_dec->nal_length_size, data);

    nal_length += h264_dec->nal_length_size;

    /* check for invalid NALU sizes, assume the size if the available bytes
     * when something is fishy */
    if (nal_length <= 1 || nal_length > avail) {
      nal_length = avail - h264_dec->nal_length_size;
      GST_DEBUG ("fixing invalid NALU size to %u", nal_length);
    }

    *size = nal_length;
  }

  else {
    guint8 *data;
    guint32 start_code;
    guint n;

    data = g_slice_alloc (SYNC_CODE_SIZE);
    gst_adapter_copy (adapter, data, 0, SYNC_CODE_SIZE);
    start_code = ((data[0] << 16) && (data[1] << 8) && data[2]);
    g_slice_free1 (SYNC_CODE_SIZE, data);

    GST_DEBUG ("start_code: %d", start_code);
    if (start_code == 0x000001)
      return GST_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC;

    n = gst_adapter_masked_scan_uint32 (adapter, 0xffffff00, 0x00000100,
        SYNC_CODE_SIZE, avail - SYNC_CODE_SIZE);
    if (n == -1)
      return GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA;

    *size = n;
  }

  GST_DEBUG ("NAL size: %d", *size);

  return GST_BASE_VIDEO_DECODER_SCAN_RESULT_OK;
}

static GstFlowReturn
gst_vdp_h264_dec_parse_data (GstBaseVideoDecoder * base_video_decoder,
    GstBuffer * buf, gboolean at_eos)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);
  GstBitReader reader;
  GstNalUnit nal_unit;
  guint8 forbidden_zero_bit;

  guint8 *data;
  guint size;
  gint i;

  GstVideoFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_MEMDUMP ("data", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  gst_bit_reader_init_from_buffer (&reader, buf);

  /* skip nal_length or sync code */
  gst_bit_reader_skip (&reader, h264_dec->nal_length_size * 8);

  if (!gst_bit_reader_get_bits_uint8 (&reader, &forbidden_zero_bit, 1))
    goto invalid_packet;
  if (forbidden_zero_bit != 0) {
    GST_WARNING ("forbidden_zero_bit != 0");
    return GST_FLOW_ERROR;
  }

  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_unit.ref_idc, 2))
    goto invalid_packet;
  GST_DEBUG ("nal_ref_idc: %u", nal_unit.ref_idc);

  /* read nal_unit_type */
  if (!gst_bit_reader_get_bits_uint16 (&reader, &nal_unit.type, 5))
    goto invalid_packet;

  GST_DEBUG ("nal_unit_type: %u", nal_unit.type);
  if (nal_unit.type == 14 || nal_unit.type == 20) {
    if (!gst_bit_reader_skip (&reader, 24))
      goto invalid_packet;
  }
  nal_unit.IdrPicFlag = (nal_unit.type == 5 ? 1 : 0);

  data = GST_BUFFER_DATA (buf) + gst_bit_reader_get_pos (&reader) / 8;
  size = gst_bit_reader_get_remaining (&reader) / 8;

  i = size - 1;
  while (size >= 0 && data[i] == 0x00) {
    size--;
    i--;
  }

  frame = gst_base_video_decoder_get_current_frame (base_video_decoder);

  /* does this mark the beginning of a new access unit */
  if (nal_unit.type == GST_NAL_AU_DELIMITER) {
    ret = gst_base_video_decoder_have_frame (base_video_decoder, &frame);
    gst_base_video_decoder_frame_start (base_video_decoder, buf);
  }

  if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VDP_H264_FRAME_GOT_PRIMARY)) {
    if (nal_unit.type == GST_NAL_SPS || nal_unit.type == GST_NAL_PPS ||
        nal_unit.type == GST_NAL_SEI ||
        (nal_unit.type >= 14 && nal_unit.type <= 18)) {
      ret = gst_base_video_decoder_have_frame (base_video_decoder, &frame);
      gst_base_video_decoder_frame_start (base_video_decoder, buf);
    }
  }

  if (nal_unit.type >= GST_NAL_SLICE && nal_unit.type <= GST_NAL_SLICE_IDR) {
    GstH264Slice slice;

    if (!gst_h264_parser_parse_slice_header (h264_dec->parser, &slice, data,
            size, nal_unit))
      goto invalid_packet;

    if (slice.redundant_pic_cnt == 0) {
      if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VDP_H264_FRAME_GOT_PRIMARY)) {
        GstH264Slice *p_slice;
        guint8 pic_order_cnt_type, p_pic_order_cnt_type;
        gboolean finish_frame = FALSE;

        p_slice = &(GST_VDP_H264_FRAME_CAST (frame)->slice_hdr);
        pic_order_cnt_type = slice.picture->sequence->pic_order_cnt_type;
        p_pic_order_cnt_type = p_slice->picture->sequence->pic_order_cnt_type;

        if (slice.frame_num != p_slice->frame_num)
          finish_frame = TRUE;

        else if (slice.picture != p_slice->picture)
          finish_frame = TRUE;

        else if (slice.bottom_field_flag != p_slice->bottom_field_flag)
          finish_frame = TRUE;

        else if (nal_unit.ref_idc != p_slice->nal_unit.ref_idc &&
            (nal_unit.ref_idc == 0 || p_slice->nal_unit.ref_idc == 0))
          finish_frame = TRUE;

        else if ((pic_order_cnt_type == 0 && p_pic_order_cnt_type == 0) &&
            (slice.pic_order_cnt_lsb != p_slice->pic_order_cnt_lsb ||
                slice.delta_pic_order_cnt_bottom !=
                p_slice->delta_pic_order_cnt_bottom))
          finish_frame = TRUE;

        else if ((p_pic_order_cnt_type == 1 && p_pic_order_cnt_type == 1) &&
            (slice.delta_pic_order_cnt[0] != p_slice->delta_pic_order_cnt[0] ||
                slice.delta_pic_order_cnt[1] !=
                p_slice->delta_pic_order_cnt[1]))
          finish_frame = TRUE;

        if (finish_frame) {
          ret = gst_base_video_decoder_have_frame (base_video_decoder, &frame);
          gst_base_video_decoder_frame_start (base_video_decoder, buf);
        }
      }

      if (!GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VDP_H264_FRAME_GOT_PRIMARY)) {
        if (GST_H264_IS_I_SLICE (slice.type)
            || GST_H264_IS_SI_SLICE (slice.type))
          GST_VIDEO_FRAME_FLAG_SET (frame, GST_VIDEO_FRAME_FLAG_KEYFRAME);

        GST_VDP_H264_FRAME_CAST (frame)->slice_hdr = slice;
        GST_VIDEO_FRAME_FLAG_SET (frame, GST_VDP_H264_FRAME_GOT_PRIMARY);
      }
    }
    gst_vdp_h264_frame_add_slice ((GstVdpH264Frame *) frame, buf);
  }

  if (nal_unit.type == GST_NAL_SPS) {
    if (!gst_h264_parser_parse_sequence (h264_dec->parser, data, size))
      goto invalid_packet;
  }

  if (nal_unit.type == GST_NAL_PPS) {
    if (!gst_h264_parser_parse_picture (h264_dec->parser, data, size))
      goto invalid_packet;
  }

  gst_buffer_unref (buf);
  return ret;

invalid_packet:
  GST_WARNING ("Invalid packet size!");
  gst_buffer_unref (buf);

  return GST_FLOW_OK;
}

static GstVideoFrame *
gst_vdp_h264_dec_create_frame (GstBaseVideoDecoder * base_video_decoder)
{
  return GST_VIDEO_FRAME_CAST (gst_vdp_h264_frame_new ());
}

static GstPad *
gst_vdp_h264_dec_create_srcpad (GstBaseVideoDecoder * base_video_decoder,
    GstBaseVideoDecoderClass * base_video_decoder_class)
{
  GstPadTemplate *pad_template;
  GstVdpVideoSrcPad *vdp_pad;

  pad_template = gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (base_video_decoder_class),
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  vdp_pad = gst_vdp_video_src_pad_new (pad_template,
      GST_BASE_VIDEO_DECODER_SRC_NAME);

  return GST_PAD (vdp_pad);
}

static gboolean
gst_vdp_h264_dec_flush (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);

  h264_dec->got_idr = FALSE;
  gst_h264_dpb_flush (h264_dec->dpb, FALSE);

  return TRUE;
}

static gboolean
gst_vdp_h264_dec_start (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);

  h264_dec->packetized = FALSE;
  h264_dec->nal_length_size = SYNC_CODE_SIZE;

  h264_dec->got_idr = FALSE;
  h264_dec->sequence = NULL;

  h264_dec->parser = g_object_new (GST_TYPE_H264_PARSER, NULL);

  h264_dec->dpb = g_object_new (GST_TYPE_H264_DPB, NULL);
  g_object_set_data (G_OBJECT (h264_dec->dpb), "decoder", h264_dec);
  h264_dec->dpb->output = gst_vdp_h264_dec_output;

  return TRUE;
}

static gboolean
gst_vdp_h264_dec_stop (GstBaseVideoDecoder * base_video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (base_video_decoder);

  GstVdpVideoSrcPad *vdp_pad;
  GstFlowReturn ret;
  GstVdpDevice *device;

  g_object_unref (h264_dec->parser);
  g_object_unref (h264_dec->dpb);

  vdp_pad =
      GST_VDP_VIDEO_SRC_PAD (GST_BASE_VIDEO_DECODER_SRC_PAD
      (base_video_decoder));

  ret = gst_vdp_video_src_pad_get_device (vdp_pad, &device, NULL);
  if (ret == GST_FLOW_OK) {

    if (h264_dec->decoder != VDP_INVALID_HANDLE)
      device->vdp_decoder_destroy (h264_dec->decoder);
  }

  return TRUE;
}

static void
gst_vdp_h264_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  GstCaps *src_caps;
  GstPadTemplate *src_template;

  gst_element_class_set_details_simple (element_class,
      "VDPAU H264 Decoder",
      "Decoder",
      "Decode h264 stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  src_caps = gst_vdp_video_buffer_get_caps (TRUE, VDP_CHROMA_TYPE_420);
  src_template = gst_pad_template_new (GST_BASE_VIDEO_DECODER_SRC_NAME,
      GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);

  gst_element_class_add_pad_template (element_class, src_template);
}

static void
gst_vdp_h264_dec_init (GstVdpH264Dec * h264_dec, GstVdpH264DecClass * klass)
{
}

static void
gst_vdp_h264_dec_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vdp_h264_dec_class_init (GstVdpH264DecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseVideoDecoderClass *base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_vdp_h264_dec_finalize;

  base_video_decoder_class->start = gst_vdp_h264_dec_start;
  base_video_decoder_class->stop = gst_vdp_h264_dec_stop;
  base_video_decoder_class->flush = gst_vdp_h264_dec_flush;

  base_video_decoder_class->create_srcpad = gst_vdp_h264_dec_create_srcpad;
  base_video_decoder_class->set_sink_caps = gst_vdp_h264_dec_set_sink_caps;

  base_video_decoder_class->scan_for_sync = gst_vdp_h264_dec_scan_for_sync;
  base_video_decoder_class->scan_for_packet_end =
      gst_vdp_h264_dec_scan_for_packet_end;
  base_video_decoder_class->parse_data = gst_vdp_h264_dec_parse_data;

  base_video_decoder_class->handle_frame = gst_vdp_h264_dec_handle_frame;
  base_video_decoder_class->create_frame = gst_vdp_h264_dec_create_frame;

  base_video_decoder_class->shape_output = gst_vdp_h264_dec_shape_output;
}
