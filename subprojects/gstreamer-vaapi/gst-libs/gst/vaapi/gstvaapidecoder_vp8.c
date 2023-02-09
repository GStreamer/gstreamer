/*
 *  gstvaapidecoder_vp8.c - VP8 decoder
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Halley Zhao <halley.zhao@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapidecoder_vp8
 * @short_description: VP8 decoder
 */

#include "sysdeps.h"
#include <gst/codecparsers/gstvp8parser.h>
#include "gstvaapidecoder_vp8.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#include "gstvaapicompat.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DECODER_VP8_CAST(decoder) \
  ((GstVaapiDecoderVp8 *)(decoder))

typedef struct _GstVaapiDecoderVp8Private GstVaapiDecoderVp8Private;
typedef struct _GstVaapiDecoderVp8Class GstVaapiDecoderVp8Class;

struct _GstVaapiDecoderVp8Private
{
  GstVaapiProfile profile;
  guint width;
  guint height;
  GstVp8Parser parser;
  GstVp8FrameHdr frame_hdr;
  GstVaapiPicture *last_picture;
  GstVaapiPicture *golden_ref_picture;
  GstVaapiPicture *alt_ref_picture;
  GstVaapiPicture *current_picture;
  guint size_changed:1;
};

/**
 * GstVaapiDecoderVp8:
 *
 * A decoder based on Vp8.
 */
struct _GstVaapiDecoderVp8
{
  /*< private > */
  GstVaapiDecoder parent_instance;

  GstVaapiDecoderVp8Private priv;
};

/**
 * GstVaapiDecoderVp8Class:
 *
 * A decoder class based on Vp8.
 */
struct _GstVaapiDecoderVp8Class
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderVp8, gst_vaapi_decoder_vp8,
    GST_TYPE_VAAPI_DECODER);

static GstVaapiDecoderStatus
get_status (GstVp8ParserResult result)
{
  GstVaapiDecoderStatus status;

  switch (result) {
    case GST_VP8_PARSER_OK:
      status = GST_VAAPI_DECODER_STATUS_SUCCESS;
      break;
    case GST_VP8_PARSER_ERROR:
      status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
      break;
    default:
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
      break;
  }
  return status;
}

static void
gst_vaapi_decoder_vp8_close (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->last_picture, NULL);
  gst_vaapi_picture_replace (&priv->golden_ref_picture, NULL);
  gst_vaapi_picture_replace (&priv->alt_ref_picture, NULL);
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
}

static gboolean
gst_vaapi_decoder_vp8_open (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;

  gst_vaapi_decoder_vp8_close (decoder);
  gst_vp8_parser_init (&priv->parser);
  return TRUE;
}

static void
gst_vaapi_decoder_vp8_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp8 *const decoder = GST_VAAPI_DECODER_VP8_CAST (base_decoder);

  gst_vaapi_decoder_vp8_close (decoder);
}

static gboolean
gst_vaapi_decoder_vp8_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp8 *const decoder = GST_VAAPI_DECODER_VP8_CAST (base_decoder);
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;

  if (!gst_vaapi_decoder_vp8_open (decoder))
    return FALSE;

  priv->profile = GST_VAAPI_PROFILE_UNKNOWN;
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_vp8_destroy (base_decoder);
  if (gst_vaapi_decoder_vp8_create (base_decoder))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
  return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  const GstVaapiProfile profile = GST_VAAPI_PROFILE_VP8;
  const GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  gboolean reset_context = FALSE;

  if (priv->profile != profile) {
    if (!gst_vaapi_display_has_decoder (GST_VAAPI_DECODER_DISPLAY (decoder),
            profile, entrypoint))
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

    priv->profile = profile;
    reset_context = TRUE;
  }

  if (priv->size_changed) {
    GST_DEBUG ("size changed");
    priv->size_changed = FALSE;
    reset_context = TRUE;
  }

  if (reset_context) {
    GstVaapiContextInfo info;
    /* *INDENT-OFF* */
    info = (GstVaapiContextInfo) {
      .profile = priv->profile,
      .entrypoint = entrypoint,
      .chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420,
      .width = priv->width,
      .height = priv->height,
      .ref_frames = 3,
    };
    /* *INDENT-ON* */

    reset_context =
        gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info);

    if (!reset_context)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_quant_matrix (GstVaapiDecoderVp8 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVp8Segmentation *const seg = &priv->parser.segmentation;
  VAIQMatrixBufferVP8 *iq_matrix;
  const gint8 QI_MAX = 127;
  gint8 qi, qi_base;
  gint i;

  picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW (VP8, decoder);
  if (!picture->iq_matrix) {
    GST_ERROR ("failed to allocate IQ matrix");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  iq_matrix = picture->iq_matrix->param;

  /* Fill in VAIQMatrixBufferVP8 */
  for (i = 0; i < 4; i++) {
    if (seg->segmentation_enabled) {
      qi_base = seg->quantizer_update_value[i];
      if (!seg->segment_feature_mode)   // 0 means delta update
        qi_base += frame_hdr->quant_indices.y_ac_qi;
    } else
      qi_base = frame_hdr->quant_indices.y_ac_qi;

    qi = qi_base;
    iq_matrix->quantization_index[i][0] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y_dc_delta;
    iq_matrix->quantization_index[i][1] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y2_dc_delta;
    iq_matrix->quantization_index[i][2] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.y2_ac_delta;
    iq_matrix->quantization_index[i][3] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.uv_dc_delta;
    iq_matrix->quantization_index[i][4] = CLAMP (qi, 0, QI_MAX);
    qi = qi_base + frame_hdr->quant_indices.uv_ac_delta;
    iq_matrix->quantization_index[i][5] = CLAMP (qi, 0, QI_MAX);
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_probability_table (GstVaapiDecoderVp8 * decoder,
    GstVaapiPicture * picture)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;
  VAProbabilityDataBufferVP8 *prob_table;

  picture->prob_table = GST_VAAPI_PROBABILITY_TABLE_NEW (VP8, decoder);
  if (!picture->prob_table) {
    GST_ERROR ("failed to allocate probality table");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  prob_table = picture->prob_table->param;

  /* Fill in VAProbabilityDataBufferVP8 */
  memcpy (prob_table->dct_coeff_probs, frame_hdr->token_probs.prob,
      sizeof (frame_hdr->token_probs.prob));

  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
init_picture (GstVaapiDecoderVp8 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;

  picture->structure = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
  picture->type = frame_hdr->key_frame ? GST_VAAPI_PICTURE_TYPE_I :
      GST_VAAPI_PICTURE_TYPE_P;
  picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;

  if (!frame_hdr->show_frame)
    GST_VAAPI_PICTURE_FLAG_SET (picture, GST_VAAPI_PICTURE_FLAG_SKIPPED);
}

static gboolean
fill_picture (GstVaapiDecoderVp8 * decoder, GstVaapiPicture * picture)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  VAPictureParameterBufferVP8 *const pic_param = picture->param;
  GstVp8Parser *const parser = &priv->parser;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVp8Segmentation *const seg = &parser->segmentation;
  gint i;

  /* Fill in VAPictureParameterBufferVP8 */
  pic_param->frame_width = priv->width;
  pic_param->frame_height = priv->height;

  pic_param->last_ref_frame = VA_INVALID_SURFACE;
  pic_param->golden_ref_frame = VA_INVALID_SURFACE;
  pic_param->alt_ref_frame = VA_INVALID_SURFACE;
  if (!frame_hdr->key_frame) {
    if (priv->last_picture)
      pic_param->last_ref_frame = priv->last_picture->surface_id;
    if (priv->golden_ref_picture)
      pic_param->golden_ref_frame = priv->golden_ref_picture->surface_id;
    if (priv->alt_ref_picture)
      pic_param->alt_ref_frame = priv->alt_ref_picture->surface_id;
  }
  pic_param->out_of_loop_frame = VA_INVALID_SURFACE;    // not used currently

  pic_param->pic_fields.value = 0;
  pic_param->pic_fields.bits.key_frame = !frame_hdr->key_frame;
  pic_param->pic_fields.bits.version = frame_hdr->version;
  pic_param->pic_fields.bits.segmentation_enabled = seg->segmentation_enabled;
  pic_param->pic_fields.bits.update_mb_segmentation_map =
      seg->update_mb_segmentation_map;
  pic_param->pic_fields.bits.update_segment_feature_data =
      seg->update_segment_feature_data;
  pic_param->pic_fields.bits.filter_type = frame_hdr->filter_type;
  pic_param->pic_fields.bits.sharpness_level = frame_hdr->sharpness_level;
  pic_param->pic_fields.bits.loop_filter_adj_enable =
      parser->mb_lf_adjust.loop_filter_adj_enable;
  pic_param->pic_fields.bits.mode_ref_lf_delta_update =
      parser->mb_lf_adjust.mode_ref_lf_delta_update;
  pic_param->pic_fields.bits.sign_bias_golden = frame_hdr->sign_bias_golden;
  pic_param->pic_fields.bits.sign_bias_alternate =
      frame_hdr->sign_bias_alternate;
  pic_param->pic_fields.bits.mb_no_coeff_skip = frame_hdr->mb_no_skip_coeff;

  for (i = 0; i < 3; i++)
    pic_param->mb_segment_tree_probs[i] = seg->segment_prob[i];

  for (i = 0; i < 4; i++) {
    gint8 level;
    if (seg->segmentation_enabled) {
      level = seg->lf_update_value[i];
      if (!seg->segment_feature_mode)   // 0 means delta update
        level += frame_hdr->loop_filter_level;
    } else
      level = frame_hdr->loop_filter_level;
    pic_param->loop_filter_level[i] = CLAMP (level, 0, 63);

    pic_param->loop_filter_deltas_ref_frame[i] =
        parser->mb_lf_adjust.ref_frame_delta[i];
    pic_param->loop_filter_deltas_mode[i] =
        parser->mb_lf_adjust.mb_mode_delta[i];
  }

  /* In decoding, the only loop filter settings that matter are those
     in the frame header (9.1) */
  pic_param->pic_fields.bits.loop_filter_disable =
      frame_hdr->loop_filter_level == 0;

  pic_param->prob_skip_false = frame_hdr->prob_skip_false;
  pic_param->prob_intra = frame_hdr->prob_intra;
  pic_param->prob_last = frame_hdr->prob_last;
  pic_param->prob_gf = frame_hdr->prob_gf;

  memcpy (pic_param->y_mode_probs, frame_hdr->mode_probs.y_prob,
      sizeof (frame_hdr->mode_probs.y_prob));
  memcpy (pic_param->uv_mode_probs, frame_hdr->mode_probs.uv_prob,
      sizeof (frame_hdr->mode_probs.uv_prob));
  memcpy (pic_param->mv_probs, frame_hdr->mv_probs.prob,
      sizeof (frame_hdr->mv_probs));

  pic_param->bool_coder_ctx.range = frame_hdr->rd_range;
  pic_param->bool_coder_ctx.value = frame_hdr->rd_value;
  pic_param->bool_coder_ctx.count = frame_hdr->rd_count;

  return TRUE;
}

static gboolean
fill_slice (GstVaapiDecoderVp8 * decoder, GstVaapiSlice * slice)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  VASliceParameterBufferVP8 *const slice_param = slice->param;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;
  gint i;

  /* Fill in VASliceParameterBufferVP8 */
  slice_param->slice_data_offset = frame_hdr->data_chunk_size;
  slice_param->macroblock_offset = frame_hdr->header_size;
  slice_param->num_of_partitions =
      (1 << frame_hdr->log2_nbr_of_dct_partitions) + 1;

  slice_param->partition_size[0] =
      frame_hdr->first_part_size - ((slice_param->macroblock_offset + 7) >> 3);
  for (i = 1; i < slice_param->num_of_partitions; i++)
    slice_param->partition_size[i] = frame_hdr->partition_size[i - 1];
  for (; i < G_N_ELEMENTS (slice_param->partition_size); i++)
    slice_param->partition_size[i] = 0;

  return TRUE;
}

static GstVaapiDecoderStatus
decode_slice (GstVaapiDecoderVp8 * decoder, GstVaapiPicture * picture,
    const guchar * buf, guint buf_size)
{
  GstVaapiSlice *slice;

  slice = GST_VAAPI_SLICE_NEW (VP8, decoder, buf, buf_size);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (!fill_slice (decoder, slice)) {
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (slice));
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  gst_vaapi_picture_add_slice (GST_VAAPI_PICTURE_CAST (picture), slice);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderVp8 * decoder, const guchar * buf,
    guint buf_size)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVaapiPicture *picture;
  GstVaapiDecoderStatus status;

  status = ensure_context (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  /* Create new picture */
  picture = GST_VAAPI_PICTURE_NEW (VP8, decoder);
  if (!picture) {
    GST_ERROR ("failed to allocate picture");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  status = ensure_quant_matrix (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  status = ensure_probability_table (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  init_picture (decoder, picture);
  if (!fill_picture (decoder, picture))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  return decode_slice (decoder, picture, buf, buf_size);
}

static void
update_ref_frames (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVaapiPicture *picture = priv->current_picture;
  GstVp8FrameHdr *const frame_hdr = &priv->frame_hdr;

  // update picture reference
  if (frame_hdr->key_frame) {
    gst_vaapi_picture_replace (&priv->golden_ref_picture, picture);
    gst_vaapi_picture_replace (&priv->alt_ref_picture, picture);
  } else {
    // process refresh_alternate_frame/copy_buffer_to_alternate first
    if (frame_hdr->refresh_alternate_frame) {
      gst_vaapi_picture_replace (&priv->alt_ref_picture, picture);
    } else {
      switch (frame_hdr->copy_buffer_to_alternate) {
        case 0:
          // do nothing
          break;
        case 1:
          gst_vaapi_picture_replace (&priv->alt_ref_picture,
              priv->last_picture);
          break;
        case 2:
          gst_vaapi_picture_replace (&priv->alt_ref_picture,
              priv->golden_ref_picture);
          break;
        default:
          GST_WARNING
              ("WARNING: VP8 decoder: unrecognized copy_buffer_to_alternate");
      }
    }

    if (frame_hdr->refresh_golden_frame) {
      gst_vaapi_picture_replace (&priv->golden_ref_picture, picture);
    } else {
      switch (frame_hdr->copy_buffer_to_golden) {
        case 0:
          // do nothing
          break;
        case 1:
          gst_vaapi_picture_replace (&priv->golden_ref_picture,
              priv->last_picture);
          break;
        case 2:
          gst_vaapi_picture_replace (&priv->golden_ref_picture,
              priv->alt_ref_picture);
          break;
        default:
          GST_WARNING
              ("WARNING: VP8 decoder: unrecognized copy_buffer_to_golden");
      }
    }
  }
  if (frame_hdr->key_frame || frame_hdr->refresh_last)
    gst_vaapi_picture_replace (&priv->last_picture, picture);
}

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  update_ref_frames (decoder);
  if (!gst_vaapi_picture_decode (picture))
    goto error;
  if (!gst_vaapi_picture_output (picture))
    goto error;
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    /* XXX: fix for cases where first field failed to be decoded */
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
}

static GstVaapiDecoderStatus
parse_frame_header (GstVaapiDecoderVp8 * decoder, const guchar * buf,
    guint buf_size, GstVp8FrameHdr * frame_hdr)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVp8ParserResult result;

  memset (frame_hdr, 0, sizeof (*frame_hdr));
  result = gst_vp8_parser_parse_frame_header (&priv->parser, frame_hdr,
      buf, buf_size);
  if (result != GST_VP8_PARSER_OK)
    return get_status (result);

  if (frame_hdr->key_frame &&
      (frame_hdr->width != priv->width || frame_hdr->height != priv->height)) {
    priv->width = frame_hdr->width;
    priv->height = frame_hdr->height;
    priv->size_changed = TRUE;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  guint flags = 0;

  unit->size = gst_adapter_available (adapter);

  /* The whole frame is available */
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
  flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

}

static GstVaapiDecoderStatus
decode_buffer (GstVaapiDecoderVp8 * decoder, const guchar * buf, guint buf_size)
{
  GstVaapiDecoderVp8Private *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  status = parse_frame_header (decoder, buf, buf_size, &priv->frame_hdr);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  return decode_picture (decoder, buf, buf_size);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderVp8 *const decoder = GST_VAAPI_DECODER_VP8_CAST (base_decoder);
  GstVaapiDecoderStatus status;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  status = decode_buffer (decoder, map_info.data + unit->offset, unit->size);
  gst_buffer_unmap (buffer, &map_info);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * base_unit)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderVp8 *const decoder = GST_VAAPI_DECODER_VP8_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_vp8_flush (GstVaapiDecoder * base_decoder)
{
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_vp8_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_vp8_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_vp8_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_vp8_class_init (GstVaapiDecoderVp8Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_vp8_finalize;

  decoder_class->reset = gst_vaapi_decoder_vp8_reset;
  decoder_class->parse = gst_vaapi_decoder_vp8_parse;
  decoder_class->decode = gst_vaapi_decoder_vp8_decode;
  decoder_class->start_frame = gst_vaapi_decoder_vp8_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_vp8_end_frame;
  decoder_class->flush = gst_vaapi_decoder_vp8_flush;
}

static void
gst_vaapi_decoder_vp8_init (GstVaapiDecoderVp8 * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_vp8_create (base_decoder);
}

/**
 * gst_vaapi_decoder_vp8_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for VP8 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_vp8_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_VP8, "display", display,
      "caps", caps, NULL);
}
