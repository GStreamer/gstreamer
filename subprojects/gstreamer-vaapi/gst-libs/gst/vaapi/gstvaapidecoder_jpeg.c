/*
 *  gstvaapidecoder_jpeg.c - JPEG decoder
 *
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
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
 * SECTION:gstvaapidecoder_jpeg
 * @short_description: JPEG decoder
 */

#include "sysdeps.h"
#include <gst/codecparsers/gstjpegparser.h>
#include "gstvaapicompat.h"
#include "gstvaapidecoder_jpeg.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_DECODER_JPEG_CAST(decoder) \
    ((GstVaapiDecoderJpeg *)(decoder))

typedef struct _GstVaapiDecoderJpegPrivate GstVaapiDecoderJpegPrivate;
typedef struct _GstVaapiDecoderJpegClass GstVaapiDecoderJpegClass;

typedef enum
{
  GST_JPEG_VIDEO_STATE_GOT_SOI = 1 << 0,
  GST_JPEG_VIDEO_STATE_GOT_SOF = 1 << 1,
  GST_JPEG_VIDEO_STATE_GOT_SOS = 1 << 2,
  GST_JPEG_VIDEO_STATE_GOT_HUF_TABLE = 1 << 3,
  GST_JPEG_VIDEO_STATE_GOT_IQ_TABLE = 1 << 4,

  GST_JPEG_VIDEO_STATE_VALID_PICTURE = (GST_JPEG_VIDEO_STATE_GOT_SOI |
      GST_JPEG_VIDEO_STATE_GOT_SOF | GST_JPEG_VIDEO_STATE_GOT_SOS),
} GstJpegVideoState;

struct _GstVaapiDecoderJpegPrivate
{
  GstVaapiProfile profile;
  guint width;
  guint height;
  GstVaapiPicture *current_picture;
  GstJpegFrameHdr frame_hdr;
  GstJpegHuffmanTables huf_tables;
  GstJpegQuantTables quant_tables;
  guint mcu_restart;
  guint parser_state;
  guint decoder_state;
  guint is_opened:1;
  guint profile_changed:1;
  guint size_changed:1;
};

/**
 * GstVaapiDecoderJpeg:
 *
 * A decoder based on Jpeg.
 */
struct _GstVaapiDecoderJpeg
{
  /*< private > */
  GstVaapiDecoder parent_instance;
  GstVaapiDecoderJpegPrivate priv;
};

/**
 * GstVaapiDecoderJpegClass:
 *
 * A decoder class based on Jpeg.
 */
struct _GstVaapiDecoderJpegClass
{
  /*< private > */
  GstVaapiDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiDecoderJpeg, gst_vaapi_decoder_jpeg,
    GST_TYPE_VAAPI_DECODER);

static inline void
unit_set_marker_code (GstVaapiDecoderUnit * unit, GstJpegMarker marker)
{
  unit->parsed_info = GSIZE_TO_POINTER (marker);
}

static inline GstJpegMarker
unit_get_marker_code (GstVaapiDecoderUnit * unit)
{
  return GPOINTER_TO_SIZE (unit->parsed_info);
}

static void
gst_vaapi_decoder_jpeg_close (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  gst_vaapi_picture_replace (&priv->current_picture, NULL);

  /* Reset all */
  priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
  priv->width = 0;
  priv->height = 0;
  priv->is_opened = FALSE;
  priv->profile_changed = TRUE;
  priv->size_changed = TRUE;
}

static gboolean
gst_vaapi_decoder_jpeg_open (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  gst_vaapi_decoder_jpeg_close (decoder);

  priv->parser_state = 0;
  priv->decoder_state = 0;
  return TRUE;
}

static void
gst_vaapi_decoder_jpeg_destroy (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);

  gst_vaapi_decoder_jpeg_close (decoder);
}

static gboolean
gst_vaapi_decoder_jpeg_create (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
  priv->profile_changed = TRUE;
  priv->size_changed = TRUE;
  return TRUE;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_reset (GstVaapiDecoder * base_decoder)
{
  gst_vaapi_decoder_jpeg_destroy (base_decoder);
  gst_vaapi_decoder_jpeg_create (base_decoder);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
get_chroma_type (GstJpegFrameHdr * frame_hdr, GstVaapiChromaType * chroma_type)
{
  int h0 = frame_hdr->components[0].horizontal_factor;
  int h1 = frame_hdr->components[1].horizontal_factor;
  int h2 = frame_hdr->components[2].horizontal_factor;
  int v0 = frame_hdr->components[0].vertical_factor;
  int v1 = frame_hdr->components[1].vertical_factor;
  int v2 = frame_hdr->components[2].vertical_factor;

  if (frame_hdr->num_components == 1) {
    *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV400;
    return TRUE;
  }

  if (h1 != h2 || v1 != v2)
    return FALSE;

  if (h0 == h1) {
    if (v0 == v1)
      *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
    else if (v0 == 2 * v1)
      *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
    else
      return FALSE;
  } else if (h0 == 2 * h1) {
    if (v0 == v1)
      *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
    else if (v0 == 2 * v1)
      *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    else
      return FALSE;
  } else if (h0 == 4 * h1) {
    if (v0 == v1)
      *chroma_type = GST_VAAPI_CHROMA_TYPE_YUV411;
    else
      return FALSE;
  } else
    return FALSE;

  return TRUE;
}

static GstVaapiDecoderStatus
ensure_context (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstJpegFrameHdr *const frame_hdr = &priv->frame_hdr;
  GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
  GstVaapiProfile profiles[2];
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
  guint i, n_profiles = 0;
  gboolean reset_context = FALSE;

  if (priv->profile_changed) {
    GST_DEBUG ("profile changed");
    priv->profile_changed = FALSE;
    reset_context = TRUE;

    profiles[n_profiles++] = priv->profile;
    //if (priv->profile == GST_VAAPI_PROFILE_JPEG_EXTENDED)
    //    profiles[n_profiles++] = GST_VAAPI_PROFILE_JPEG_BASELINE;

    for (i = 0; i < n_profiles; i++) {
      if (gst_vaapi_display_has_decoder (GST_VAAPI_DECODER_DISPLAY (decoder),
              profiles[i], entrypoint))
        break;
    }
    if (i == n_profiles)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    priv->profile = profiles[i];
  }

  if (priv->size_changed) {
    GST_DEBUG ("size changed");
    priv->size_changed = FALSE;
    reset_context = TRUE;
  }

  if (reset_context) {
    GstVaapiContextInfo info;

    if (!get_chroma_type (frame_hdr, &chroma_type))
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;

  /* *INDENT-OFF* */
    info = (GstVaapiContextInfo) {
      .profile = priv->profile,
      .entrypoint = entrypoint,
      .chroma_type = chroma_type,
      .width = priv->width,
      .height = priv->height,
      .ref_frames = 2,
    };
  /* *INDENT-ON* */

    reset_context =
        gst_vaapi_decoder_ensure_context (GST_VAAPI_DECODER (decoder), &info);
    if (!reset_context)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gboolean
is_valid_state (guint state, guint ref_state)
{
  return (state & ref_state) == ref_state;
}

#define VALID_STATE(TYPE, STATE)                \
    is_valid_state(priv->G_PASTE(TYPE,_state),  \
        G_PASTE(GST_JPEG_VIDEO_STATE_,STATE))

static GstVaapiDecoderStatus
decode_current_picture (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;

  if (!VALID_STATE (decoder, VALID_PICTURE))
    goto drop_frame;
  priv->decoder_state = 0;

  if (!picture)
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_vaapi_picture_decode (picture))
    goto error;
  if (!gst_vaapi_picture_output (picture))
    goto error;
  gst_vaapi_picture_replace (&priv->current_picture, NULL);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    gst_vaapi_picture_replace (&priv->current_picture, NULL);
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

drop_frame:
  {
    priv->decoder_state = 0;
    return (GstVaapiDecoderStatus) GST_VAAPI_DECODER_STATUS_DROP_FRAME;
  }
}

static gboolean
fill_picture (GstVaapiDecoderJpeg * decoder,
    GstVaapiPicture * picture, GstJpegFrameHdr * frame_hdr)
{
  VAPictureParameterBufferJPEGBaseline *const pic_param = picture->param;
  guint i;

  memset (pic_param, 0, sizeof (VAPictureParameterBufferJPEGBaseline));
  pic_param->picture_width = frame_hdr->width;
  pic_param->picture_height = frame_hdr->height;

  pic_param->num_components = frame_hdr->num_components;
  if (frame_hdr->num_components > 4)
    return FALSE;
  for (i = 0; i < pic_param->num_components; i++) {
    pic_param->components[i].component_id = frame_hdr->components[i].identifier;
    pic_param->components[i].h_sampling_factor =
        frame_hdr->components[i].horizontal_factor;
    pic_param->components[i].v_sampling_factor =
        frame_hdr->components[i].vertical_factor;
    pic_param->components[i].quantiser_table_selector =
        frame_hdr->components[i].quant_table_selector;
  }
  return TRUE;
}

static GstVaapiDecoderStatus
fill_quantization_table (GstVaapiDecoderJpeg * decoder,
    GstVaapiPicture * picture)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  VAIQMatrixBufferJPEGBaseline *iq_matrix;
  guint i, j, num_tables;

  if (!VALID_STATE (decoder, GOT_IQ_TABLE))
    gst_jpeg_get_default_quantization_tables (&priv->quant_tables);

  picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW (JPEGBaseline, decoder);
  if (!picture->iq_matrix) {
    GST_ERROR ("failed to allocate quantiser table");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  iq_matrix = picture->iq_matrix->param;

  num_tables = MIN (G_N_ELEMENTS (iq_matrix->quantiser_table),
      GST_JPEG_MAX_QUANT_ELEMENTS);

  for (i = 0; i < num_tables; i++) {
    GstJpegQuantTable *const quant_table = &priv->quant_tables.quant_tables[i];

    iq_matrix->load_quantiser_table[i] = quant_table->valid;
    if (!iq_matrix->load_quantiser_table[i])
      continue;

    if (quant_table->quant_precision != 0) {
      // Only Baseline profile is supported, thus 8-bit Qk values
      GST_ERROR ("unsupported quantization table element precision");
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
    }

    for (j = 0; j < GST_JPEG_MAX_QUANT_ELEMENTS; j++)
      iq_matrix->quantiser_table[i][j] = quant_table->quant_table[j];
    iq_matrix->load_quantiser_table[i] = 1;
    quant_table->valid = FALSE;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
huffman_tables_updated (const GstJpegHuffmanTables * huf_tables)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (huf_tables->dc_tables); i++)
    if (huf_tables->dc_tables[i].valid)
      return TRUE;
  for (i = 0; i < G_N_ELEMENTS (huf_tables->ac_tables); i++)
    if (huf_tables->ac_tables[i].valid)
      return TRUE;
  return FALSE;
}

static void
huffman_tables_reset (GstJpegHuffmanTables * huf_tables)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (huf_tables->dc_tables); i++)
    huf_tables->dc_tables[i].valid = FALSE;
  for (i = 0; i < G_N_ELEMENTS (huf_tables->ac_tables); i++)
    huf_tables->ac_tables[i].valid = FALSE;
}

static void
fill_huffman_table (GstVaapiHuffmanTable * huf_table,
    const GstJpegHuffmanTables * huf_tables)
{
  VAHuffmanTableBufferJPEGBaseline *const huffman_table = huf_table->param;
  guint i, num_tables;

  num_tables = MIN (G_N_ELEMENTS (huffman_table->huffman_table),
      GST_JPEG_MAX_SCAN_COMPONENTS);

  for (i = 0; i < num_tables; i++) {
    huffman_table->load_huffman_table[i] =
        huf_tables->dc_tables[i].valid && huf_tables->ac_tables[i].valid;
    if (!huffman_table->load_huffman_table[i])
      continue;

    memcpy (huffman_table->huffman_table[i].num_dc_codes,
        huf_tables->dc_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_dc_codes));
    memcpy (huffman_table->huffman_table[i].dc_values,
        huf_tables->dc_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].dc_values));
    memcpy (huffman_table->huffman_table[i].num_ac_codes,
        huf_tables->ac_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_ac_codes));
    memcpy (huffman_table->huffman_table[i].ac_values,
        huf_tables->ac_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].ac_values));
    memset (huffman_table->huffman_table[i].pad,
        0, sizeof (huffman_table->huffman_table[i].pad));
  }
}

static void
get_max_sampling_factors (const GstJpegFrameHdr * frame_hdr,
    guint * h_max_ptr, guint * v_max_ptr)
{
  guint h_max = frame_hdr->components[0].horizontal_factor;
  guint v_max = frame_hdr->components[0].vertical_factor;
  guint i;

  for (i = 1; i < frame_hdr->num_components; i++) {
    const GstJpegFrameComponent *const fcp = &frame_hdr->components[i];
    if (h_max < fcp->horizontal_factor)
      h_max = fcp->horizontal_factor;
    if (v_max < fcp->vertical_factor)
      v_max = fcp->vertical_factor;
  }

  if (h_max_ptr)
    *h_max_ptr = h_max;
  if (v_max_ptr)
    *v_max_ptr = v_max;
}

static const GstJpegFrameComponent *
get_component (const GstJpegFrameHdr * frame_hdr, guint selector)
{
  guint i;

  for (i = 0; i < frame_hdr->num_components; i++) {
    const GstJpegFrameComponent *const fcp = &frame_hdr->components[i];
    if (fcp->identifier == selector)
      return fcp;
  }
  return NULL;
}

static GstVaapiDecoderStatus
decode_picture (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstJpegFrameHdr *const frame_hdr = &priv->frame_hdr;

  if (!VALID_STATE (decoder, GOT_SOI))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  switch (seg->marker) {
    case GST_JPEG_MARKER_SOF_MIN:
      priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
      break;
    default:
      GST_ERROR ("unsupported profile %d", seg->marker);
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
  }

  memset (frame_hdr, 0, sizeof (*frame_hdr));
  if (!gst_jpeg_segment_parse_frame_header (seg, frame_hdr)) {
    GST_ERROR ("failed to parse image");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  if (priv->height != frame_hdr->height || priv->width != frame_hdr->width)
    priv->size_changed = TRUE;

  priv->height = frame_hdr->height;
  priv->width = frame_hdr->width;

  priv->decoder_state |= GST_JPEG_VIDEO_STATE_GOT_SOF;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_huffman_table (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  if (!VALID_STATE (decoder, GOT_SOI))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_jpeg_segment_parse_huffman_table (seg, &priv->huf_tables)) {
    GST_ERROR ("failed to parse Huffman table");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  priv->decoder_state |= GST_JPEG_VIDEO_STATE_GOT_HUF_TABLE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_quant_table (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  if (!VALID_STATE (decoder, GOT_SOI))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_jpeg_segment_parse_quantization_table (seg, &priv->quant_tables)) {
    GST_ERROR ("failed to parse quantization table");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  priv->decoder_state |= GST_JPEG_VIDEO_STATE_GOT_IQ_TABLE;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_restart_interval (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  if (!VALID_STATE (decoder, GOT_SOI))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  if (!gst_jpeg_segment_parse_restart_interval (seg, &priv->mcu_restart)) {
    GST_ERROR ("failed to parse restart interval");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_scan (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstVaapiPicture *const picture = priv->current_picture;
  GstVaapiSlice *slice;
  VASliceParameterBufferJPEGBaseline *slice_param;
  GstJpegScanHdr scan_hdr;
  guint scan_hdr_size, scan_data_size;
  guint i, h_max, v_max, mcu_width, mcu_height;

  if (!VALID_STATE (decoder, GOT_SOF))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  scan_hdr_size = (seg->data[seg->offset] << 8) | seg->data[seg->offset + 1];
  scan_data_size = seg->size - scan_hdr_size;

  memset (&scan_hdr, 0, sizeof (scan_hdr));
  if (!gst_jpeg_segment_parse_scan_header (seg, &scan_hdr)) {
    GST_ERROR ("failed to parse scan header");
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
  }

  slice = GST_VAAPI_SLICE_NEW (JPEGBaseline, decoder,
      seg->data + seg->offset + scan_hdr_size, scan_data_size);
  if (!slice) {
    GST_ERROR ("failed to allocate slice");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_add_slice (picture, slice);

  if (!VALID_STATE (decoder, GOT_HUF_TABLE))
    gst_jpeg_get_default_huffman_tables (&priv->huf_tables);

  // Update VA Huffman table if it changed for this scan
  if (huffman_tables_updated (&priv->huf_tables)) {
    slice->huf_table = GST_VAAPI_HUFFMAN_TABLE_NEW (JPEGBaseline, decoder);
    if (!slice->huf_table) {
      GST_ERROR ("failed to allocate Huffman tables");
      huffman_tables_reset (&priv->huf_tables);
      return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    fill_huffman_table (slice->huf_table, &priv->huf_tables);
    huffman_tables_reset (&priv->huf_tables);
  }

  slice_param = slice->param;
  slice_param->num_components = scan_hdr.num_components;
  for (i = 0; i < scan_hdr.num_components; i++) {
    slice_param->components[i].component_selector =
        scan_hdr.components[i].component_selector;
    slice_param->components[i].dc_table_selector =
        scan_hdr.components[i].dc_selector;
    slice_param->components[i].ac_table_selector =
        scan_hdr.components[i].ac_selector;
  }
  slice_param->restart_interval = priv->mcu_restart;
  slice_param->slice_horizontal_position = 0;
  slice_param->slice_vertical_position = 0;

  get_max_sampling_factors (&priv->frame_hdr, &h_max, &v_max);
  mcu_width = 8 * h_max;
  mcu_height = 8 * v_max;

  if (scan_hdr.num_components == 1) {   // Non-interleaved
    const guint Csj = slice_param->components[0].component_selector;
    const GstJpegFrameComponent *const fcp =
        get_component (&priv->frame_hdr, Csj);

    if (!fcp || fcp->horizontal_factor == 0 || fcp->vertical_factor == 0) {
      GST_ERROR ("failed to validate image component %u", Csj);
      return GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER;
    }
    mcu_width /= fcp->horizontal_factor;
    mcu_height /= fcp->vertical_factor;
  }
  slice_param->num_mcus =
      ((priv->frame_hdr.width + mcu_width - 1) / mcu_width) *
      ((priv->frame_hdr.height + mcu_height - 1) / mcu_height);

  priv->decoder_state |= GST_JPEG_VIDEO_STATE_GOT_SOS;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_segment (GstVaapiDecoderJpeg * decoder, GstJpegSegment * seg)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstVaapiDecoderStatus status;

  // Decode segment
  status = GST_VAAPI_DECODER_STATUS_SUCCESS;
  switch (seg->marker) {
    case GST_JPEG_MARKER_SOI:
      priv->mcu_restart = 0;
      priv->decoder_state |= GST_JPEG_VIDEO_STATE_GOT_SOI;
      break;
    case GST_JPEG_MARKER_EOI:
      priv->decoder_state = 0;
      break;
    case GST_JPEG_MARKER_DAC:
      GST_ERROR ("unsupported arithmetic coding mode");
      status = GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
      break;
    case GST_JPEG_MARKER_DHT:
      status = decode_huffman_table (decoder, seg);
      break;
    case GST_JPEG_MARKER_DQT:
      status = decode_quant_table (decoder, seg);
      break;
    case GST_JPEG_MARKER_DRI:
      status = decode_restart_interval (decoder, seg);
      break;
    case GST_JPEG_MARKER_SOS:
      status = decode_scan (decoder, seg);
      break;
    default:
      // SOFn segments
      if (seg->marker >= GST_JPEG_MARKER_SOF_MIN &&
          seg->marker <= GST_JPEG_MARKER_SOF_MAX)
        status = decode_picture (decoder, seg);
      break;
  }
  return status;
}

static GstVaapiDecoderStatus
ensure_decoder (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;

  if (!priv->is_opened) {
    priv->is_opened = gst_vaapi_decoder_jpeg_open (decoder);
    if (!priv->is_opened)
      return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
  }
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
is_scan_complete (GstJpegMarker marker)
{
  // Scan is assumed to be complete when the new segment is not RSTi
  return marker < GST_JPEG_MARKER_RST_MIN || marker > GST_JPEG_MARKER_RST_MAX;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_parse (GstVaapiDecoder * base_decoder,
    GstAdapter * adapter, gboolean at_eos, GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstVaapiParserState *const ps = GST_VAAPI_PARSER_STATE (base_decoder);
  GstVaapiDecoderStatus status;
  GstJpegMarker marker;
  GstJpegSegment seg;
  const guchar *buf;
  guint buf_size, flags;
  gint ofs1, ofs2;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  /* Expect at least 2 bytes for the marker */
  buf_size = gst_adapter_available (adapter);
  if (buf_size < 2)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  buf = gst_adapter_map (adapter, buf_size);
  if (!buf)
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

  ofs1 = ps->input_offset1 - 2;
  if (ofs1 < 0)
    ofs1 = 0;

  for (;;) {
    // Skip any garbage until we reach SOI, if needed
    if (!gst_jpeg_parse (&seg, buf, buf_size, ofs1)) {
      gst_adapter_unmap (adapter);
      ps->input_offset1 = buf_size;
      return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }
    ofs1 = seg.offset;

    marker = seg.marker;
    if (!VALID_STATE (parser, GOT_SOI) && marker != GST_JPEG_MARKER_SOI)
      continue;
    if (marker == GST_JPEG_MARKER_SOS) {
      ofs2 = ps->input_offset2 - 2;
      if (ofs2 < ofs1 + seg.size)
        ofs2 = ofs1 + seg.size;

      // Parse the whole scan + ECSs, including RSTi
      for (;;) {
        if (!gst_jpeg_parse (&seg, buf, buf_size, ofs2)) {
          gst_adapter_unmap (adapter);
          ps->input_offset1 = ofs1;
          ps->input_offset2 = buf_size;
          return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        }

        if (is_scan_complete (seg.marker))
          break;
        ofs2 = seg.offset + seg.size;
      }
      ofs2 = seg.offset - 2;
    } else {
      // Check that the whole segment is actually available (in buffer)
      ofs2 = ofs1 + seg.size;
      if (ofs2 > buf_size) {
        gst_adapter_unmap (adapter);
        ps->input_offset1 = ofs1;
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
      }
    }
    break;
  }
  gst_adapter_unmap (adapter);

  unit->size = ofs2 - ofs1;
  unit_set_marker_code (unit, marker);
  gst_adapter_flush (adapter, ofs1);
  ps->input_offset1 = 2;
  ps->input_offset2 = 2;

  flags = 0;
  switch (marker) {
    case GST_JPEG_MARKER_SOI:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
      priv->parser_state |= GST_JPEG_VIDEO_STATE_GOT_SOI;
      break;
    case GST_JPEG_MARKER_EOI:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
      priv->parser_state = 0;
      break;
    case GST_JPEG_MARKER_SOS:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      priv->parser_state |= GST_JPEG_VIDEO_STATE_GOT_SOS;
      break;
    case GST_JPEG_MARKER_DAC:
    case GST_JPEG_MARKER_DHT:
    case GST_JPEG_MARKER_DQT:
      if (priv->parser_state & GST_JPEG_VIDEO_STATE_GOT_SOF)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      break;
    case GST_JPEG_MARKER_DRI:
      if (priv->parser_state & GST_JPEG_VIDEO_STATE_GOT_SOS)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      break;
    case GST_JPEG_MARKER_DNL:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
      break;
    case GST_JPEG_MARKER_COM:
      flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
    default:
      /* SOFn segments */
      if (marker >= GST_JPEG_MARKER_SOF_MIN &&
          marker <= GST_JPEG_MARKER_SOF_MAX)
        priv->parser_state |= GST_JPEG_VIDEO_STATE_GOT_SOF;

      /* Application segments */
      else if (marker >= GST_JPEG_MARKER_APP_MIN &&
          marker <= GST_JPEG_MARKER_APP_MAX)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;

      /* Reserved */
      else if (marker >= 0x02 && marker <= 0xbf)
        flags |= GST_VAAPI_DECODER_UNIT_FLAG_SKIP;
      break;
  }
  GST_VAAPI_DECODER_UNIT_FLAG_SET (unit, flags);
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_decode (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * unit)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);
  GstVaapiDecoderStatus status;
  GstJpegSegment seg;
  GstBuffer *const buffer =
      GST_VAAPI_DECODER_CODEC_FRAME (decoder)->input_buffer;
  GstMapInfo map_info;

  status = ensure_decoder (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  if (!gst_buffer_map (buffer, &map_info, GST_MAP_READ)) {
    GST_ERROR ("failed to map buffer");
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
  }

  seg.marker = unit_get_marker_code (unit);
  seg.data = map_info.data;
  seg.offset = unit->offset;
  seg.size = unit->size;

  status = decode_segment (decoder, &seg);
  gst_buffer_unmap (buffer, &map_info);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_start_frame (GstVaapiDecoder * base_decoder,
    GstVaapiDecoderUnit * base_unit)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);
  GstVaapiDecoderJpegPrivate *const priv = &decoder->priv;
  GstVaapiPicture *picture;
  GstVaapiDecoderStatus status;

  if (!VALID_STATE (decoder, GOT_SOF))
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

  status = ensure_context (decoder);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
    GST_ERROR ("failed to reset context");
    return status;
  }

  picture = GST_VAAPI_PICTURE_NEW (JPEGBaseline, decoder);
  if (!picture) {
    GST_ERROR ("failed to allocate picture");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  gst_vaapi_picture_replace (&priv->current_picture, picture);
  gst_vaapi_picture_unref (picture);

  if (!fill_picture (decoder, picture, &priv->frame_hdr))
    return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

  status = fill_quantization_table (decoder, picture);
  if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
    return status;

  /* Update presentation time */
  picture->pts = GST_VAAPI_DECODER_CODEC_FRAME (decoder)->pts;
  return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_end_frame (GstVaapiDecoder * base_decoder)
{
  GstVaapiDecoderJpeg *const decoder =
      GST_VAAPI_DECODER_JPEG_CAST (base_decoder);

  return decode_current_picture (decoder);
}

static void
gst_vaapi_decoder_jpeg_finalize (GObject * object)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (object);

  gst_vaapi_decoder_jpeg_destroy (base_decoder);
  G_OBJECT_CLASS (gst_vaapi_decoder_jpeg_parent_class)->finalize (object);
}

static void
gst_vaapi_decoder_jpeg_class_init (GstVaapiDecoderJpegClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDecoderClass *const decoder_class = GST_VAAPI_DECODER_CLASS (klass);

  object_class->finalize = gst_vaapi_decoder_jpeg_finalize;

  decoder_class->reset = gst_vaapi_decoder_jpeg_reset;
  decoder_class->parse = gst_vaapi_decoder_jpeg_parse;
  decoder_class->decode = gst_vaapi_decoder_jpeg_decode;
  decoder_class->start_frame = gst_vaapi_decoder_jpeg_start_frame;
  decoder_class->end_frame = gst_vaapi_decoder_jpeg_end_frame;
}

static void
gst_vaapi_decoder_jpeg_init (GstVaapiDecoderJpeg * decoder)
{
  GstVaapiDecoder *const base_decoder = GST_VAAPI_DECODER (decoder);

  gst_vaapi_decoder_jpeg_create (base_decoder);
}

/**
 * gst_vaapi_decoder_jpeg_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for JPEG decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_jpeg_new (GstVaapiDisplay * display, GstCaps * caps)
{
  return g_object_new (GST_TYPE_VAAPI_DECODER_JPEG, "display", display,
      "caps", caps, NULL);
}
