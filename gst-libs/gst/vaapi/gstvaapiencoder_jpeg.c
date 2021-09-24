/*
 *  gstvaapiencoder_jpeg.c - JPEG encoder
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "sysdeps.h"
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gstjpegparser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiencoder_jpeg.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_NONE

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (NONE))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_RAW_DATA)

#define NUM_DC_RUN_SIZE_BITS 16
#define NUM_AC_RUN_SIZE_BITS 16
#define NUM_AC_CODE_WORDS_HUFFVAL 162
#define NUM_DC_CODE_WORDS_HUFFVAL 12

/* ------------------------------------------------------------------------- */
/* --- JPEG Encoder                                                      --- */
/* ------------------------------------------------------------------------- */

struct _GstVaapiEncoderJpeg
{
  GstVaapiEncoder parent_instance;
  GstVaapiProfile profile;
  guint quality;
  GstJpegQuantTables quant_tables;
  GstJpegQuantTables scaled_quant_tables;
  gboolean has_quant_tables;
  GstJpegHuffmanTables huff_tables;
  gboolean has_huff_tables;
  gint cwidth[GST_VIDEO_MAX_COMPONENTS];
  gint cheight[GST_VIDEO_MAX_COMPONENTS];
  gint h_samp[GST_VIDEO_MAX_COMPONENTS];
  gint v_samp[GST_VIDEO_MAX_COMPONENTS];
  gint h_max_samp;
  gint v_max_samp;
  guint n_components;
};

/* based on upstream gst-plugins-good jpegencoder */
static void
generate_sampling_factors (GstVaapiEncoderJpeg * encoder)
{
  GstVideoInfo *vinfo;
  gint i;

  vinfo = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);

  if (GST_VIDEO_INFO_FORMAT (vinfo) == GST_VIDEO_FORMAT_ENCODED) {
    /* Use native I420 format */
    encoder->n_components = 3;
    for (i = 0; i < encoder->n_components; ++i) {
      if (i == 0)
        encoder->h_samp[i] = encoder->v_samp[i] = 2;
      else
        encoder->h_samp[i] = encoder->v_samp[i] = 1;
      GST_DEBUG ("sampling factors: %d %d", encoder->h_samp[i],
          encoder->v_samp[i]);
    }
    return;
  }

  encoder->n_components = GST_VIDEO_INFO_N_COMPONENTS (vinfo);

  encoder->h_max_samp = 0;
  encoder->v_max_samp = 0;
  for (i = 0; i < encoder->n_components; ++i) {
    encoder->cwidth[i] = GST_VIDEO_INFO_COMP_WIDTH (vinfo, i);
    encoder->cheight[i] = GST_VIDEO_INFO_COMP_HEIGHT (vinfo, i);
    encoder->h_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (vinfo)) / encoder->cwidth[i];
    encoder->h_max_samp = MAX (encoder->h_max_samp, encoder->h_samp[i]);
    encoder->v_samp[i] =
        GST_ROUND_UP_4 (GST_VIDEO_INFO_HEIGHT (vinfo)) / encoder->cheight[i];
    encoder->v_max_samp = MAX (encoder->v_max_samp, encoder->v_samp[i]);
  }
  /* samp should only be 1, 2 or 4 */
  g_assert (encoder->h_max_samp <= 4);
  g_assert (encoder->v_max_samp <= 4);

  /* now invert */
  /* maximum is invariant, as one of the components should have samp 1 */
  for (i = 0; i < encoder->n_components; ++i) {
    encoder->h_samp[i] = encoder->h_max_samp / encoder->h_samp[i];
    encoder->v_samp[i] = encoder->v_max_samp / encoder->v_samp[i];
    GST_DEBUG ("sampling factors: %d %d", encoder->h_samp[i],
        encoder->v_samp[i]);
  }
}

/* Derives the profile that suits best to the configuration */
static GstVaapiEncoderStatus
ensure_profile (GstVaapiEncoderJpeg * encoder)
{
  /* Always start from "simple" profile for maximum compatibility */
  encoder->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiEncoderJpeg * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_PICTURE_ENCODE;
  GstVaapiProfile profile, profiles[2];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (i = 0; i < num_profiles; i++) {
    if (gst_vaapi_display_has_encoder (display, profiles[i], entrypoint)) {
      profile = profiles[i];
      break;
    }
  }
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    goto error_unsupported_profile;

  GST_VAAPI_ENCODER_CAST (encoder)->profile = profile;
  return TRUE;

  /* ERRORS */
error_unsupported_profile:
  {
    GST_ERROR ("unsupported HW profile %s",
        gst_vaapi_profile_get_va_name (encoder->profile));
    return FALSE;
  }
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderJpeg *encoder = GST_VAAPI_ENCODER_JPEG (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);

  /* Maximum sizes for common headers (in bytes) */
  enum
  {
    MAX_APP_HDR_SIZE = 20,
    MAX_FRAME_HDR_SIZE = 19,
    MAX_QUANT_TABLE_SIZE = 138,
    MAX_HUFFMAN_TABLE_SIZE = 432,
    MAX_SCAN_HDR_SIZE = 14
  };

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames = 0;

  /* Only YUV 4:2:0 formats are supported for now. */
  base_encoder->codedbuf_size = GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) * 3 / 2;

  base_encoder->codedbuf_size += MAX_APP_HDR_SIZE + MAX_FRAME_HDR_SIZE +
      MAX_QUANT_TABLE_SIZE + MAX_HUFFMAN_TABLE_SIZE + MAX_SCAN_HDR_SIZE;

  base_encoder->context_info.profile = base_encoder->profile;
  base_encoder->context_info.entrypoint = GST_VAAPI_ENTRYPOINT_PICTURE_ENCODE;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
fill_picture (GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  guint i;
  VAEncPictureParameterBufferJPEG *const pic_param = picture->param;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferJPEG));

  pic_param->reconstructed_picture =
      GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->picture_width = GST_VAAPI_ENCODER_WIDTH (encoder);
  pic_param->picture_height = GST_VAAPI_ENCODER_HEIGHT (encoder);
  pic_param->coded_buf = GST_VAAPI_CODED_BUFFER_ID (codedbuf);

  pic_param->pic_flags.bits.profile = 0;        /* Profile = Baseline */
  pic_param->pic_flags.bits.progressive = 0;    /* Sequential encoding */
  pic_param->pic_flags.bits.huffman = 1;        /* Uses Huffman coding */
  pic_param->pic_flags.bits.interleaved = 0;    /* Input format is non interleaved (YUV) */
  pic_param->pic_flags.bits.differential = 0;   /* non-Differential Encoding */
  pic_param->sample_bit_depth = 8;
  pic_param->num_scan = 1;
  pic_param->num_components = encoder->n_components;
  pic_param->quality = encoder->quality;
  for (i = 0; i < pic_param->num_components; i++) {
    pic_param->component_id[i] = i + 1;
    if (i != 0)
      pic_param->quantiser_table_selector[i] = 1;
  }
  return TRUE;
}

static gboolean
ensure_picture (GstVaapiEncoderJpeg * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  return TRUE;
}

/* This is a work-around: Normalize the quality factor and scale QM
 * values similar to what VA-Intel driver is doing. Otherwise the
 * generated packed headers will be wrong, since the driver itself
 * is scaling the QM values using the normalized quality factor */
static void
generate_scaled_qm (GstJpegQuantTables * quant_tables,
    GstJpegQuantTables * scaled_quant_tables, guint quality, guint shift)
{
  guint qt_val, nm_quality, i;
  nm_quality = quality == 0 ? 1 : quality;
  nm_quality =
      (nm_quality < 50) ? (5000 / nm_quality) : (200 - (nm_quality * 2));

  g_assert (quant_tables != NULL);
  g_assert (scaled_quant_tables != NULL);

  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    /* Luma QM */
    qt_val =
        (quant_tables->quant_tables[0].quant_table[i] * nm_quality +
        shift) / 100;
    scaled_quant_tables->quant_tables[0].quant_table[i] =
        CLAMP (qt_val, 1, 255);
    /* Chroma QM */
    qt_val =
        (quant_tables->quant_tables[1].quant_table[i] * nm_quality +
        shift) / 100;
    scaled_quant_tables->quant_tables[1].quant_table[i] =
        CLAMP (qt_val, 1, 255);
  }
}

static gboolean
fill_quantization_table (GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  VAQMatrixBufferJPEG *q_matrix;
  int i;

  g_assert (picture);

  picture->q_matrix = GST_VAAPI_ENC_Q_MATRIX_NEW (JPEG, encoder);
  if (!picture->q_matrix) {
    GST_ERROR ("failed to allocate quantiser table");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  q_matrix = picture->q_matrix->param;

  if (!encoder->has_quant_tables) {
    GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
    guint shift = 0;

    if (gst_vaapi_display_has_driver_quirks (display,
            GST_VAAPI_DRIVER_QUIRK_JPEG_ENC_SHIFT_VALUE_BY_50))
      shift = 50;

    gst_jpeg_get_default_quantization_tables (&encoder->quant_tables);
    encoder->has_quant_tables = TRUE;
    generate_scaled_qm (&encoder->quant_tables, &encoder->scaled_quant_tables,
        encoder->quality, shift);
  }
  q_matrix->load_lum_quantiser_matrix = 1;
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    q_matrix->lum_quantiser_matrix[i] =
        encoder->quant_tables.quant_tables[0].quant_table[i];
  }

  q_matrix->load_chroma_quantiser_matrix = 1;
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    q_matrix->chroma_quantiser_matrix[i] =
        encoder->quant_tables.quant_tables[1].quant_table[i];
  }

  return TRUE;
}

static gboolean
ensure_quantization_table (GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  g_assert (picture);

  if (!fill_quantization_table (encoder, picture))
    return FALSE;

  return TRUE;
}

static gboolean
fill_huffman_table (GstVaapiEncoderJpeg * encoder, GstVaapiEncPicture * picture)
{
  VAHuffmanTableBufferJPEGBaseline *huffman_table;
  guint i, num_tables;

  g_assert (picture);

  picture->huf_table = GST_VAAPI_ENC_HUFFMAN_TABLE_NEW (JPEGBaseline, encoder);
  if (!picture->huf_table) {
    GST_ERROR ("failed to allocate Huffman tables");
    return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
  huffman_table = picture->huf_table->param;

  num_tables = MIN (G_N_ELEMENTS (huffman_table->huffman_table),
      GST_JPEG_MAX_SCAN_COMPONENTS);

  if (!encoder->has_huff_tables) {
    gst_jpeg_get_default_huffman_tables (&encoder->huff_tables);
    encoder->has_huff_tables = TRUE;
  }

  for (i = 0; i < num_tables; i++) {
    huffman_table->load_huffman_table[i] =
        encoder->huff_tables.dc_tables[i].valid
        && encoder->huff_tables.ac_tables[i].valid;
    if (!huffman_table->load_huffman_table[i])
      continue;

    memcpy (huffman_table->huffman_table[i].num_dc_codes,
        encoder->huff_tables.dc_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_dc_codes));
    memcpy (huffman_table->huffman_table[i].dc_values,
        encoder->huff_tables.dc_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].dc_values));
    memcpy (huffman_table->huffman_table[i].num_ac_codes,
        encoder->huff_tables.ac_tables[i].huf_bits,
        sizeof (huffman_table->huffman_table[i].num_ac_codes));
    memcpy (huffman_table->huffman_table[i].ac_values,
        encoder->huff_tables.ac_tables[i].huf_values,
        sizeof (huffman_table->huffman_table[i].ac_values));
    memset (huffman_table->huffman_table[i].pad,
        0, sizeof (huffman_table->huffman_table[i].pad));
  }

  return TRUE;
}

static gboolean
ensure_huffman_table (GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  g_assert (picture);

  if (!fill_huffman_table (encoder, picture))
    return FALSE;

  return TRUE;
}

static gboolean
fill_slices (GstVaapiEncoderJpeg * encoder, GstVaapiEncPicture * picture)
{
  VAEncSliceParameterBufferJPEG *slice_param;
  GstVaapiEncSlice *slice;
  VAEncPictureParameterBufferJPEG *const pic_param = picture->param;

  slice = GST_VAAPI_ENC_SLICE_NEW (JPEG, encoder);
  g_assert (slice && slice->param_id != VA_INVALID_ID);
  slice_param = slice->param;

  memset (slice_param, 0, sizeof (VAEncSliceParameterBufferJPEG));

  slice_param->restart_interval = 0;
  slice_param->num_components = pic_param->num_components;

  slice_param->components[0].component_selector = 1;
  slice_param->components[0].dc_table_selector = 0;
  slice_param->components[0].ac_table_selector = 0;

  slice_param->components[1].component_selector = 2;
  slice_param->components[1].dc_table_selector = 1;
  slice_param->components[1].ac_table_selector = 1;

  slice_param->components[2].component_selector = 3;
  slice_param->components[2].dc_table_selector = 1;
  slice_param->components[2].ac_table_selector = 1;

  gst_vaapi_enc_picture_add_slice (picture, slice);
  gst_vaapi_codec_object_replace (&slice, NULL);

  return TRUE;
}

static gboolean
ensure_slices (GstVaapiEncoderJpeg * encoder, GstVaapiEncPicture * picture)
{
  g_assert (picture);

  if (!fill_slices (encoder, picture))
    return FALSE;

  return TRUE;
}

static void
generate_frame_hdr (GstJpegFrameHdr * frame_hdr, GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  VAEncPictureParameterBufferJPEG *const pic_param = picture->param;
  guint i;

  memset (frame_hdr, 0, sizeof (GstJpegFrameHdr));
  frame_hdr->sample_precision = 8;
  frame_hdr->width = pic_param->picture_width;
  frame_hdr->height = pic_param->picture_height;
  frame_hdr->num_components = pic_param->num_components;

  for (i = 0; i < frame_hdr->num_components; i++) {
    frame_hdr->components[i].identifier = pic_param->component_id[i];
    frame_hdr->components[i].horizontal_factor = encoder->h_samp[i];
    frame_hdr->components[i].vertical_factor = encoder->v_samp[i];
    frame_hdr->components[i].quant_table_selector =
        pic_param->quantiser_table_selector[i];
  }
}

static void
generate_scan_hdr (GstJpegScanHdr * scan_hdr, GstVaapiEncPicture * picture)
{

  VAEncPictureParameterBufferJPEG *const pic_param = picture->param;

  memset (scan_hdr, 0, sizeof (GstJpegScanHdr));
  scan_hdr->num_components = pic_param->num_components;
  //Y Component
  scan_hdr->components[0].component_selector = 1;
  scan_hdr->components[0].dc_selector = 0;
  scan_hdr->components[0].ac_selector = 0;


  //U Component
  scan_hdr->components[1].component_selector = 2;
  scan_hdr->components[1].dc_selector = 1;
  scan_hdr->components[1].ac_selector = 1;

  //V Component
  scan_hdr->components[2].component_selector = 3;
  scan_hdr->components[2].dc_selector = 1;
  scan_hdr->components[2].ac_selector = 1;
}

static gboolean
bs_write_jpeg_header (GstBitWriter * bs, GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  GstJpegFrameHdr frame_hdr;
  GstJpegScanHdr scan_hdr;
  guint i, j;

  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_SOI, 8);
  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_APP_MIN, 8);
  gst_bit_writer_put_bits_uint16 (bs, 16, 16);
  gst_bit_writer_put_bits_uint8 (bs, 0x4A, 8);  //J
  gst_bit_writer_put_bits_uint8 (bs, 0x46, 8);  //F
  gst_bit_writer_put_bits_uint8 (bs, 0x49, 8);  //I
  gst_bit_writer_put_bits_uint8 (bs, 0x46, 8);  //F
  gst_bit_writer_put_bits_uint8 (bs, 0x00, 8);  //0
  gst_bit_writer_put_bits_uint8 (bs, 1, 8);     //Major Version
  gst_bit_writer_put_bits_uint8 (bs, 1, 8);     //Minor Version
  gst_bit_writer_put_bits_uint8 (bs, 0, 8);     //Density units 0:no units, 1:pixels per inch, 2: pixels per cm
  gst_bit_writer_put_bits_uint16 (bs, 1, 16);   //X density (pixel-aspect-ratio)
  gst_bit_writer_put_bits_uint16 (bs, 1, 16);   //Y density (pixel-aspect-ratio)
  gst_bit_writer_put_bits_uint8 (bs, 0, 8);     //Thumbnail width
  gst_bit_writer_put_bits_uint8 (bs, 0, 8);     //Thumbnail height

  /* Add  quantization table */
  if (!encoder->has_quant_tables) {
    GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
    guint shift = 0;

    if (gst_vaapi_display_has_driver_quirks (display,
            GST_VAAPI_DRIVER_QUIRK_JPEG_ENC_SHIFT_VALUE_BY_50))
      shift = 50;

    gst_jpeg_get_default_quantization_tables (&encoder->quant_tables);
    generate_scaled_qm (&encoder->quant_tables, &encoder->scaled_quant_tables,
        encoder->quality, shift);
    encoder->has_quant_tables = TRUE;
  }

  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_DQT, 8);
  gst_bit_writer_put_bits_uint16 (bs, 3 + GST_JPEG_MAX_QUANT_ELEMENTS, 16);     //Lq
  gst_bit_writer_put_bits_uint8 (bs, encoder->quant_tables.quant_tables[0].quant_precision, 4); //Pq
  gst_bit_writer_put_bits_uint8 (bs, 0, 4);     //Tq
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    gst_bit_writer_put_bits_uint16 (bs,
        encoder->scaled_quant_tables.quant_tables[0].quant_table[i], 8);
  }
  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_DQT, 8);
  gst_bit_writer_put_bits_uint16 (bs, 3 + GST_JPEG_MAX_QUANT_ELEMENTS, 16);     //Lq
  gst_bit_writer_put_bits_uint8 (bs, encoder->quant_tables.quant_tables[1].quant_precision, 4); //Pq
  gst_bit_writer_put_bits_uint8 (bs, 1, 4);     //Tq
  for (i = 0; i < GST_JPEG_MAX_QUANT_ELEMENTS; i++) {
    gst_bit_writer_put_bits_uint16 (bs,
        encoder->scaled_quant_tables.quant_tables[1].quant_table[i], 8);
  }

  /*Add frame header */
  generate_frame_hdr (&frame_hdr, encoder, picture);
  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_SOF_MIN, 8);
  gst_bit_writer_put_bits_uint16 (bs, 8 + (3 * 3), 16); //lf, Size of FrameHeader in bytes without the Marker SOF
  gst_bit_writer_put_bits_uint8 (bs, frame_hdr.sample_precision, 8);
  gst_bit_writer_put_bits_uint16 (bs, frame_hdr.height, 16);
  gst_bit_writer_put_bits_uint16 (bs, frame_hdr.width, 16);
  gst_bit_writer_put_bits_uint8 (bs, frame_hdr.num_components, 8);
  for (i = 0; i < frame_hdr.num_components; i++) {
    gst_bit_writer_put_bits_uint8 (bs, frame_hdr.components[i].identifier, 8);
    gst_bit_writer_put_bits_uint8 (bs,
        frame_hdr.components[i].horizontal_factor, 4);
    gst_bit_writer_put_bits_uint8 (bs, frame_hdr.components[i].vertical_factor,
        4);
    gst_bit_writer_put_bits_uint8 (bs,
        frame_hdr.components[i].quant_table_selector, 8);
  }

  /* Add Huffman table */
  if (!encoder->has_huff_tables) {
    gst_jpeg_get_default_huffman_tables (&encoder->huff_tables);
    encoder->has_huff_tables = TRUE;
  }
  for (i = 0; i < 2; i++) {
    gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
    gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_DHT, 8);
    gst_bit_writer_put_bits_uint16 (bs, 0x1F, 16);      //length of table
    gst_bit_writer_put_bits_uint8 (bs, 0, 4);
    gst_bit_writer_put_bits_uint8 (bs, i, 4);
    for (j = 0; j < NUM_DC_RUN_SIZE_BITS; j++) {
      gst_bit_writer_put_bits_uint8 (bs,
          encoder->huff_tables.dc_tables[i].huf_bits[j], 8);
    }

    for (j = 0; j < NUM_DC_CODE_WORDS_HUFFVAL; j++) {
      gst_bit_writer_put_bits_uint8 (bs,
          encoder->huff_tables.dc_tables[i].huf_values[j], 8);
    }

    gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
    gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_DHT, 8);
    gst_bit_writer_put_bits_uint16 (bs, 0xB5, 16);      //length of table
    gst_bit_writer_put_bits_uint8 (bs, 1, 4);
    gst_bit_writer_put_bits_uint8 (bs, i, 4);
    for (j = 0; j < NUM_AC_RUN_SIZE_BITS; j++) {
      gst_bit_writer_put_bits_uint8 (bs,
          encoder->huff_tables.ac_tables[i].huf_bits[j], 8);
    }

    for (j = 0; j < NUM_AC_CODE_WORDS_HUFFVAL; j++) {
      gst_bit_writer_put_bits_uint8 (bs,
          encoder->huff_tables.ac_tables[i].huf_values[j], 8);
    }
  }

  /* Add ScanHeader */
  generate_scan_hdr (&scan_hdr, picture);
  gst_bit_writer_put_bits_uint8 (bs, 0xFF, 8);
  gst_bit_writer_put_bits_uint8 (bs, GST_JPEG_MARKER_SOS, 8);
  gst_bit_writer_put_bits_uint16 (bs, 12, 16);  //Length of Scan
  gst_bit_writer_put_bits_uint8 (bs, scan_hdr.num_components, 8);

  for (i = 0; i < scan_hdr.num_components; i++) {
    gst_bit_writer_put_bits_uint8 (bs,
        scan_hdr.components[i].component_selector, 8);
    gst_bit_writer_put_bits_uint8 (bs, scan_hdr.components[i].dc_selector, 4);
    gst_bit_writer_put_bits_uint8 (bs, scan_hdr.components[i].ac_selector, 4);
  }
  gst_bit_writer_put_bits_uint8 (bs, 0, 8);     //0 for Baseline
  gst_bit_writer_put_bits_uint8 (bs, 63, 8);    //63 for Baseline
  gst_bit_writer_put_bits_uint8 (bs, 0, 4);     //0 for Baseline
  gst_bit_writer_put_bits_uint8 (bs, 0, 4);     //0 for Baseline

  return TRUE;
}

static gboolean
add_packed_header (GstVaapiEncoderJpeg * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncPackedHeader *packed_raw_data_hdr;
  GstBitWriter bs;
  VAEncPackedHeaderParameterBuffer packed_raw_data_hdr_param = { 0 };
  guint32 data_bit_size;
  guint8 *data;

  gst_bit_writer_init_with_size (&bs, 128, FALSE);
  bs_write_jpeg_header (&bs, encoder, picture);
  data_bit_size = GST_BIT_WRITER_BIT_SIZE (&bs);
  data = GST_BIT_WRITER_DATA (&bs);

  packed_raw_data_hdr_param.type = VAEncPackedHeaderRawData;
  packed_raw_data_hdr_param.bit_length = data_bit_size;
  packed_raw_data_hdr_param.has_emulation_bytes = 0;

  packed_raw_data_hdr =
      gst_vaapi_enc_packed_header_new (GST_VAAPI_ENCODER (encoder),
      &packed_raw_data_hdr_param, sizeof (packed_raw_data_hdr_param), data,
      (data_bit_size + 7) / 8);
  g_assert (packed_raw_data_hdr);

  gst_vaapi_enc_picture_add_packed_header (picture, packed_raw_data_hdr);
  gst_vaapi_codec_object_replace (&packed_raw_data_hdr, NULL);

  gst_bit_writer_reset (&bs);

  return TRUE;
}

static gboolean
ensure_packed_headers (GstVaapiEncoderJpeg * encoder,
    GstVaapiEncPicture * picture)
{
  g_assert (picture);

  if ((GST_VAAPI_ENCODER_PACKED_HEADERS (encoder) &
          VA_ENC_PACKED_HEADER_RAW_DATA)
      && !add_packed_header (encoder, picture))
    goto error_create_packed_hdr;

  return TRUE;

  /* ERRORS */
error_create_packed_hdr:
  {
    GST_ERROR ("failed to create packed raw data header buffer");
    return FALSE;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_jpeg_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderJpeg *const encoder = GST_VAAPI_ENCODER_JPEG (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!ensure_quantization_table (encoder, picture))
    goto error;
  if (!ensure_huffman_table (encoder, picture))
    goto error;
  if (!ensure_slices (encoder, picture))
    goto error;
  if (!ensure_packed_headers (encoder, picture))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;
  if (reconstruct)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
        reconstruct);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error:
  {
    if (reconstruct)
      gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
          reconstruct);
    return ret;
  }
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_jpeg_flush (GstVaapiEncoder * base_encoder)
{
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_jpeg_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderJpeg *const encoder = GST_VAAPI_ENCODER_JPEG (base_encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  if (!frame)
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

  picture = GST_VAAPI_ENC_PICTURE_NEW (JPEG, encoder, frame);
  if (!picture) {
    GST_WARNING ("create JPEG picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  *output = picture;
  return status;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_jpeg_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderJpeg *const encoder = GST_VAAPI_ENCODER_JPEG (base_encoder);
  GstVaapiEncoderStatus status;

  status = ensure_profile (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  /* generate sampling factors (A.1.1) */
  generate_sampling_factors (encoder);

  return set_context_info (base_encoder);
}

struct _GstVaapiEncoderJpegClass
{
  GstVaapiEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVaapiEncoderJpeg, gst_vaapi_encoder_jpeg,
    GST_TYPE_VAAPI_ENCODER);

static void
gst_vaapi_encoder_jpeg_init (GstVaapiEncoderJpeg * encoder)
{
  encoder->has_quant_tables = FALSE;
  memset (&encoder->quant_tables, 0, sizeof (encoder->quant_tables));
  memset (&encoder->scaled_quant_tables, 0,
      sizeof (encoder->scaled_quant_tables));
  encoder->has_huff_tables = FALSE;
  memset (&encoder->huff_tables, 0, sizeof (encoder->huff_tables));
}

/**
 * @ENCODER_JPEG_PROP_RATECONTROL: Rate control (#GstVaapiRateControl).
 * @ENCODER_JPEG_PROP_TUNE: The tuning options (#GstVaapiEncoderTune).
 * @ENCODER_JPEG_PROP_QUALITY: Quality Factor value (uint).
 *
 * The set of JPEG encoder specific configurable properties.
 */
enum
{
  ENCODER_JPEG_PROP_RATECONTROL = 1,
  ENCODER_JPEG_PROP_TUNE,
  ENCODER_JPEG_PROP_QUALITY,
  ENCODER_JPEG_N_PROPERTIES
};

static GParamSpec *properties[ENCODER_JPEG_N_PROPERTIES];

static void
gst_vaapi_encoder_jpeg_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);
  GstVaapiEncoderJpeg *const encoder = GST_VAAPI_ENCODER_JPEG (object);

  if (base_encoder->num_codedbuf_queued > 0) {
    GST_ERROR_OBJECT (object,
        "failed to set any property after encoding started");
    return;
  }

  switch (prop_id) {
    case ENCODER_JPEG_PROP_RATECONTROL:
      gst_vaapi_encoder_set_rate_control (base_encoder,
          g_value_get_enum (value));
      break;
    case ENCODER_JPEG_PROP_TUNE:
      gst_vaapi_encoder_set_tuning (base_encoder, g_value_get_enum (value));
      break;
    case ENCODER_JPEG_PROP_QUALITY:
      encoder->quality = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_vaapi_encoder_jpeg_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncoderJpeg *const encoder = GST_VAAPI_ENCODER_JPEG (object);
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER (object);

  switch (prop_id) {
    case ENCODER_JPEG_PROP_RATECONTROL:
      g_value_set_enum (value, base_encoder->rate_control);
      break;
    case ENCODER_JPEG_PROP_TUNE:
      g_value_set_enum (value, base_encoder->tune);
      break;
    case ENCODER_JPEG_PROP_QUALITY:
      g_value_set_uint (value, encoder->quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (JPEG);

static void
gst_vaapi_encoder_jpeg_class_init (GstVaapiEncoderJpegClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiEncoderClass *const encoder_class = GST_VAAPI_ENCODER_CLASS (klass);

  encoder_class->class_data = &g_class_data;
  encoder_class->reconfigure = gst_vaapi_encoder_jpeg_reconfigure;
  encoder_class->reordering = gst_vaapi_encoder_jpeg_reordering;
  encoder_class->encode = gst_vaapi_encoder_jpeg_encode;
  encoder_class->flush = gst_vaapi_encoder_jpeg_flush;

  object_class->set_property = gst_vaapi_encoder_jpeg_set_property;
  object_class->get_property = gst_vaapi_encoder_jpeg_get_property;

  properties[ENCODER_JPEG_PROP_RATECONTROL] =
      g_param_spec_enum ("rate-control",
      "Rate Control", "Rate control mode",
      g_class_data.rate_control_get_type (),
      g_class_data.default_rate_control,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_JPEG_PROP_TUNE] =
      g_param_spec_enum ("tune",
      "Encoder Tuning",
      "Encoder tuning option",
      g_class_data.encoder_tune_get_type (),
      g_class_data.default_encoder_tune,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  properties[ENCODER_JPEG_PROP_QUALITY] =
      g_param_spec_uint ("quality",
      "Quality factor",
      "Quality factor", 0, 100, 50,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT |
      GST_VAAPI_PARAM_ENCODER_EXPOSURE);

  g_object_class_install_properties (object_class, ENCODER_JPEG_N_PROPERTIES,
      properties);

  gst_type_mark_as_plugin_api (g_class_data.rate_control_get_type (), 0);
  gst_type_mark_as_plugin_api (g_class_data.encoder_tune_get_type (), 0);
}

/**
 * gst_vaapi_encoder_jpeg_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for JPEG encoding.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_jpeg_new (GstVaapiDisplay * display)
{
  return g_object_new (GST_TYPE_VAAPI_ENCODER_JPEG, "display", display, NULL);
}
