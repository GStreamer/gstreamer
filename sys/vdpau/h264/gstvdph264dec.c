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
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gsth264meta.h>
#include <string.h>

#include "gstvdph264dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_h264_dec_debug);
#define GST_CAT_DEFAULT gst_vdp_h264_dec_debug

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (GST_VIDEO_DECODER_SINK_NAME,
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264,stream-format=byte-stream,alignment=au")
    );

#define DEBUG_INIT \
    GST_DEBUG_CATEGORY_INIT (gst_vdp_h264_dec_debug, "vdpauh264dec", 0, \
    "VDPAU h264 decoder");

#define gst_vdp_h264_dec_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVdpH264Dec, gst_vdp_h264_dec, GST_TYPE_VDP_DECODER,
    DEBUG_INIT);

static GstFlowReturn
gst_vdp_h264_dec_output (GstH264DPB * dpb, GstH264Frame * h264_frame,
    gpointer user_data)
{
  GstVideoDecoder *video_decoder = (GstVideoDecoder *) user_data;

  GST_DEBUG ("poc: %d", h264_frame->poc);

  return gst_video_decoder_finish_frame (video_decoder, h264_frame->frame);
}

static guint
gst_vdp_h264_dec_calculate_poc (GstVdpH264Dec * h264_dec,
    GstH264SliceHdr * slice)
{
  GstH264SPS *seq;
  guint poc = 0;

  seq = slice->pps->sequence;

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
    GstH264Frame * h264_frame, GstH264SliceHdr * slice)
{
  h264_frame->poc = gst_vdp_h264_dec_calculate_poc (h264_dec, slice);

  h264_frame->output_needed = TRUE;
  h264_frame->is_long_term = FALSE;
  h264_frame->frame_idx = slice->frame_num;

  /* is reference */
  if (slice->nalu_ref_idc == 0)
    h264_frame->is_reference = FALSE;
  else if (slice->slice_type == GST_H264_NAL_SLICE_IDR) {
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

static GstFlowReturn
gst_vdp_h264_dec_idr (GstVdpH264Dec * h264_dec, GstVideoCodecFrame * frame,
    GstH264SliceHdr * slice)
{
  GstH264SPS *seq;

  GST_DEBUG_OBJECT (h264_dec, "Handling IDR slice");

  h264_dec->poc_msb = 0;
  h264_dec->prev_poc_lsb = 0;

  if (slice->dec_ref_pic_marking.no_output_of_prior_pics_flag)
    gst_h264_dpb_flush (h264_dec->dpb, FALSE);
  else
    gst_h264_dpb_flush (h264_dec->dpb, TRUE);

  if (slice->dec_ref_pic_marking.long_term_reference_flag)
    g_object_set (h264_dec->dpb, "max-longterm-frame-idx", 0, NULL);
  else
    g_object_set (h264_dec->dpb, "max-longterm-frame-idx", -1, NULL);

  seq = slice->pps->sequence;

  if (seq->id != h264_dec->current_sps) {
    GstVideoCodecState *state;
    VdpDecoderProfile profile;
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (h264_dec, "Sequence changed !");

    state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER (h264_dec),
        GST_VIDEO_FORMAT_YV12, seq->width, seq->height, h264_dec->input_state);

    /* calculate framerate if we haven't got one */
    if (state->info.fps_n == 0) {
      state->info.fps_n = seq->fps_num;
      state->info.fps_d = seq->fps_den;
    }
    if (state->info.par_n == 0 && seq->vui_parameters_present_flag) {
      state->info.par_n = seq->vui_parameters.par_n;
      state->info.par_d = seq->vui_parameters.par_d;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (h264_dec)))
      goto nego_fail;

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
        goto profile_not_suported;
    }

    ret = gst_vdp_decoder_init_decoder (GST_VDP_DECODER (h264_dec), profile,
        seq->num_ref_frames, h264_dec->input_state);
    if (ret != GST_FLOW_OK)
      return ret;

    g_object_set (h264_dec->dpb, "num-ref-frames", seq->num_ref_frames, NULL);

    h264_dec->current_sps = seq->id;
  }

  return GST_FLOW_OK;

profile_not_suported:
  {
    GST_ELEMENT_ERROR (h264_dec, STREAM, WRONG_TYPE,
        ("vdpauh264dec doesn't support this streams profile"),
        ("profile_idc: %d", seq->profile_idc));
    return GST_FLOW_ERROR;
  }

nego_fail:
  {
    GST_ERROR_OBJECT (h264_dec, "Negotiation failed");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static void
gst_vdp_h264_dec_fill_info (VdpPictureInfoH264 * info, GstVdpH264Dec * h264_dec,
    GstH264Frame * h264_frame, GstH264SliceHdr * slice)
{
  GstH264PPS *pic;
  GstH264SPS *seq;

  pic = slice->pps;
  seq = pic->sequence;

  GST_DEBUG_OBJECT (h264_dec, "Filling info");

  /* FIXME: we only handle frames for now */
  info->field_order_cnt[0] = h264_frame->poc;
  info->field_order_cnt[1] = h264_frame->poc;

  info->is_reference = h264_frame->is_reference;
  info->frame_num = slice->frame_num;

  info->field_pic_flag = slice->field_pic_flag;
  info->bottom_field_flag = slice->bottom_field_flag;
  info->num_ref_idx_l0_active_minus1 = slice->num_ref_idx_l0_active_minus1;
  info->num_ref_idx_l1_active_minus1 = slice->num_ref_idx_l1_active_minus1;

  info->num_ref_frames = seq->num_ref_frames;
  info->mb_adaptive_frame_field_flag = seq->mb_adaptive_frame_field_flag;
  info->frame_mbs_only_flag = seq->frame_mbs_only_flag;
  info->log2_max_frame_num_minus4 = seq->log2_max_frame_num_minus4;
  info->pic_order_cnt_type = seq->pic_order_cnt_type;
  info->log2_max_pic_order_cnt_lsb_minus4 =
      seq->log2_max_pic_order_cnt_lsb_minus4;
  info->delta_pic_order_always_zero_flag =
      seq->delta_pic_order_always_zero_flag;
  info->direct_8x8_inference_flag = seq->direct_8x8_inference_flag;

  info->constrained_intra_pred_flag = pic->constrained_intra_pred_flag;
  info->weighted_pred_flag = pic->weighted_pred_flag;
  info->weighted_bipred_idc = pic->weighted_bipred_idc;
  info->transform_8x8_mode_flag = pic->transform_8x8_mode_flag;
  info->chroma_qp_index_offset = pic->chroma_qp_index_offset;
  info->second_chroma_qp_index_offset = pic->second_chroma_qp_index_offset;
  info->pic_init_qp_minus26 = pic->pic_init_qp_minus26;
  info->entropy_coding_mode_flag = pic->entropy_coding_mode_flag;
  info->pic_order_present_flag = pic->pic_order_present_flag;
  info->deblocking_filter_control_present_flag =
      pic->deblocking_filter_control_present_flag;
  info->redundant_pic_cnt_present_flag = pic->redundant_pic_cnt_present_flag;

  memcpy (&info->scaling_lists_4x4, &pic->scaling_lists_4x4, 96);
  memcpy (&info->scaling_lists_8x8, &pic->scaling_lists_8x8, 128);

  gst_h264_dpb_fill_reference_frames (h264_dec->dpb, info->referenceFrames);
}

static VdpBitstreamBuffer *
gst_vdp_h264_dec_create_bitstream_buffers (GstVdpH264Dec * h264_dec,
    GstH264Meta * meta, GstMapInfo * info)
{
  VdpBitstreamBuffer *bufs;
  guint i;
  gsize offset = 0;

  bufs = g_new (VdpBitstreamBuffer, meta->num_slices);

  for (i = 0; i < meta->num_slices; i++) {
    bufs[i].bitstream = info->data + offset;
    if (i == meta->num_slices)
      offset = info->size;
    else
      offset = meta->slice_offsets[i + 1];
    bufs[i].bitstream_bytes = offset - meta->slice_offsets[i];
    bufs[i].struct_version = VDP_BITSTREAM_BUFFER_VERSION;
  }

  return bufs;
}

static GstFlowReturn
gst_vdp_h264_dec_handle_dpb (GstVdpH264Dec * h264_dec,
    GstH264Frame * h264_frame, GstH264SliceHdr * slice)
{
  if (slice->nalu_ref_idc != 0 && slice->slice_type != GST_H264_NAL_SLICE_IDR) {
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

  return gst_h264_dpb_add (h264_dec->dpb, h264_frame);

}

static void
gst_h264_frame_free (GstH264Frame * frame)
{
  g_slice_free (GstH264Frame, frame);
}

static GstFlowReturn
gst_vdp_h264_dec_handle_frame (GstVideoDecoder * video_decoder,
    GstVideoCodecFrame * frame)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (video_decoder);
  GstH264Meta *h264_meta;
  GstH264Frame *h264_frame;
  GList *tmp;
  GstFlowReturn ret;
  VdpPictureInfoH264 info;
  VdpBitstreamBuffer *bufs;
  GstH264SliceHdr *first_slice;
  guint i;
  GstMapInfo map;

  GST_DEBUG ("handle_frame");

  h264_meta = gst_buffer_get_h264_meta (frame->input_buffer);
  if (G_UNLIKELY (h264_meta == NULL))
    goto no_h264_meta;

  if (G_UNLIKELY (h264_meta->num_slices == 0))
    goto no_slices;

  /* Handle PPS/SPS/SEI if present */
  if (h264_meta->sps) {
    for (tmp = h264_meta->sps; tmp; tmp = tmp->next) {
      GstH264SPS *sps = (GstH264SPS *) tmp->data;
      GST_LOG_OBJECT (h264_dec, "Storing SPS %d", sps->id);
      h264_dec->sps[sps->id] = g_slice_dup (GstH264SPS, sps);
    }
  }
  if (h264_meta->pps) {
    for (tmp = h264_meta->pps; tmp; tmp = tmp->next) {
      GstH264PPS *pps = (GstH264PPS *) tmp->data;
      GST_LOG_OBJECT (h264_dec, "Storing PPS %d", pps->id);
      h264_dec->pps[pps->id] = g_slice_dup (GstH264PPS, pps);
      /* Adjust pps pointer */
      h264_dec->pps[pps->id]->sequence = h264_dec->sps[pps->sps_id];
    }
  }

  first_slice = &h264_meta->slices[0];

  if (!h264_dec->got_idr && first_slice->slice_type != GST_H264_NAL_SLICE_IDR)
    goto no_idr;

  /* Handle slices */
  for (i = 0; i < h264_meta->num_slices; i++) {
    GstH264SliceHdr *slice = &h264_meta->slices[i];

    GST_LOG_OBJECT (h264_dec, "Handling slice #%d", i);
    slice->pps = h264_dec->pps[slice->pps_id];
  }

  if (first_slice->slice_type == GST_H264_NAL_SLICE_IDR) {
    ret = gst_vdp_h264_dec_idr (h264_dec, frame, first_slice);
    if (ret == GST_FLOW_OK)
      h264_dec->got_idr = TRUE;
    else
      goto skip_frame;
  }

  h264_frame = g_slice_new0 (GstH264Frame);
  gst_video_codec_frame_set_user_data (frame, h264_frame,
      (GDestroyNotify) gst_h264_frame_free);

  gst_vdp_h264_dec_init_frame_info (h264_dec, h264_frame, first_slice);
  h264_frame->frame = frame;
  gst_vdp_h264_dec_fill_info (&info, h264_dec, h264_frame, first_slice);
  info.slice_count = h264_meta->num_slices;

  if (!gst_buffer_map (frame->input_buffer, &map, GST_MAP_READ))
    goto map_fail;
  bufs = gst_vdp_h264_dec_create_bitstream_buffers (h264_dec, h264_meta, &map);

  ret = gst_vdp_decoder_render (GST_VDP_DECODER (h264_dec),
      (VdpPictureInfo *) & info, h264_meta->num_slices, bufs, frame);
  g_free (bufs);
  gst_buffer_unmap (frame->input_buffer, &map);

  if (ret != GST_FLOW_OK)
    goto render_fail;

  /* DPB handling */
  return gst_vdp_h264_dec_handle_dpb (h264_dec, h264_frame, first_slice);

  /* EARLY exit */
no_idr:
  {
    GST_DEBUG_OBJECT (video_decoder, "Didn't see a IDR yet, skipping frame");
    return gst_video_decoder_finish_frame (video_decoder, frame);
  }

skip_frame:
  {
    GST_DEBUG_OBJECT (video_decoder, "Skipping frame");
    return gst_video_decoder_finish_frame (video_decoder, frame);
  }

  /* ERRORS */
no_h264_meta:
  {
    GST_ERROR_OBJECT (video_decoder, "Input buffer doesn't have GstH264Meta");
    return GST_FLOW_ERROR;
  }

no_slices:
  {
    GST_ERROR_OBJECT (video_decoder, "Input buffer doesn't have any slices");
    return GST_FLOW_ERROR;
  }

map_fail:
  {
    GST_ERROR_OBJECT (video_decoder, "Failed to map input buffer for READ");
    return GST_FLOW_ERROR;
  }

render_fail:
  {
    GST_ERROR_OBJECT (video_decoder, "Failed to render : %s",
        gst_flow_get_name (ret));
    gst_video_decoder_drop_frame (video_decoder, frame);
    return ret;
  }
}


static gboolean
gst_vdp_h264_dec_flush (GstVideoDecoder * video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (video_decoder);

  h264_dec->got_idr = FALSE;
  gst_h264_dpb_flush (h264_dec->dpb, FALSE);

  return TRUE;
}

static gboolean
gst_vdp_h264_dec_start (GstVideoDecoder * video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (video_decoder);

  h264_dec->got_idr = FALSE;
  h264_dec->current_sps = -1;
  h264_dec->got_idr = FALSE;

  h264_dec->dpb = g_object_new (GST_TYPE_H264_DPB, NULL);
  gst_h264_dpb_set_output_func (h264_dec->dpb, gst_vdp_h264_dec_output,
      h264_dec);

  return GST_VIDEO_DECODER_CLASS (parent_class)->start (video_decoder);
}

static gboolean
gst_vdp_h264_dec_stop (GstVideoDecoder * video_decoder)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (video_decoder);

  g_object_unref (h264_dec->dpb);

  return GST_VIDEO_DECODER_CLASS (parent_class)->stop (video_decoder);
}

static gboolean
gst_vdp_h264_dec_set_format (GstVideoDecoder * video_decoder,
    GstVideoCodecState * state)
{
  GstVdpH264Dec *h264_dec = GST_VDP_H264_DEC (video_decoder);

  if (h264_dec->input_state)
    gst_video_codec_state_unref (h264_dec->input_state);

  h264_dec->input_state = gst_video_codec_state_ref (state);

  GST_FIXME_OBJECT (video_decoder, "Do something when receiving input state ?");

  return TRUE;
}

static void
gst_vdp_h264_dec_init (GstVdpH264Dec * h264_dec)
{
}

static void
gst_vdp_h264_dec_class_init (GstVdpH264DecClass * klass)
{
  GstElementClass *element_class;
  GstVideoDecoderClass *video_decoder_class;

  element_class = GST_ELEMENT_CLASS (klass);
  video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "VDPAU H264 Decoder",
      "Decoder",
      "Decode h264 stream with vdpau",
      "Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  video_decoder_class->start = gst_vdp_h264_dec_start;
  video_decoder_class->stop = gst_vdp_h264_dec_stop;
  video_decoder_class->flush = gst_vdp_h264_dec_flush;

  video_decoder_class->set_format = gst_vdp_h264_dec_set_format;

  video_decoder_class->handle_frame = gst_vdp_h264_dec_handle_frame;
}
