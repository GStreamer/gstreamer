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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11vp8dec.h"
#include "gstd3d11memory.h"
#include "gstd3d11bufferpool.h"

#include <gst/codecs/gstvp8decoder.h>
#include <string.h>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_vp8_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_vp8_dec_debug

enum
{
  PROP_0,
  PROP_ADAPTER,
  PROP_DEVICE_ID,
  PROP_VENDOR_ID,
};

/* copied from d3d11.h since mingw header doesn't define them */
DEFINE_GUID (GST_GUID_D3D11_DECODER_PROFILE_VP8_VLD,
    0x90b899ea, 0x3a62, 0x4705, 0x88, 0xb3, 0x8d, 0xf0, 0x4b, 0x27, 0x44, 0xe7);

/* reference list 4 + 4 margin */
#define NUM_OUTPUT_VIEW 8

typedef struct _GstD3D11Vp8Dec
{
  GstVp8Decoder parent;

  GstVideoCodecState *output_state;
  GstD3D11Device *device;
  GstD3D11Decoder *d3d11_decoder;

  guint width, height;
  GstVideoFormat out_format;

  gboolean use_d3d11_output;
} GstD3D11Vp8Dec;

typedef struct _GstD3D11Vp8DecClass
{
  GstVp8DecoderClass parent_class;
  guint adapter;
  guint device_id;
  guint vendor_id;
} GstD3D11Vp8DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_VP8_DEC(object) ((GstD3D11Vp8Dec *) (object))
#define GST_D3D11_VP8_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Vp8DecClass))

static void gst_d3d11_vp8_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_vp8_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_vp8_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_vp8_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_vp8_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);

/* GstVp8Decoder */
static gboolean gst_d3d11_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr);
static gboolean gst_d3d11_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture);
static GstFlowReturn gst_d3d11_vp8_dec_output_picture (GstVp8Decoder *
    decoder, GstVideoCodecFrame * frame, GstVp8Picture * picture);
static gboolean gst_d3d11_vp8_dec_start_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture);
static gboolean gst_d3d11_vp8_dec_decode_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture, GstVp8Parser * parser);
static gboolean gst_d3d11_vp8_dec_end_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture);

static void
gst_d3d11_vp8_dec_class_init (GstD3D11Vp8DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstVp8DecoderClass *vp8decoder_class = GST_VP8_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;
  gchar *long_name;

  gobject_class->get_property = gst_d3d11_vp8_dec_get_property;

  g_object_class_install_property (gobject_class, PROP_ADAPTER,
      g_param_spec_uint ("adapter", "Adapter",
          "DXGI Adapter index for creating device",
          0, G_MAXUINT32, cdata->adapter,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("device-id", "Device Id",
          "DXGI Device ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VENDOR_ID,
      g_param_spec_uint ("vendor-id", "Vendor Id",
          "DXGI Vendor ID", 0, G_MAXUINT32, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  parent_class = g_type_class_peek_parent (klass);

  klass->adapter = cdata->adapter;
  klass->device_id = cdata->device_id;
  klass->vendor_id = cdata->vendor_id;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_set_context);

  long_name = g_strdup_printf ("Direct3D11 VP8 %s Decoder", cdata->description);
  gst_element_class_set_metadata (element_class, long_name,
      "Codec/Decoder/Video/Hardware",
      "A Direct3D11 based VP8 video decoder",
      "Seungha Yang <seungha.yang@navercorp.com>");
  g_free (long_name);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));
  gst_d3d11_decoder_class_data_free (cdata);

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_src_query);

  vp8decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_new_sequence);
  vp8decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_new_picture);
  vp8decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_output_picture);
  vp8decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_start_picture);
  vp8decoder_class->decode_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_decode_picture);
  vp8decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_vp8_dec_end_picture);
}

static void
gst_d3d11_vp8_dec_init (GstD3D11Vp8Dec * self)
{
}

static void
gst_d3d11_vp8_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (object);

  switch (prop_id) {
    case PROP_ADAPTER:
      g_value_set_uint (value, klass->adapter);
      break;
    case PROP_DEVICE_ID:
      g_value_set_uint (value, klass->device_id);
      break;
    case PROP_VENDOR_ID:
      g_value_set_uint (value, klass->vendor_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d11_vp8_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (element);
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (self);

  gst_d3d11_handle_set_context (element, context, klass->adapter,
      &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_vp8_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11Vp8DecClass *klass = GST_D3D11_VP8_DEC_GET_CLASS (self);

  if (!gst_d3d11_ensure_element_data (GST_ELEMENT_CAST (self), klass->adapter,
          &self->device)) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11device");
    return FALSE;
  }

  self->d3d11_decoder = gst_d3d11_decoder_new (self->device);

  if (!self->d3d11_decoder) {
    GST_ERROR_OBJECT (self, "Cannot create d3d11 decoder");
    gst_clear_object (&self->device);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_vp8_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);

  gst_clear_object (&self->d3d11_decoder);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d11_vp8_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstVp8Decoder *vp8dec = GST_VP8_DECODER (decoder);

  if (!gst_d3d11_decoder_negotiate (decoder, vp8dec->input_state,
          self->out_format, self->width, self->height, &self->output_state,
          &self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_vp8_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);

  if (!gst_d3d11_decoder_decide_allocation (decoder, query, self->device,
          GST_D3D11_CODEC_VP8, self->use_d3d11_output))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_vp8_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d11_handle_context_query (GST_ELEMENT (decoder),
              query, self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->src_query (decoder, query);
}

static gboolean
gst_d3d11_vp8_dec_new_sequence (GstVp8Decoder * decoder,
    const GstVp8FrameHdr * frame_hdr)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  static const GUID *profile_guid = &GST_GUID_D3D11_DECODER_PROFILE_VP8_VLD;
  GstVideoInfo info;

  GST_LOG_OBJECT (self, "new sequence");

  /* FIXME: support I420 */
  self->out_format = GST_VIDEO_FORMAT_NV12;
  self->width = frame_hdr->width;
  self->height = frame_hdr->height;

  gst_video_info_set_format (&info,
      self->out_format, self->width, self->height);

  gst_d3d11_decoder_reset (self->d3d11_decoder);
  if (!gst_d3d11_decoder_open (self->d3d11_decoder, GST_D3D11_CODEC_VP8,
          &info, self->width, self->height,
          NUM_OUTPUT_VIEW, &profile_guid, 1)) {
    GST_ERROR_OBJECT (self, "Failed to create decoder");
    return FALSE;
  }

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
    GST_ERROR_OBJECT (self, "Failed to negotiate with downstream");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_vp8_dec_new_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstBuffer *view_buffer;
  GstD3D11Memory *mem;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (self->d3d11_decoder);
  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "No available output view buffer");
    return FALSE;
  }

  mem = (GstD3D11Memory *) gst_buffer_peek_memory (view_buffer, 0);

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT " (index %d)",
      view_buffer, mem->subresource_index);

  gst_vp8_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New VP8 picture %p", picture);

  return TRUE;
}

static GstFlowReturn
gst_d3d11_vp8_dec_output_picture (GstVp8Decoder * decoder,
    GstVideoCodecFrame * frame, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *output_buffer = NULL;
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_vp8_picture_get_user_data (picture);

  if (!view_buffer) {
    GST_ERROR_OBJECT (self, "Could not get output view");
    goto error;
  }

  if (!picture->frame_hdr.show_frame) {
    GST_LOG_OBJECT (self, "Decode only picture %p", picture);
    GST_VIDEO_CODEC_FRAME_SET_DECODE_ONLY (frame);

    gst_vp8_picture_unref (picture);

    return gst_video_decoder_finish_frame (vdec, frame);
  }

  /* if downstream is d3d11 element and forward playback case,
   * expose our decoder view without copy. In case of reverse playback, however,
   * we cannot do that since baseclass will store the decoded buffer
   * up to gop size but our dpb pool cannot be increased */
  if (self->use_d3d11_output &&
      gst_d3d11_decoder_supports_direct_rendering (self->d3d11_decoder) &&
      vdec->input_segment.rate > 0) {
    GstMemory *mem;

    output_buffer = gst_buffer_ref (view_buffer);
    mem = gst_buffer_peek_memory (output_buffer, 0);
    GST_MINI_OBJECT_FLAG_SET (mem, GST_D3D11_MEMORY_TRANSFER_NEED_DOWNLOAD);
  } else {
    output_buffer = gst_video_decoder_allocate_output_buffer (vdec);
  }

  if (!output_buffer) {
    GST_ERROR_OBJECT (self, "Couldn't allocate output buffer");
    goto error;
  }

  frame->output_buffer = output_buffer;

  if (!gst_d3d11_decoder_process_output (self->d3d11_decoder,
          &self->output_state->info,
          picture->frame_hdr.width, picture->frame_hdr.height,
          view_buffer, output_buffer)) {
    GST_ERROR_OBJECT (self, "Failed to copy buffer");
    goto error;
  }

  GST_LOG_OBJECT (self, "Finish frame %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (output_buffer)));

  gst_vp8_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_video_decoder_drop_frame (vdec, frame);
  gst_vp8_picture_unref (picture);

  return GST_FLOW_ERROR;
}

static GstD3D11DecoderOutputView *
gst_d3d11_vp8_dec_get_output_view_from_picture (GstD3D11Vp8Dec * self,
    GstVp8Picture * picture)
{
  GstBuffer *view_buffer;
  GstD3D11DecoderOutputView *view;

  view_buffer = (GstBuffer *) gst_vp8_picture_get_user_data (picture);
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view buffer");
    return NULL;
  }

  view =
      gst_d3d11_decoder_get_output_view_from_buffer (self->d3d11_decoder,
      view_buffer);
  if (!view) {
    GST_DEBUG_OBJECT (self, "current picture does not have output view handle");
    return NULL;
  }

  return view;
}

static gboolean
gst_d3d11_vp8_dec_start_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  GstD3D11DecoderOutputView *view;

  view = gst_d3d11_vp8_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Begin frame");

  if (!gst_d3d11_decoder_begin_frame (self->d3d11_decoder, view, 0, NULL)) {
    GST_ERROR_OBJECT (self, "Failed to begin frame");
    return FALSE;
  }

  return TRUE;
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
  GstD3D11DecoderOutputView *view;

  if (decoder->alt_ref_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->alt_ref_picture);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->alt_fb_idx.Index7Bits = view->view_id;
  } else {
    params->alt_fb_idx.bPicEntry = 0xff;
  }

  if (decoder->golden_ref_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->golden_ref_picture);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->gld_fb_idx.Index7Bits = view->view_id;
  } else {
    params->gld_fb_idx.bPicEntry = 0xff;
  }

  if (decoder->last_picture) {
    view = gst_d3d11_vp8_dec_get_output_view_from_picture (self,
        decoder->last_picture);
    if (!view) {
      GST_ERROR_OBJECT (self, "picture does not have output view handle");
      return;
    }

    params->lst_fb_idx.Index7Bits = view->view_id;
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

static gboolean
gst_d3d11_vp8_dec_submit_picture_data (GstD3D11Vp8Dec * self,
    GstVp8Picture * picture, DXVA_PicParams_VP8 * params)
{
  guint d3d11_buffer_size;
  gpointer d3d11_buffer;
  gsize buffer_offset = 0;
  gboolean is_first = TRUE;

  GST_TRACE_OBJECT (self, "Getting picture params buffer");
  if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &d3d11_buffer_size,
          &d3d11_buffer)) {
    GST_ERROR_OBJECT (self,
        "Failed to get decoder buffer for picture parameters");
    return FALSE;
  }

  memcpy (d3d11_buffer, params, sizeof (DXVA_PicParams_VP8));

  GST_TRACE_OBJECT (self, "Release picture param decoder buffer");

  if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
          D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS)) {
    GST_ERROR_OBJECT (self, "Failed to release decoder buffer");
    return FALSE;
  }

  if (!picture->data || !picture->size) {
    GST_ERROR_OBJECT (self, "No data to submit");
    return FALSE;
  }

  GST_TRACE_OBJECT (self, "Submit total %" G_GSIZE_FORMAT " bytes",
      picture->size);

  while (buffer_offset < picture->size) {
    gsize bytes_to_copy = picture->size - buffer_offset;
    gsize written_buffer_size;
    gboolean is_last = TRUE;
    DXVA_Slice_VPx_Short slice_short = { 0, };
    D3D11_VIDEO_DECODER_BUFFER_DESC buffer_desc[3] = { 0, };
    gboolean bad_aligned_bitstream_buffer = FALSE;

    GST_TRACE_OBJECT (self, "Getting bitstream buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &d3d11_buffer_size,
            &d3d11_buffer)) {
      GST_ERROR_OBJECT (self, "Couldn't get bitstream buffer");
      goto error;
    }

    if ((d3d11_buffer_size & 127) != 0) {
      GST_WARNING_OBJECT (self,
          "The size of bitstream buffer is not 128 bytes aligned");
      bad_aligned_bitstream_buffer = TRUE;
    }

    if (bytes_to_copy > d3d11_buffer_size) {
      /* if the size of this slice is larger than the size of remaining d3d11
       * decoder bitstream memory, write the data up to the remaining d3d11
       * decoder bitstream memory size and the rest would be written to the
       * next d3d11 bitstream memory */
      bytes_to_copy = d3d11_buffer_size;
      is_last = FALSE;
    }

    memcpy (d3d11_buffer, picture->data + buffer_offset, bytes_to_copy);
    written_buffer_size = bytes_to_copy;

    /* DXVA2 spec is saying that written bitstream data must be 128 bytes
     * aligned if the bitstream buffer contains end of frame
     * (i.e., wBadSliceChopping == 0 or 2) */
    if (is_last) {
      guint padding = MIN (GST_ROUND_UP_128 (bytes_to_copy) - bytes_to_copy,
          d3d11_buffer_size - bytes_to_copy);

      if (padding) {
        GST_TRACE_OBJECT (self,
            "Written bitstream buffer size %" G_GSIZE_FORMAT
            " is not 128 bytes aligned, add padding %d bytes",
            bytes_to_copy, padding);
        memset ((guint8 *) d3d11_buffer + bytes_to_copy, 0, padding);
        written_buffer_size += padding;
      }
    }

    GST_TRACE_OBJECT (self, "Release bitstream buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM)) {
      GST_ERROR_OBJECT (self, "Failed to release bitstream buffer");

      goto error;
    }

    slice_short.BSNALunitDataLocation = 0;
    slice_short.SliceBytesInBuffer = (UINT) written_buffer_size;

    /* wBadSliceChopping: (dxva spec.)
     * 0: All bits for the slice are located within the corresponding
     *    bitstream data buffer
     * 1: The bitstream data buffer contains the start of the slice,
     *    but not the entire slice, because the buffer is full
     * 2: The bitstream data buffer contains the end of the slice.
     *    It does not contain the start of the slice, because the start of
     *    the slice was located in the previous bitstream data buffer.
     * 3: The bitstream data buffer does not contain the start of the slice
     *    (because the start of the slice was located in the previous
     *     bitstream data buffer), and it does not contain the end of the slice
     *    (because the current bitstream data buffer is also full).
     */
    if (is_last && is_first) {
      slice_short.wBadSliceChopping = 0;
    } else if (!is_last && is_first) {
      slice_short.wBadSliceChopping = 1;
    } else if (is_last && !is_first) {
      slice_short.wBadSliceChopping = 2;
    } else {
      slice_short.wBadSliceChopping = 3;
    }

    GST_TRACE_OBJECT (self, "Getting slice control buffer");
    if (!gst_d3d11_decoder_get_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, &d3d11_buffer_size,
            &d3d11_buffer)) {
      GST_ERROR_OBJECT (self, "Couldn't get slice control buffer");

      goto error;
    }

    memcpy (d3d11_buffer, &slice_short, sizeof (DXVA_Slice_VPx_Short));

    GST_TRACE_OBJECT (self, "Release slice control buffer");
    if (!gst_d3d11_decoder_release_decoder_buffer (self->d3d11_decoder,
            D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL)) {
      GST_ERROR_OBJECT (self, "Failed to release slice control buffer");

      goto error;
    }

    buffer_desc[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
    buffer_desc[0].DataOffset = 0;
    buffer_desc[0].DataSize = sizeof (DXVA_PicParams_VP8);

    buffer_desc[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
    buffer_desc[1].DataOffset = 0;
    buffer_desc[1].DataSize = sizeof (DXVA_Slice_VPx_Short);

    if (!bad_aligned_bitstream_buffer && (written_buffer_size & 127) != 0) {
      GST_WARNING_OBJECT (self,
          "Written bitstream buffer size %" G_GSIZE_FORMAT
          " is not 128 bytes aligned", written_buffer_size);
    }

    buffer_desc[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
    buffer_desc[2].DataOffset = 0;
    buffer_desc[2].DataSize = written_buffer_size;

    if (!gst_d3d11_decoder_submit_decoder_buffers (self->d3d11_decoder,
            3, buffer_desc)) {
      GST_ERROR_OBJECT (self, "Couldn't submit decoder buffers");
      goto error;
    }

    buffer_offset += bytes_to_copy;
    is_first = FALSE;
  }

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_d3d11_vp8_dec_decode_picture (GstVp8Decoder * decoder,
    GstVp8Picture * picture, GstVp8Parser * parser)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);
  DXVA_PicParams_VP8 pic_params = { 0, };
  GstD3D11DecoderOutputView *view;
  const GstVp8FrameHdr *frame_hdr = &picture->frame_hdr;

  view = gst_d3d11_vp8_dec_get_output_view_from_picture (self, picture);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return FALSE;
  }

  pic_params.first_part_size = frame_hdr->first_part_size;
  pic_params.width = self->width;
  pic_params.height = self->height;
  pic_params.CurrPic.Index7Bits = view->view_id;
  pic_params.StatusReportFeedbackNumber = 1;

  gst_d3d11_vp8_dec_copy_frame_params (self, picture, parser, &pic_params);
  gst_d3d11_vp8_dec_copy_reference_frames (self, &pic_params);
  gst_d3d11_vp8_dec_copy_segmentation_params (self, parser, &pic_params);

  return gst_d3d11_vp8_dec_submit_picture_data (self, picture, &pic_params);
}

static gboolean
gst_d3d11_vp8_dec_end_picture (GstVp8Decoder * decoder, GstVp8Picture * picture)
{
  GstD3D11Vp8Dec *self = GST_D3D11_VP8_DEC (decoder);

  if (!gst_d3d11_decoder_end_frame (self->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to EndFrame");
    return FALSE;
  }

  return TRUE;
}

typedef struct
{
  guint width;
  guint height;
} GstD3D11Vp8DecResolution;

void
gst_d3d11_vp8_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    GstD3D11Decoder * decoder, guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  guint i;
  GUID profile;
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
  static const GUID *profile_guid = &GST_GUID_D3D11_DECODER_PROFILE_VP8_VLD;
  /* values were taken from chromium. See supported_profile_helper.cc */
  GstD3D11Vp8DecResolution resolutions_to_check[] = {
    {1920, 1088}, {2560, 1440}, {3840, 2160}, {4096, 2160}, {4096, 2304}
  };
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;
  guint max_width = 0;
  guint max_height = 0;
  guint resolution;
  DXGI_FORMAT format = DXGI_FORMAT_NV12;

  if (!gst_d3d11_decoder_get_supported_decoder_profile (decoder,
          &profile_guid, 1, &profile)) {
    GST_INFO_OBJECT (device, "device does not support VP8 decoding");
    return;
  }

  for (i = 0; i < G_N_ELEMENTS (resolutions_to_check); i++) {
    if (gst_d3d11_decoder_supports_resolution (decoder, &profile,
            format, resolutions_to_check[i].width,
            resolutions_to_check[i].height)) {
      max_width = resolutions_to_check[i].width;
      max_height = resolutions_to_check[i].height;

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

  sink_caps = gst_caps_from_string ("video/x-vp8, "
      "framerate = " GST_VIDEO_FPS_RANGE);
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "), "
      "framerate = " GST_VIDEO_FPS_RANGE ";"
      "video/x-raw, " "framerate = " GST_VIDEO_FPS_RANGE);

  gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);

  /* To cover both landscape and portrait, select max value */
  resolution = MAX (max_width, max_height);
  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, 64, resolution,
      "height", GST_TYPE_INT_RANGE, 64, resolution, NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, sink_caps, src_caps);

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
      type_name, &type_info, 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
