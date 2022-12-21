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
 * SECTION:element-nvvp9sldec
 * @title: nvvp9sldec
 *
 * GstCodecs based NVIDIA VP9 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/vp9/file ! parsebin ! nvvp9sldec ! videoconvert ! autovideosink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvvp9dec.h"
#include "gstnvdecoder.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_nv_vp9_dec_debug);
#define GST_CAT_DEFAULT gst_nv_vp9_dec_debug

typedef struct _GstNvVp9Dec
{
  GstVp9Decoder parent;

  GstCudaContext *context;
  GstNvDecoder *decoder;
  CUVIDPICPARAMS params;

  guint width, height;
  GstVP9Profile profile;
} GstNvVp9Dec;

typedef struct _GstNvVp9DecClass
{
  GstVp9DecoderClass parent_class;
  guint cuda_device_id;
} GstNvVp9DecClass;

enum
{
  PROP_0,
  PROP_CUDA_DEVICE_ID,
};

static GTypeClass *parent_class = NULL;

#define GST_NV_VP9_DEC(object) ((GstNvVp9Dec *) (object))
#define GST_NV_VP9_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstNvVp9DecClass))

static void gst_nv_vp9_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_nv_vp9_dec_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_nv_vp9_dec_open (GstVideoDecoder * decoder);
static gboolean gst_nv_vp9_dec_close (GstVideoDecoder * decoder);
static gboolean gst_nv_vp9_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_nv_vp9_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_nv_vp9_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstVp9Decoder */
static GstFlowReturn gst_nv_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHeader * frame_hdr, gint max_dpb_size);
static GstFlowReturn gst_nv_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture);
static GstVp9Picture *gst_nv_vp9_dec_duplicate_picture (GstVp9Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp9Picture * picture);
static GstFlowReturn gst_nv_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb);
static GstFlowReturn gst_nv_vp9_dec_output_picture (GstVp9Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp9Picture * picture);
static guint gst_nv_vp9_dec_get_preferred_output_delay (GstVp9Decoder * decoder,
    gboolean is_live);

static void
gst_nv_vp9_dec_class_init (GstNvVp9DecClass * klass,
    GstNvDecoderClassData * cdata)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp9DecoderClass *vp9decoder_class = GST_VP9_DECODER_CLASS (klass);

  object_class->get_property = gst_nv_vp9_dec_get_property;

  /**
   * GstNvVp9SLDec:cuda-device-id:
   *
   * Assigned CUDA device id
   *
   * Since: 1.22
   */
  g_object_class_install_property (object_class, PROP_CUDA_DEVICE_ID,
      g_param_spec_uint ("cuda-device-id", "CUDA device id",
          "Assigned CUDA device id", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_set_context);

  parent_class = (GTypeClass *) g_type_class_peek_parent (klass);
  gst_element_class_set_metadata (element_class,
      "NVDEC VP9 Stateless Decoder",
      "Codec/Decoder/Video/Hardware",
      "NVIDIA VP9 video decoder", "Seungha Yang <seungha@centricular.com>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_src_query);

  vp9decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_new_sequence);
  vp9decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_new_picture);
  vp9decoder_class->duplicate_picture =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_duplicate_picture);
  vp9decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_decode_picture);
  vp9decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_output_picture);
  vp9decoder_class->get_preferred_output_delay =
      GST_DEBUG_FUNCPTR (gst_nv_vp9_dec_get_preferred_output_delay);

  klass->cuda_device_id = cdata->cuda_device_id;

  gst_caps_unref (cdata->sink_caps);
  gst_caps_unref (cdata->src_caps);
  g_free (cdata);
}

static void
gst_nv_vp9_dec_init (GstNvVp9Dec * self)
{
}

static void
gst_nv_vp9_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNvVp9DecClass *klass = GST_NV_VP9_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_CUDA_DEVICE_ID:
      g_value_set_uint (value, klass->cuda_device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_vp9_dec_set_context (GstElement * element, GstContext * context)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (element);
  GstNvVp9DecClass *klass = GST_NV_VP9_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "set context %s",
      gst_context_get_context_type (context));

  if (gst_cuda_handle_set_context (element, context, klass->cuda_device_id,
          &self->context)) {
    goto done;
  }

  if (self->decoder)
    gst_nv_decoder_handle_set_context (self->decoder, element, context);

done:
  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_vp9_dec_open (GstVideoDecoder * decoder)
{
  GstVp9Decoder *vp9dec = GST_VP9_DECODER (decoder);
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstNvVp9DecClass *klass = GST_NV_VP9_DEC_GET_CLASS (self);

  if (!gst_cuda_ensure_element_context (GST_ELEMENT (self),
          klass->cuda_device_id, &self->context)) {
    GST_ERROR_OBJECT (self, "Required element data is unavailable");
    return FALSE;
  }

  self->decoder = gst_nv_decoder_new (self->context);
  if (!self->decoder) {
    GST_ERROR_OBJECT (self, "Failed to create decoder object");
    gst_clear_object (&self->context);

    return FALSE;
  }

  /* NVDEC doesn't support non-keyframe resolution change and it will result
   * in outputting broken frames */
  gst_vp9_decoder_set_non_keyframe_format_change_support (vp9dec, FALSE);

  return TRUE;
}

static gboolean
gst_nv_vp9_dec_close (GstVideoDecoder * decoder)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);

  gst_clear_object (&self->decoder);
  gst_clear_object (&self->context);

  return TRUE;
}

static gboolean
gst_nv_vp9_dec_negotiate (GstVideoDecoder * decoder)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstVp9Decoder *vp9dec = GST_VP9_DECODER (decoder);

  GST_DEBUG_OBJECT (self, "negotiate");

  gst_nv_decoder_negotiate (self->decoder, decoder, vp9dec->input_state);

  /* TODO: add support D3D11 memory */

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_nv_vp9_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);

  if (!gst_nv_decoder_decide_allocation (self->decoder, decoder, query)) {
    GST_WARNING_OBJECT (self, "Failed to handle decide allocation");
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_nv_vp9_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_cuda_handle_context_query (GST_ELEMENT (decoder), query,
              self->context)) {
        return TRUE;
      } else if (self->decoder &&
          gst_nv_decoder_handle_context_query (self->decoder, decoder, query)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static GstFlowReturn
gst_nv_vp9_dec_new_sequence (GstVp9Decoder * decoder,
    const GstVp9FrameHeader * frame_hdr, gint max_dpb_size)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstVideoInfo info;

  GST_LOG_OBJECT (self, "new sequence");

  self->width = frame_hdr->width;
  self->height = frame_hdr->height;
  self->profile = frame_hdr->profile;

  if (self->profile == GST_VP9_PROFILE_0) {
    out_format = GST_VIDEO_FORMAT_NV12;
  } else if (self->profile == GST_VP9_PROFILE_2) {
    if (frame_hdr->bit_depth == 10) {
      out_format = GST_VIDEO_FORMAT_P010_10LE;
    } else {
      out_format = GST_VIDEO_FORMAT_P016_LE;
    }
  }

  if (out_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Could not support profile %d", self->profile);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  gst_video_info_set_format (&info, out_format, self->width, self->height);
  if (!gst_nv_decoder_configure (self->decoder,
          cudaVideoCodec_VP9, &info, self->width, self->height,
          frame_hdr->bit_depth, max_dpb_size, FALSE)) {
    GST_ERROR_OBJECT (self, "Failed to configure decoder");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
    return GST_FLOW_NOT_NEGOTIATED;
  }

  memset (&self->params, 0, sizeof (CUVIDPICPARAMS));

  self->params.CodecSpecific.vp9.colorSpace = frame_hdr->color_space;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_vp9_dec_new_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstNvDecoderFrame *nv_frame;

  nv_frame = gst_nv_decoder_new_frame (self->decoder);
  if (!nv_frame) {
    GST_ERROR_OBJECT (self, "No available decoder frame");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self,
      "New decoder frame %p (index %d)", nv_frame, nv_frame->index);

  gst_vp9_picture_set_user_data (picture,
      nv_frame, (GDestroyNotify) gst_nv_decoder_frame_unref);

  return GST_FLOW_OK;
}

static GstNvDecoderFrame *
gst_nv_vp9_dec_get_decoder_frame_from_picture (GstNvVp9Dec * self,
    GstVp9Picture * picture)
{
  GstNvDecoderFrame *frame;

  frame = (GstNvDecoderFrame *) gst_vp9_picture_get_user_data (picture);

  if (!frame)
    GST_DEBUG_OBJECT (self, "current picture does not have decoder frame");

  return frame;
}

static GstVp9Picture *
gst_nv_vp9_dec_duplicate_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstNvDecoderFrame *nv_frame;
  GstVp9Picture *new_picture;

  nv_frame = gst_nv_vp9_dec_get_decoder_frame_from_picture (self, picture);

  if (!nv_frame) {
    GST_ERROR_OBJECT (self, "Parent picture does not have decoder frame");
    return NULL;
  }

  new_picture = gst_vp9_picture_new ();
  new_picture->frame_hdr = picture->frame_hdr;

  gst_vp9_picture_set_user_data (new_picture,
      gst_nv_decoder_frame_ref (nv_frame),
      (GDestroyNotify) gst_nv_decoder_frame_unref);

  return new_picture;
}

static GstFlowReturn
gst_nv_vp9_dec_decode_picture (GstVp9Decoder * decoder,
    GstVp9Picture * picture, GstVp9Dpb * dpb)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  const GstVp9FrameHeader *frame_hdr = &picture->frame_hdr;
  const GstVp9LoopFilterParams *lfp = &frame_hdr->loop_filter_params;
  const GstVp9SegmentationParams *sp = &frame_hdr->segmentation_params;
  const GstVp9QuantizationParams *qp = &frame_hdr->quantization_params;
  CUVIDPICPARAMS *params = &self->params;
  CUVIDVP9PICPARAMS *vp9_params = &params->CodecSpecific.vp9;
  GstNvDecoderFrame *frame;
  GstNvDecoderFrame *other_frame;
  guint offset = 0;
  guint8 ref_frame_map[GST_VP9_REF_FRAMES];
  gint i;

  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->mbRefLfDelta) ==
      GST_VP9_MAX_REF_LF_DELTAS);
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->mbModeLfDelta) ==
      GST_VP9_MAX_MODE_LF_DELTAS);
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->mb_segment_tree_probs) ==
      GST_VP9_SEG_TREE_PROBS);
  G_STATIC_ASSERT (sizeof (vp9_params->mb_segment_tree_probs) ==
      sizeof (sp->segmentation_tree_probs));
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->segment_pred_probs) ==
      GST_VP9_PREDICTION_PROBS);
  G_STATIC_ASSERT (sizeof (vp9_params->segment_pred_probs) ==
      sizeof (sp->segmentation_pred_prob));
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->refFrameSignBias) ==
      GST_VP9_REFS_PER_FRAME + 1);
  G_STATIC_ASSERT (sizeof (vp9_params->refFrameSignBias) ==
      sizeof (frame_hdr->ref_frame_sign_bias));
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->activeRefIdx) ==
      GST_VP9_REFS_PER_FRAME);
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->segmentFeatureEnable) ==
      GST_VP9_MAX_SEGMENTS);
  G_STATIC_ASSERT (sizeof (vp9_params->segmentFeatureEnable) ==
      sizeof (sp->feature_enabled));
  G_STATIC_ASSERT (G_N_ELEMENTS (vp9_params->segmentFeatureData) ==
      GST_VP9_MAX_SEGMENTS);
  G_STATIC_ASSERT (sizeof (vp9_params->segmentFeatureData) ==
      sizeof (sp->feature_data));

  GST_LOG_OBJECT (self, "Decode picture, size %" G_GSIZE_FORMAT, picture->size);

  frame = gst_nv_vp9_dec_get_decoder_frame_from_picture (self, picture);
  if (!frame) {
    GST_ERROR_OBJECT (self, "Decoder frame is unavailable");
    return GST_FLOW_ERROR;
  }

  params->nBitstreamDataLen = picture->size;
  params->pBitstreamData = picture->data;
  params->nNumSlices = 1;
  params->pSliceDataOffsets = &offset;

  params->PicWidthInMbs = GST_ROUND_UP_16 (frame_hdr->width) >> 4;
  params->FrameHeightInMbs = GST_ROUND_UP_16 (frame_hdr->height) >> 4;
  params->CurrPicIdx = frame->index;

  vp9_params->width = frame_hdr->width;
  vp9_params->height = frame_hdr->height;

  for (i = 0; i < GST_VP9_REF_FRAMES; i++) {
    if (dpb->pic_list[i]) {
      other_frame = gst_nv_vp9_dec_get_decoder_frame_from_picture (self,
          dpb->pic_list[i]);
      if (!other_frame) {
        GST_ERROR_OBJECT (self, "Couldn't get decoder frame from picture");
        return GST_FLOW_ERROR;
      }

      ref_frame_map[i] = other_frame->index;
    } else {
      ref_frame_map[i] = 0xff;
    }
  }

  vp9_params->LastRefIdx = ref_frame_map[frame_hdr->ref_frame_idx[0]];
  vp9_params->GoldenRefIdx = ref_frame_map[frame_hdr->ref_frame_idx[1]];
  vp9_params->AltRefIdx = ref_frame_map[frame_hdr->ref_frame_idx[2]];

  vp9_params->profile = frame_hdr->profile;
  vp9_params->frameContextIdx = frame_hdr->frame_context_idx;
  vp9_params->frameType = frame_hdr->frame_type;
  vp9_params->showFrame = frame_hdr->show_frame;
  vp9_params->errorResilient = frame_hdr->error_resilient_mode;
  vp9_params->frameParallelDecoding = frame_hdr->frame_parallel_decoding_mode;
  vp9_params->subSamplingX = frame_hdr->subsampling_x;
  vp9_params->subSamplingY = frame_hdr->subsampling_y;
  vp9_params->intraOnly = frame_hdr->intra_only;
  vp9_params->allow_high_precision_mv = frame_hdr->allow_high_precision_mv;
  vp9_params->refreshEntropyProbs = frame_hdr->refresh_frame_context;
  vp9_params->bitDepthMinus8Luma = frame_hdr->bit_depth - 8;
  vp9_params->bitDepthMinus8Chroma = frame_hdr->bit_depth - 8;

  vp9_params->loopFilterLevel = lfp->loop_filter_level;
  vp9_params->loopFilterSharpness = lfp->loop_filter_sharpness;
  vp9_params->modeRefLfEnabled = lfp->loop_filter_delta_enabled;

  vp9_params->log2_tile_columns = frame_hdr->tile_cols_log2;
  vp9_params->log2_tile_rows = frame_hdr->tile_rows_log2;

  vp9_params->segmentEnabled = sp->segmentation_enabled;
  vp9_params->segmentMapUpdate = sp->segmentation_update_map;
  vp9_params->segmentMapTemporalUpdate = sp->segmentation_temporal_update;
  vp9_params->segmentFeatureMode = sp->segmentation_abs_or_delta_update;

  vp9_params->qpYAc = qp->base_q_idx;
  vp9_params->qpYDc = qp->delta_q_y_dc;
  vp9_params->qpChDc = qp->delta_q_uv_dc;
  vp9_params->qpChAc = qp->delta_q_uv_ac;

  vp9_params->resetFrameContext = frame_hdr->reset_frame_context;
  vp9_params->mcomp_filter_type = frame_hdr->interpolation_filter;
  vp9_params->frameTagSize = frame_hdr->frame_header_length_in_bytes;
  vp9_params->offsetToDctParts = frame_hdr->header_size_in_bytes;

  for (i = 0; i < GST_VP9_MAX_REF_LF_DELTAS; i++)
    vp9_params->mbRefLfDelta[i] = lfp->loop_filter_ref_deltas[i];

  for (i = 0; i < GST_VP9_MAX_MODE_LF_DELTAS; i++)
    vp9_params->mbModeLfDelta[i] = lfp->loop_filter_mode_deltas[i];

  memcpy (vp9_params->mb_segment_tree_probs, sp->segmentation_tree_probs,
      sizeof (sp->segmentation_tree_probs));
  memcpy (vp9_params->segment_pred_probs, sp->segmentation_pred_prob,
      sizeof (sp->segmentation_pred_prob));
  memcpy (vp9_params->refFrameSignBias, frame_hdr->ref_frame_sign_bias,
      sizeof (frame_hdr->ref_frame_sign_bias));

  for (i = 0; i < GST_VP9_REFS_PER_FRAME; i++) {
    vp9_params->activeRefIdx[i] = frame_hdr->ref_frame_idx[i];
  }

  memcpy (vp9_params->segmentFeatureEnable, sp->feature_enabled,
      sizeof (sp->feature_enabled));
  memcpy (vp9_params->segmentFeatureData, sp->feature_data,
      sizeof (sp->feature_data));

  if (!gst_nv_decoder_decode_picture (self->decoder, &self->params))
    return GST_FLOW_ERROR;

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_nv_vp9_dec_output_picture (GstVp9Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp9Picture * picture)
{
  GstNvVp9Dec *self = GST_NV_VP9_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstNvDecoderFrame *decoder_frame;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  decoder_frame = (GstNvDecoderFrame *) gst_vp9_picture_get_user_data (picture);
  if (!decoder_frame) {
    GST_ERROR_OBJECT (self, "No decoder frame in picture %p", picture);
    goto error;
  }

  if (!gst_nv_decoder_finish_frame (self->decoder, vdec, picture->discont_state,
          decoder_frame, &frame->output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to handle output picture");
    goto error;
  }

  gst_vp9_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_vp9_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static guint
gst_nv_vp9_dec_get_preferred_output_delay (GstVp9Decoder * decoder,
    gboolean is_live)
{
  /* Prefer to zero latency for live pipeline */
  if (is_live)
    return 0;

  /* NVCODEC SDK uses 4 frame delay for better throughput performance */
  return 4;
}

void
gst_nv_vp9_dec_register (GstPlugin * plugin, guint device_id, guint rank,
    GstCaps * sink_caps, GstCaps * src_caps, gboolean is_primary)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  GstNvDecoderClassData *cdata;
  gint index = 0;
  GTypeInfo type_info = {
    sizeof (GstNvVp9DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_nv_vp9_dec_class_init,
    NULL,
    NULL,
    sizeof (GstNvVp9Dec),
    0,
    (GInstanceInitFunc) gst_nv_vp9_dec_init,
  };

  GST_DEBUG_CATEGORY_INIT (gst_nv_vp9_dec_debug, "nvvp9dec", 0, "nvvp9dec");

  cdata = g_new0 (GstNvDecoderClassData, 1);
  cdata->sink_caps = gst_caps_copy (sink_caps);
  gst_caps_set_simple (cdata->sink_caps,
      "alignment", G_TYPE_STRING, "frame", NULL);
  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  cdata->src_caps = gst_caps_ref (src_caps);
  cdata->cuda_device_id = device_id;

  if (is_primary) {
    type_name = g_strdup ("GstNvVp9Dec");
    feature_name = g_strdup ("nvvp9dec");
  } else {
    type_name = g_strdup ("GstNvVp9SLDec");
    feature_name = g_strdup ("nvvp9sldec");
  }

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    if (is_primary) {
      type_name = g_strdup_printf ("GstNvVp9Device%dDec", index);
      feature_name = g_strdup_printf ("nvvp9device%ddec", index);
    } else {
      type_name = g_strdup_printf ("GstNvVp9SLDevice%dDec", index);
      feature_name = g_strdup_printf ("nvvp9sldevice%ddec", index);
    }
  }

  type_info.class_data = cdata;

  type = g_type_register_static (GST_TYPE_VP9_DECODER,
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index > 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
