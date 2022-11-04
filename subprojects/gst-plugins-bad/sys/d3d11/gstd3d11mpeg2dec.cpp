/* GStreamer
 * Copyright (C) 2021 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-d3d11mpeg2dec
 * @title: d3d11mpeg2dec
 *
 * A Direct3D11/DXVA based MPEG-2 video decoder
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 filesrc location=/path/to/mpeg2/file ! parsebin ! d3d11mpeg2dec ! d3d11videosink
 * ```
 *
 * Since: 1.20
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstd3d11mpeg2dec.h"

#include <gst/codecs/gstmpeg2decoder.h>
#include <string.h>
#include <vector>

/* HACK: to expose dxva data structure on UWP */
#ifdef WINAPI_PARTITION_DESKTOP
#undef WINAPI_PARTITION_DESKTOP
#endif
#define WINAPI_PARTITION_DESKTOP 1
#include <d3d9.h>
#include <dxva.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_mpeg2_dec_debug);
#define GST_CAT_DEFAULT gst_d3d11_mpeg2_dec_debug

/* *INDENT-OFF* */
typedef struct _GstD3D11Mpeg2DecInner
{
  GstD3D11Device *device = nullptr;
  GstD3D11Decoder *d3d11_decoder = nullptr;

  DXVA_PictureParameters pic_params;
  DXVA_QmatrixData iq_matrix;

  std::vector<DXVA_SliceInfo> slice_list;
  std::vector<guint8> bitstream_buffer;

  gboolean submit_iq_data;

  gint width = 0;
  gint height = 0;
  guint width_in_mb = 0;
  guint height_in_mb = 0;
  GstVideoFormat out_format = GST_VIDEO_FORMAT_UNKNOWN;
  GstMpegVideoSequenceHdr seq;
  GstMpegVideoProfile profile = GST_MPEG_VIDEO_PROFILE_MAIN;
  gboolean interlaced = FALSE;
} GstD3D11Mpeg2DecInner;
/* *INDENT-ON* */

typedef struct _GstD3D11Mpeg2Dec
{
  GstMpeg2Decoder parent;
  GstD3D11Mpeg2DecInner *inner;
} GstD3D11Mpeg2Dec;

typedef struct _GstD3D11Mpeg2DecClass
{
  GstMpeg2DecoderClass parent_class;
  GstD3D11DecoderSubClassData class_data;
} GstD3D11Mpeg2DecClass;

static GstElementClass *parent_class = NULL;

#define GST_D3D11_MPEG2_DEC(object) ((GstD3D11Mpeg2Dec *) (object))
#define GST_D3D11_MPEG2_DEC_GET_CLASS(object) \
    (G_TYPE_INSTANCE_GET_CLASS ((object),G_TYPE_FROM_INSTANCE (object),GstD3D11Mpeg2DecClass))

static void gst_d3d11_mpeg2_dec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_d3d11_mpeg2_dec_finalize (GObject * object);
static void gst_d3d11_mpeg2_dec_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_d3d11_mpeg2_dec_open (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_close (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_d3d11_mpeg2_dec_decide_allocation (GstVideoDecoder *
    decoder, GstQuery * query);
static gboolean gst_d3d11_mpeg2_dec_src_query (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_d3d11_mpeg2_dec_sink_event (GstVideoDecoder * decoder,
    GstEvent * event);

/* GstMpeg2Decoder */
static GstFlowReturn gst_d3d11_mpeg2_dec_new_sequence (GstMpeg2Decoder *
    decoder, const GstMpegVideoSequenceHdr * seq,
    const GstMpegVideoSequenceExt * seq_ext,
    const GstMpegVideoSequenceDisplayExt * seq_display_ext,
    const GstMpegVideoSequenceScalableExt * seq_scalable_ext,
    gint max_dpb_size);
static GstFlowReturn gst_d3d11_mpeg2_dec_new_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture);
static GstFlowReturn
gst_d3d11_mpeg2_dec_new_field_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * first_field, GstMpeg2Picture * second_field);
static GstFlowReturn gst_d3d11_mpeg2_dec_start_picture (GstMpeg2Decoder *
    decoder, GstMpeg2Picture * picture, GstMpeg2Slice * slice,
    GstMpeg2Picture * prev_picture, GstMpeg2Picture * next_picture);
static GstFlowReturn gst_d3d11_mpeg2_dec_decode_slice (GstMpeg2Decoder *
    decoder, GstMpeg2Picture * picture, GstMpeg2Slice * slice);
static GstFlowReturn gst_d3d11_mpeg2_dec_end_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture);
static GstFlowReturn gst_d3d11_mpeg2_dec_output_picture (GstMpeg2Decoder *
    decoder, GstVideoCodecFrame * frame, GstMpeg2Picture * picture);

static void
gst_d3d11_mpeg2_dec_class_init (GstD3D11Mpeg2DecClass * klass, gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *decoder_class = GST_VIDEO_DECODER_CLASS (klass);
  GstMpeg2DecoderClass *mpeg2decoder_class = GST_MPEG2_DECODER_CLASS (klass);
  GstD3D11DecoderClassData *cdata = (GstD3D11DecoderClassData *) data;

  gobject_class->get_property = gst_d3d11_mpeg2_dec_get_property;
  gobject_class->finalize = gst_d3d11_mpeg2_dec_finalize;

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gst_d3d11_decoder_class_data_fill_subclass_data (cdata, &klass->class_data);

  /**
   * GstD3D11Mpeg2Dec:adapter-luid:
   *
   * DXGI Adapter LUID for this element
   *
   * Since: 1.20
   */
  gst_d3d11_decoder_proxy_class_init (element_class, cdata,
      "Seungha Yang <seungha@centricular.com>");

  decoder_class->open = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_open);
  decoder_class->close = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_close);
  decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_negotiate);
  decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_decide_allocation);
  decoder_class->src_query = GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_src_query);
  decoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_sink_event);

  mpeg2decoder_class->new_sequence =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_sequence);
  mpeg2decoder_class->new_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_picture);
  mpeg2decoder_class->new_field_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_new_field_picture);
  mpeg2decoder_class->start_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_start_picture);
  mpeg2decoder_class->decode_slice =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_decode_slice);
  mpeg2decoder_class->end_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_end_picture);
  mpeg2decoder_class->output_picture =
      GST_DEBUG_FUNCPTR (gst_d3d11_mpeg2_dec_output_picture);
}

static void
gst_d3d11_mpeg2_dec_init (GstD3D11Mpeg2Dec * self)
{
  self->inner = new GstD3D11Mpeg2DecInner ();
}

static void
gst_d3d11_mpeg2_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (object);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_decoder_proxy_get_property (object, prop_id, value, pspec, cdata);
}

static void
gst_d3d11_mpeg2_dec_finalize (GObject * object)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (object);

  delete self->inner;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d11_mpeg2_dec_set_context (GstElement * element, GstContext * context)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (element);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  gst_d3d11_handle_set_context_for_adapter_luid (element,
      context, cdata->adapter_luid, &inner->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d11_mpeg2_dec_open (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstD3D11Mpeg2DecClass *klass = GST_D3D11_MPEG2_DEC_GET_CLASS (self);
  GstD3D11DecoderSubClassData *cdata = &klass->class_data;

  if (!gst_d3d11_decoder_proxy_open (decoder,
          cdata, &inner->device, &inner->d3d11_decoder)) {
    GST_ERROR_OBJECT (self, "Failed to open decoder");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d11_mpeg2_dec_close (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;

  gst_clear_object (&inner->d3d11_decoder);
  gst_clear_object (&inner->device);

  return TRUE;
}

static gboolean
gst_d3d11_mpeg2_dec_negotiate (GstVideoDecoder * decoder)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_negotiate (inner->d3d11_decoder, decoder))
    return FALSE;

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);
}

static gboolean
gst_d3d11_mpeg2_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;

  if (!gst_d3d11_decoder_decide_allocation (inner->d3d11_decoder,
          decoder, query)) {
    return FALSE;
  }

  return GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation
      (decoder, query);
}

static gboolean
gst_d3d11_mpeg2_dec_src_query (GstVideoDecoder * decoder, GstQuery * query)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;

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
gst_d3d11_mpeg2_dec_sink_event (GstVideoDecoder * decoder, GstEvent * event)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;

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
gst_d3d11_mpeg2_dec_new_sequence (GstMpeg2Decoder * decoder,
    const GstMpegVideoSequenceHdr * seq,
    const GstMpegVideoSequenceExt * seq_ext,
    const GstMpegVideoSequenceDisplayExt * seq_display_ext,
    const GstMpegVideoSequenceScalableExt * seq_scalable_ext, gint max_dpb_size)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  gboolean interlaced;
  gboolean modified = FALSE;
  gint width, height;
  GstMpegVideoProfile mpeg_profile;

  GST_LOG_OBJECT (self, "new sequence");

  interlaced = seq_ext ? !seq_ext->progressive : FALSE;
  if (inner->interlaced != interlaced) {
    GST_INFO_OBJECT (self, "interlaced sequence change");
    inner->interlaced = interlaced;
    modified = TRUE;
  }

  width = seq->width;
  height = seq->height;
  if (seq_ext) {
    width = (width & 0x0fff) | ((guint32) seq_ext->horiz_size_ext << 12);
    height = (height & 0x0fff) | ((guint32) seq_ext->vert_size_ext << 12);
  }

  if (inner->width != width || inner->height != height) {
    GST_INFO_OBJECT (self, "resolution change %dx%d -> %dx%d",
        inner->width, inner->height, width, height);
    inner->width = width;
    inner->height = height;
    inner->width_in_mb = GST_ROUND_UP_16 (width) >> 4;
    inner->height_in_mb = GST_ROUND_UP_16 (height) >> 4;
    modified = TRUE;
  }

  mpeg_profile = GST_MPEG_VIDEO_PROFILE_MAIN;
  if (seq_ext)
    mpeg_profile = (GstMpegVideoProfile) seq_ext->profile;

  if (mpeg_profile != GST_MPEG_VIDEO_PROFILE_MAIN &&
      mpeg_profile != GST_MPEG_VIDEO_PROFILE_SIMPLE) {
    GST_ERROR_OBJECT (self, "Cannot support profile %d", mpeg_profile);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (inner->profile != mpeg_profile) {
    GST_INFO_OBJECT (self, "Profile change %d -> %d",
        inner->profile, mpeg_profile);
    inner->profile = mpeg_profile;
    modified = TRUE;
  }

  if (modified || !gst_d3d11_decoder_is_configured (inner->d3d11_decoder)) {
    GstVideoInfo info;

    /* FIXME: support I420 */
    inner->out_format = GST_VIDEO_FORMAT_NV12;

    gst_video_info_set_format (&info,
        inner->out_format, inner->width, inner->height);
    if (inner->interlaced)
      GST_VIDEO_INFO_INTERLACE_MODE (&info) = GST_VIDEO_INTERLACE_MODE_MIXED;

    if (!gst_d3d11_decoder_configure (inner->d3d11_decoder,
            decoder->input_state, &info, 0, 0,
            inner->width, inner->height, max_dpb_size)) {
      GST_ERROR_OBJECT (self, "Failed to create decoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      GST_WARNING_OBJECT (self, "Failed to negotiate with downstream");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_new_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstBuffer *view_buffer;

  view_buffer = gst_d3d11_decoder_get_output_view_buffer (inner->d3d11_decoder,
      GST_VIDEO_DECODER (decoder));
  if (!view_buffer) {
    GST_DEBUG_OBJECT (self, "No available output view buffer");
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "New output view buffer %" GST_PTR_FORMAT, view_buffer);

  gst_mpeg2_picture_set_user_data (picture,
      view_buffer, (GDestroyNotify) gst_buffer_unref);

  GST_LOG_OBJECT (self, "New MPEG2 picture %p", picture);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_new_field_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * first_field, GstMpeg2Picture * second_field)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstBuffer *view_buffer;

  view_buffer = (GstBuffer *)
      gst_mpeg2_picture_get_user_data (first_field);

  if (!view_buffer) {
    GST_WARNING_OBJECT (self, "First picture does not have output view buffer");
    return GST_FLOW_OK;
  }

  GST_LOG_OBJECT (self, "New field picture with buffer %" GST_PTR_FORMAT,
      view_buffer);

  gst_mpeg2_picture_set_user_data (second_field,
      gst_buffer_ref (view_buffer), (GDestroyNotify) gst_buffer_unref);

  return GST_FLOW_OK;
}

static ID3D11VideoDecoderOutputView *
gst_d3d11_mpeg2_dec_get_output_view_from_picture (GstD3D11Mpeg2Dec * self,
    GstMpeg2Picture * picture, guint8 * view_id)
{
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstBuffer *view_buffer;
  ID3D11VideoDecoderOutputView *view;

  if (!picture)
    return NULL;

  view_buffer = (GstBuffer *) gst_mpeg2_picture_get_user_data (picture);
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

static inline WORD
_pack_f_codes (guint8 f_code[2][2])
{
  return (((WORD) f_code[0][0] << 12)
      | ((WORD) f_code[0][1] << 8)
      | ((WORD) f_code[1][0] << 4)
      | (f_code[1][1]));
}

static inline WORD
_pack_pce_elements (GstMpeg2Slice * slice)
{
  return (((WORD) slice->pic_ext->intra_dc_precision << 14)
      | ((WORD) slice->pic_ext->picture_structure << 12)
      | ((WORD) slice->pic_ext->top_field_first << 11)
      | ((WORD) slice->pic_ext->frame_pred_frame_dct << 10)
      | ((WORD) slice->pic_ext->concealment_motion_vectors << 9)
      | ((WORD) slice->pic_ext->q_scale_type << 8)
      | ((WORD) slice->pic_ext->intra_vlc_format << 7)
      | ((WORD) slice->pic_ext->alternate_scan << 6)
      | ((WORD) slice->pic_ext->repeat_first_field << 5)
      | ((WORD) slice->pic_ext->chroma_420_type << 4)
      | ((WORD) slice->pic_ext->progressive_frame << 3));
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_start_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice,
    GstMpeg2Picture * prev_picture, GstMpeg2Picture * next_picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  DXVA_PictureParameters *pic_params = &inner->pic_params;
  DXVA_QmatrixData *iq_matrix = &inner->iq_matrix;
  ID3D11VideoDecoderOutputView *view;
  ID3D11VideoDecoderOutputView *other_view;
  guint8 view_id = 0xff;
  guint8 other_view_id = 0xff;
  gboolean is_field =
      picture->structure != GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;

  view = gst_d3d11_mpeg2_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (pic_params, 0, sizeof (DXVA_PictureParameters));
  memset (iq_matrix, 0, sizeof (DXVA_QmatrixData));

  /* Fill DXVA_PictureParameters */
  pic_params->wDecodedPictureIndex = view_id;
  pic_params->wForwardRefPictureIndex = 0xffff;
  pic_params->wBackwardRefPictureIndex = 0xffff;

  switch (picture->type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:{
      if (next_picture) {
        other_view =
            gst_d3d11_mpeg2_dec_get_output_view_from_picture (self,
            next_picture, &other_view_id);
        if (other_view)
          pic_params->wBackwardRefPictureIndex = other_view_id;
      }
    }
      /* fall-through */
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:{
      if (prev_picture) {
        other_view =
            gst_d3d11_mpeg2_dec_get_output_view_from_picture (self,
            prev_picture, &other_view_id);
        if (other_view)
          pic_params->wForwardRefPictureIndex = other_view_id;
      }
    }
    default:
      break;
  }

  pic_params->wPicWidthInMBminus1 = inner->width_in_mb - 1;
  pic_params->wPicHeightInMBminus1 = (inner->height_in_mb >> is_field) - 1;
  pic_params->bMacroblockWidthMinus1 = 15;
  pic_params->bMacroblockHeightMinus1 = 15;
  pic_params->bBlockWidthMinus1 = 7;
  pic_params->bBlockHeightMinus1 = 7;
  pic_params->bBPPminus1 = 7;
  pic_params->bPicStructure = (BYTE) picture->structure;
  if (picture->first_field && is_field) {
    pic_params->bSecondField = TRUE;
  }
  pic_params->bPicIntra = picture->type == GST_MPEG_VIDEO_PICTURE_TYPE_I;
  pic_params->bPicBackwardPrediction =
      picture->type == GST_MPEG_VIDEO_PICTURE_TYPE_B;
  /* FIXME: 1 -> 4:2:0, 2 -> 4:2:2, 3 -> 4:4:4 */
  pic_params->bChromaFormat = 1;
  pic_params->bPicScanFixed = 1;
  pic_params->bPicScanMethod = slice->pic_ext->alternate_scan;
  pic_params->wBitstreamFcodes = _pack_f_codes (slice->pic_ext->f_code);
  pic_params->wBitstreamPCEelements = _pack_pce_elements (slice);

  /* Fill DXVA_QmatrixData */
  if (slice->quant_matrix &&
      /* The value in bNewQmatrix[0] and bNewQmatrix[1] must not both be zero.
       * https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/dxva/ns-dxva-_dxva_qmatrixdata
       */
      (slice->quant_matrix->load_intra_quantiser_matrix ||
          slice->quant_matrix->load_non_intra_quantiser_matrix)) {
    GstMpegVideoQuantMatrixExt *quant_matrix = slice->quant_matrix;

    if (quant_matrix->load_intra_quantiser_matrix) {
      iq_matrix->bNewQmatrix[0] = 1;
      for (guint i = 0; i < 64; i++) {
        iq_matrix->Qmatrix[0][i] = quant_matrix->intra_quantiser_matrix[i];
      }
    }

    if (quant_matrix->load_non_intra_quantiser_matrix) {
      iq_matrix->bNewQmatrix[1] = 1;
      for (guint i = 0; i < 64; i++) {
        iq_matrix->Qmatrix[1][i] = quant_matrix->non_intra_quantiser_matrix[i];
      }
    }

    if (quant_matrix->load_chroma_intra_quantiser_matrix) {
      iq_matrix->bNewQmatrix[2] = 1;
      for (guint i = 0; i < 64; i++) {
        iq_matrix->Qmatrix[2][i] =
            quant_matrix->chroma_intra_quantiser_matrix[i];
      }
    }

    if (quant_matrix->load_chroma_non_intra_quantiser_matrix) {
      iq_matrix->bNewQmatrix[3] = 1;
      for (guint i = 0; i < 64; i++) {
        iq_matrix->Qmatrix[3][i] =
            quant_matrix->chroma_non_intra_quantiser_matrix[i];
      }
    }

    inner->submit_iq_data = TRUE;
  } else {
    inner->submit_iq_data = FALSE;
  }

  inner->slice_list.resize (0);
  inner->bitstream_buffer.resize (0);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_decode_slice (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture, GstMpeg2Slice * slice)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstMpegVideoSliceHdr *header = &slice->header;
  GstMpegVideoPacket *packet = &slice->packet;
  DXVA_SliceInfo slice_info = { 0, };

  g_assert (packet->offset >= 4);

  slice_info.wHorizontalPosition = header->mb_column;
  slice_info.wVerticalPosition = header->mb_row;
  /* including start code 4 bytes */
  slice_info.dwSliceBitsInBuffer = 8 * (packet->size + 4);
  slice_info.dwSliceDataLocation = inner->bitstream_buffer.size ();
  /* XXX: We don't have information about the number of MBs in this slice.
   * Just store offset here, and actual number will be calculated later */
  slice_info.wNumberMBsInSlice =
      (header->mb_row * inner->width_in_mb) + header->mb_column;
  slice_info.wQuantizerScaleCode = header->quantiser_scale_code;
  slice_info.wMBbitOffset = header->header_size + 32;

  inner->slice_list.push_back (slice_info);

  size_t pos = inner->bitstream_buffer.size ();
  inner->bitstream_buffer.resize (pos + packet->size + 4);
  memcpy (&inner->bitstream_buffer[0] + pos, packet->data + packet->offset - 4,
      packet->size + 4);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_end_picture (GstMpeg2Decoder * decoder,
    GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  ID3D11VideoDecoderOutputView *view;
  guint8 view_id = 0xff;
  GstD3D11DecodeInputStreamArgs input_args;
  gboolean is_field =
      picture->structure != GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;
  guint mb_count = inner->width_in_mb * (inner->height_in_mb >> is_field);

  if (inner->bitstream_buffer.empty ()) {
    GST_ERROR_OBJECT (self, "No bitstream buffer to submit");
    return GST_FLOW_ERROR;
  }

  view = gst_d3d11_mpeg2_dec_get_output_view_from_picture (self, picture,
      &view_id);
  if (!view) {
    GST_ERROR_OBJECT (self, "current picture does not have output view handle");
    return GST_FLOW_ERROR;
  }

  memset (&input_args, 0, sizeof (GstD3D11DecodeInputStreamArgs));

  DXVA_SliceInfo *first = &inner->slice_list[0];
  for (size_t i = 0; i < inner->slice_list.size (); i++) {
    DXVA_SliceInfo *slice = first + i;

    /* Update the number of MBs per slice */
    if (i == inner->slice_list.size () - 1) {
      slice->wNumberMBsInSlice = mb_count - slice->wNumberMBsInSlice;
    } else {
      DXVA_SliceInfo *next = first + i + 1;
      slice->wNumberMBsInSlice =
          next->wNumberMBsInSlice - slice->wNumberMBsInSlice;
    }
  }

  input_args.picture_params = &inner->pic_params;
  input_args.picture_params_size = sizeof (DXVA_PictureParameters);
  input_args.slice_control = &inner->slice_list[0];
  input_args.slice_control_size =
      sizeof (DXVA_SliceInfo) * inner->slice_list.size ();
  input_args.bitstream = &inner->bitstream_buffer[0];
  input_args.bitstream_size = inner->bitstream_buffer.size ();
  if (inner->submit_iq_data) {
    input_args.inverse_quantization_matrix = &inner->iq_matrix;
    input_args.inverse_quantization_matrix_size = sizeof (DXVA_QmatrixData);
  }

  return gst_d3d11_decoder_decode_frame (inner->d3d11_decoder,
      view, &input_args);
}

static GstFlowReturn
gst_d3d11_mpeg2_dec_output_picture (GstMpeg2Decoder * decoder,
    GstVideoCodecFrame * frame, GstMpeg2Picture * picture)
{
  GstD3D11Mpeg2Dec *self = GST_D3D11_MPEG2_DEC (decoder);
  GstD3D11Mpeg2DecInner *inner = self->inner;
  GstVideoDecoder *vdec = GST_VIDEO_DECODER (decoder);
  GstBuffer *view_buffer;

  GST_LOG_OBJECT (self, "Outputting picture %p", picture);

  view_buffer = (GstBuffer *) gst_mpeg2_picture_get_user_data (picture);

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

  if (picture->buffer_flags != 0) {
    gboolean interlaced =
        (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_INTERLACED) != 0;
    gboolean tff = (picture->buffer_flags & GST_VIDEO_BUFFER_FLAG_TFF) != 0;

    GST_TRACE_OBJECT (self,
        "apply buffer flags 0x%x (interlaced %d, top-field-first %d)",
        picture->buffer_flags, interlaced, tff);
    GST_BUFFER_FLAG_SET (frame->output_buffer, picture->buffer_flags);
  }

  gst_mpeg2_picture_unref (picture);

  return gst_video_decoder_finish_frame (vdec, frame);

error:
  gst_mpeg2_picture_unref (picture);
  gst_video_decoder_release_frame (vdec, frame);

  return GST_FLOW_ERROR;
}

void
gst_d3d11_mpeg2_dec_register (GstPlugin * plugin, GstD3D11Device * device,
    guint rank)
{
  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  GTypeInfo type_info = {
    sizeof (GstD3D11Mpeg2DecClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_d3d11_mpeg2_dec_class_init,
    NULL,
    NULL,
    sizeof (GstD3D11Mpeg2Dec),
    0,
    (GInstanceInitFunc) gst_d3d11_mpeg2_dec_init,
  };
  const GUID *supported_profile = NULL;
  GstCaps *sink_caps = NULL;
  GstCaps *src_caps = NULL;

  if (!gst_d3d11_decoder_get_supported_decoder_profile (device,
          GST_DXVA_CODEC_MPEG2, GST_VIDEO_FORMAT_NV12, &supported_profile)) {
    GST_INFO_OBJECT (device, "device does not support MPEG-2 video decoding");
    return;
  }

  sink_caps = gst_caps_from_string ("video/mpeg, "
      "mpegversion = (int)2, systemstream = (boolean) false, "
      "profile = (string) { main, simple }");
  src_caps = gst_caps_from_string ("video/x-raw("
      GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY "); video/x-raw");

  /* NOTE: We are supporting only 4:2:0, main or simple profiles */
  gst_caps_set_simple (src_caps, "format", G_TYPE_STRING, "NV12", NULL);

  type_info.class_data =
      gst_d3d11_decoder_class_data_new (device, GST_DXVA_CODEC_MPEG2,
      sink_caps, src_caps, 1920);

  type_name = g_strdup ("GstD3D11Mpeg2Dec");
  feature_name = g_strdup ("d3d11mpeg2dec");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D11Mpeg2Device%dDec", index);
    feature_name = g_strdup_printf ("d3d11mpeg2device%ddec", index);
  }

  type = g_type_register_static (GST_TYPE_MPEG2_DECODER,
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
