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
 * SECTION:element-d3d11vp8dec
 * @title: d3d11vp8dec
 *
 * A Direct3D11/DXVA based VP8 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/vp8/file ! parsebin ! d3d11vp8dec ! d3d11videosink
 * ```
 *
 * Since: 1.18
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11vp8dec.h"

#include <gst/codecs/gstvp8decoder.h>
#include <string.h>
#include <vector>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_vp8_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_vp8_dec_debug

/* *INDENT-OFF* */
typedef struct _GstD3D11Vp8DecInner
{
  GstD3D11Device *device = nullptr;
  GstD3D11Decoder *d3d11_decoder = nullptr;

  DXVA_PicParams_VP8 pic_params;
  DXVA_Slice_VPx_Short slice;

  /* In case of VP8, there's only one slice per picture so we don't
   * need this bitstream buffer, but this will be used for 128 bytes alignment */
  std::vector<guint8> bitstream_buffer;

  guint width = 0;
  guint height = 0;
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;
} GstD3D11Vp8DecInner;
/* *INDENT-ON* */

typedef struct _GstD3D11Vp8Dec
{
  GstVp8Decoder parent;
  GstD3D11Vp8DecInner *inner;
} GstD3D11Vp8Dec;

typedef struct _GstD3D11Vp8DecClass
{
  GstVp8DecoderClass parent_class;
  GstD3D11DecoderSubClassData class_data;
} GstD3D11Vp8DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_VP8_DEC(object) ((GstD3D11Vp8Dec *) (object))
#define GST_D3D11_VP8_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Vp8DecClass))

static void gst_d3d11_vp8_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp8_dec_finalize (GObject * object);
static void gst_d3d11_vp8_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_vp8_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_vp8_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_vp8_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstVp8Decoder */
static GstFlowReturn gst_d3d11_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr, gint max_dpb_size);
static GstFlowReturn gst_d3d11_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture);
static GstFlowReturn gst_d3d11_vp8_dec_start_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture);
static GstFlowReturn gst_d3d11_vp8_dec_decode_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture, GstVp8Parser * parser);
static GstFlowReturn gst_d3d11_vp8_dec_end_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture);
static GstFlowReturn gst_d3d11_vp8_dec_output_picture (GstVp8Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp8Picture * picture);

static void
gst_d3d11_vp8_dec_class_init (GstD3D11Vp8DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp8DecoderClass *vp8decoder_class = GST_VP8_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;

  gobject_class->get_property = gst_d3d11_vp8_dec_get_property;
  gobject_class->finalize = gst_d3d11_vp8_dec_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gst_d3d11_decoder_class_data_fill_subclass_data (cdata, &klass->class_data);

  /**
   * GstD3D11Vp8Dec:adapter-luid:
   *
   * DXGI Adapter LUID for this element
   *
   * Since: 1.20
   */
  gst_d3d11_decoder_proxy_class_init (element_class, cdata,
      "Seungha Yang <seungha.yang@navercorp.com>");

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_src_query);
  decoder_class->sink_event = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_sink_event);

  vp8decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_new_sequence);
  vp8decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_new_picture);
  vp8decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_start_picture);
  vp8decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_decode_picture);
  vp8decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_end_picture);
  vp8decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_output_picture);
}

static void
gst_d3d11_vp8_dec_init (GstD3D11Vp8Dec * self)
{
  self->inner = new GstD3D11Vp8DecInner ();
}

static void
gst_d3d11_vp8_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (object);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_decoder_proxy_get_property (object, prop_id, value, pspec, cdata);
}

static void
gst_d3d11_vp8_dec_finalize (GObject * object)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (object);

  delete self->inner;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_vp8_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (element);
  GstD3D11Vp8DecInner *inner = self->inner;
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, cdata->adapter_luid, &inner->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_vp8_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  if (!gst_d3d11_decoder_proxy_open (decoder,
          cdata, &inner->device, &inner->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_vp8_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  gst_clear_object (&inner->d3d11_decoder);
  gst_clear_object (&inner->device);

  return TRUE;
}

static gboolean
gst_d3d11_vp8_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_negotiate (inner->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_vp8_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_decide_allocation (inner->d3d11_decoder, decoder,
          query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_vp8_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, inner->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_vp8_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      if (inner->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (inner->d3d11_decoder, decoder, TRUE);
      break;
    case GST_EVENT_FLUSH_STOP:
      if (inner->d3d11_decoder)
        gst_d3d11_decoder_set_flushing (inner->d3d11_decoder, decoder, FALSE);
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->sink_event (decoder, event);
}

static GstFlowReturn
gst_d3d11_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr, gint max_dpb_size)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  GstVideoInfo info;

  GST_LOG_OBJECT (self, "new sequence");

  /* FIXME: support I420 */
  inner->out_format = GST_VIDEO_FORMAT_NV12;
  inner->width = frame_hdr->width;
  inner->height = frame_hdr->height;

  gst_video_info_set_format (&info,
      inner->out_format, inner->width, inner->height);

  if (!gst_d3d11_decoder_configure (inner->d3d11_decoder,
          decoder->input_state, &info, 0, 0, inner->width, inner->height,
          max_dpb_size)) {
    GST_ERROR_OBJECT (self, "Failed to create decoder");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
    GST_WARNING_OBJECT (self, "Failed to negotiate with downstream");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (inner->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return GST_FLOW_FLUSHING;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_vp8_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New VP8 picture %p", picture);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_vp8_dec_start_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;

  inner->bitstream_buffer.resize (0);

  return GST_FLOW_OK;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_vp8_dec_get_output_view_from_picture (GstD3D11Vp8Dec * self,
    GstVp8Picture * picture, guint8 * view_id)
{
  GstD3D11Vp8DecInner *inner = self->inner;
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_vp8_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (inner->d3d11_decoder,
      view_buffer, view_id);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static void
gst_d3d11_vp8_dec_copy_frame_params (GstD3D11Vp8Dec * self,
    GstVp8Picture * picture, GstVp8Parser * parser, DXVA_PicParams_VP8 * params)
{
  const GstVp8FrameHdr *frame_hdr = &picture->frame_hdr;
  gint i;

  /* 0: keyframe, 1: inter */
  params->frame_type = !frame_hdr->key_frame;
  params->version = frame_hdr->version;
  params->show_frame = frame_hdr->show_frame;
  params->clamp_type = frame_hdr->clamping_type;

  params->filter_type = frame_hdr->filter_type;
  params->filter_level = frame_hdr->loop_filter_level;
  params->sharpness_level = frame_hdr->sharpness_level;
  params->mode_ref_lf_delta_enabled =
      parser->mb_lf_adjust.loop_filter_adj_enable;
  params->mode_ref_lf_delta_update =
      parser->mb_lf_adjust.mode_ref_lf_delta_update;
  for (i = 0; i < 4; i++) {
    params->ref_lf_deltas[i] = parser->mb_lf_adjust.ref_frame_delta[i];
    params->mode_lf_deltas[i] = parser->mb_lf_adjust.mb_mode_delta[i];
  }
  params->log2_nbr_of_dct_partitions = frame_hdr->log2_nbr_of_dct_partitions;
  params->base_qindex = frame_hdr->quant_indices.y_ac_qi;
  params->y1dc_delta_q = frame_hdr->quant_indices.y_dc_delta;
  params->y2dc_delta_q = frame_hdr->quant_indices.y2_dc_delta;
  params->y2ac_delta_q = frame_hdr->quant_indices.y2_ac_delta;
  params->uvdc_delta_q = frame_hdr->quant_indices.uv_dc_delta;
  params->uvac_delta_q = frame_hdr->quant_indices.uv_ac_delta;

  params->ref_frame_sign_bias_golden = frame_hdr->sign_bias_golden;
  params->ref_frame_sign_bias_altref = frame_hdr->sign_bias_alternate;

  params->refresh_entropy_probs = frame_hdr->refresh_entropy_probs;

  memcpy (params->vp8_coef_update_probs, frame_hdr->token_probs.prob,
      sizeof (frame_hdr->token_probs.prob));

  params->mb_no_coeff_skip = frame_hdr->mb_no_skip_coeff;
  params->prob_skip_false = frame_hdr->prob_skip_false;
  params->prob_intra = frame_hdr->prob_intra;
  params->prob_last = frame_hdr->prob_last;
  params->prob_golden = frame_hdr->prob_gf;

  memcpy (params->intra_16x16_prob, frame_hdr->mode_probs.y_prob,
      sizeof (frame_hdr->mode_probs.y_prob));
  memcpy (params->intra_chroma_prob, frame_hdr->mode_probs.uv_prob,
      sizeof (frame_hdr->mode_probs.uv_prob));
  memcpy (params->vp8_mv_update_probs, frame_hdr->mv_probs.prob,
      sizeof (frame_hdr->mv_probs.prob));
}

static void
gst_d3d11_vp8_dec_copy_reference_frames (GstD3D11Vp8Dec * self,
    DXVA_PicParams_VP8 * params)
{
  GstVp8Decoder *decoder = GST_VP8_DECODER (self);
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;

  if (decoder->alt_ref_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->alt_ref_picture, &view_id);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->alt_fb_idx.Index7Bits = view_id;
  } else {
    params->alt_fb_idx.bPicEntry = 0xff;
  }

  if (decoder->golden_ref_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->golden_ref_picture, &view_id);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->gld_fb_idx.Index7Bits = view_id;
  } else {
    params->gld_fb_idx.bPicEntry = 0xff;
  }

  if (decoder->last_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->last_picture, &view_id);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->lst_fb_idx.Index7Bits = view_id;
  } else {
    params->lst_fb_idx.bPicEntry = 0xff;
  }
}

static void
gst_d3d11_vp8_dec_copy_segmentation_params (GstD3D11Vp8Dec * self,
    GstVp8Parser * parser, DXVA_PicParams_VP8 * params)
{
  const GstVp8Segmentation *seg = &parser->segmentation;
  gint i;

  params->stVP8Segments.segmentation_enabled = seg->segmentation_enabled;
  params->stVP8Segments.update_mb_segmentation_map =
      seg->update_mb_segmentation_map;
  params->stVP8Segments.update_mb_segmentation_data =
      seg->update_segment_feature_data;
  params->stVP8Segments.mb_segement_abs_delta = seg->segment_feature_mode;

  for (i = 0; i < 4; i++) {
    params->stVP8Segments.segment_feature_data[0][i] =
        seg->quantizer_update_value[i];
  }

  for (i = 0; i < 4; i++) {
    params->stVP8Segments.segment_feature_data[1][i] = seg->lf_update_value[i];
  }

  for (i = 0; i < 3; i++) {
    params->stVP8Segments.mb_segment_tree_probs[i] = seg->segment_prob[i];
  }
}

static GstFlowReturn
gst_d3d11_vp8_dec_decode_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture, GstVp8Parser * parser)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  DXVA_PicParams_VP8 *pic_params = &inner->pic_params;
  DXVA_Slice_VPx_Short *slice = &inner->slice;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  const GstVp8FrameHdr *frame_hdr = &picture->frame_hdr;

  view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
      picture, &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (pic_params, 0, sizeof (DXVA_PicParams_VP8));

  pic_params->first_part_size = frame_hdr->first_part_size;
  pic_params->width = inner->width;
  pic_params->height = inner->height;
  pic_params->CurrPic.Index7Bits = view_id;
  pic_params->StatusReportFeedbackNumber = 1;

  gst_d3d11_vp8_dec_copy_frame_params (self, picture, parser, pic_params);
  gst_d3d11_vp8_dec_copy_reference_frames (self, pic_params);
  gst_d3d11_vp8_dec_copy_segmentation_params (self, parser, pic_params);

  inner->bitstream_buffer.resize (picture->size);
  memcpy (&inner->bitstream_buffer[0], picture->data, picture->size);

  slice->BSNALunitDataLocation = 0;
  slice->SliceBytesInBuffer = inner->bitstream_buffer.size ();
  slice->wBadSliceChopping = 0;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_vp8_dec_end_picture (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  size_t bitstream_buffer_size;
  size_t bitstream_pos;
  GstD3D11DecodeInputStreamArgs input_args;

  if (inner->bitstream_buffer.empty ()) {
    GST_ERROR_OBJECT (self, "No bitstream buffer to submit");
    return GST_FLOW_ERROR;
  }

  view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
      picture, &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (&input_args, 0, sizeof (GstD3D11DecodeInputStreamArgs));

  bitstream_pos = inner->bitstream_buffer.size ();
  bitstream_buffer_size = GST_ROUND_UP_128 (bitstream_pos);

  if (bitstream_buffer_size > bitstream_pos) {
    size_t padding = bitstream_buffer_size - bitstream_pos;

    /* As per DXVA spec, total amount of bitstream buffer size should be
     * 128 bytes aligned. If actual data is not multiple of 128 bytes,
     * the last slice data needs to be zero-padded */
    inner->bitstream_buffer.resize (bitstream_buffer_size, 0);

    inner->slice.SliceBytesInBuffer += padding;
  }

  input_args.picture_params = &inner->pic_params;
  input_args.picture_params_size = sizeof (DXVA_PicParams_VP8);
  input_args.slice_control = &inner->slice;
  input_args.slice_control_size = sizeof (DXVA_Slice_VPx_Short);
  input_args.bitstream = &inner->bitstream_buffer[0];
  input_args.bitstream_size = inner->bitstream_buffer.size ();

  return gst_d3d11_decoder_decode_frame (inner->d3d11_decoder,
      view, &input_args);
}

static GstFlowReturn
gst_d3d11_vp8_dec_output_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecInner *inner = self->inner;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  g_assert (picture->frame_hdr.show_frame);

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_vp8_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!gst_d3d11_decoder_process_output (inner->d3d11_decoder, vdec,
          picture->discont_state, inner->width, inner->height, view_buffer,
          &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  gst_vp8_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_vp8_picture_unref (picture);
  gst_video_decoder_release_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

void
gst_d3d11_vp8_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  GTypeInfo type_info = {
    sizeof (GstD3D11Vp8DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_vp8_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11Vp8Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_vp8_dec_init,
  };
  const GUID *profile_guid = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  DXGI_FORMAT format = DXGI_FORMAT_NV12;

  if (!gst_d3d11_decoder_get_supported_decoder_profile (device,
          GST_DXVA_CODEC_VP8, GST_VIDEO_FORMAT_NV12, &profile_guid)) {
    GST_INFO_OBJECT (device, "device does not support VP8 decoding");
    return;
  }

  for (i = 0; i < G_N_ELEMENTS (gst_dxva_resolutions); i++) {
    if (gst_d3d11_decoder_supports_resolution (device, profile_guid,
            format, gst_dxva_resolutions[i].width,
            gst_dxva_resolutions[i].height)) {
      max_width = gst_dxva_resolutions[i].width;
      max_height = gst_dxva_resolutions[i].height;

      GST_DEBUG_OBJECT (device,
          "device support resolution %dx%d", max_width, max_height);
    } else {
      break;
    }
  }

  if (max_width == 0 || max_height == 0) {
    GST_WARNING_OBJECT (device, "Couldn't query supported resolution");
    return;
  }

  sink_caps = gst_caps_from_string ("video/x-vp8");
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "); video/x-raw");

  gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, GST_DXVA_CODEC_VP8,
      sink_caps, src_caps, resolution);

  type_name = g_strdup ("GstD3D11Vp8Dec");
  feature_name = g_strdup ("d3d11vp8dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11Vp8Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11vp8device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_VP8_DECODER,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
