/*
 *  gstvaapidecoder_jpeg.c - JPEG decoder
 *
 *  Copyright (C) 2011-2013 Intel Corporation
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
#include <string.h>
#include <gst/codecparsers/gstjpegparser.h>
#include "gstvaapicompat.h"
#include "gstvaapidecoder_jpeg.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderJpeg,
              gst_vaapi_decoder_jpeg,
              GST_VAAPI_TYPE_DECODER)

#define GST_VAAPI_DECODER_JPEG_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_JPEG,   \
                                 GstVaapiDecoderJpegPrivate))

struct _GstVaapiDecoderJpegPrivate {
    GstVaapiProfile             profile;
    guint                       width;
    guint                       height;
    GstVaapiPicture            *current_picture;
    GstJpegFrameHdr             frame_hdr;
    GstJpegHuffmanTables        huf_tables;
    GstJpegQuantTables          quant_tables;
    gboolean                    has_huf_table;
    gboolean                    has_quant_table;
    guint                       mcu_restart;
    guint                       is_opened       : 1;
    guint                       profile_changed : 1;
    guint                       is_constructed  : 1;
};

typedef struct _GstJpegScanSegment GstJpegScanSegment;
struct _GstJpegScanSegment {
    guint                       header_offset;
    guint                       header_size;
    guint                       data_offset;
    guint                       data_size;
    guint                       is_valid        : 1;
};

static void
gst_vaapi_decoder_jpeg_close(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);

    /* Reset all */
    priv->profile               = GST_VAAPI_PROFILE_JPEG_BASELINE;
    priv->width                 = 0;
    priv->height                = 0;
    priv->is_opened             = FALSE;
    priv->profile_changed       = TRUE;
}

static gboolean
gst_vaapi_decoder_jpeg_open(GstVaapiDecoderJpeg *decoder)
{
    gst_vaapi_decoder_jpeg_close(decoder);

    return TRUE;
}

static void
gst_vaapi_decoder_jpeg_destroy(GstVaapiDecoderJpeg *decoder)
{
    gst_vaapi_decoder_jpeg_close(decoder);
}

static gboolean
gst_vaapi_decoder_jpeg_create(GstVaapiDecoderJpeg *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiProfile profiles[2];
    GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
    guint i, n_profiles = 0;
    gboolean reset_context = FALSE;

    if (priv->profile_changed) {
        GST_DEBUG("profile changed");
        priv->profile_changed = FALSE;
        reset_context         = TRUE;

        profiles[n_profiles++] = priv->profile;
        //if (priv->profile == GST_VAAPI_PROFILE_JPEG_EXTENDED)
        //    profiles[n_profiles++] = GST_VAAPI_PROFILE_JPEG_BASELINE;

        for (i = 0; i < n_profiles; i++) {
            if (gst_vaapi_display_has_decoder(GST_VAAPI_DECODER_DISPLAY(decoder),
                                              profiles[i], entrypoint))
                break;
        }
        if (i == n_profiles)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
        priv->profile = profiles[i];
    }

    if (reset_context) {
        GstVaapiContextInfo info;

        info.profile    = priv->profile;
        info.entrypoint = entrypoint;
        info.width      = priv->width;
        info.height     = priv->height;
        info.ref_frames = 2;
        reset_context   = gst_vaapi_decoder_ensure_context(
            GST_VAAPI_DECODER(decoder),
            &info
        );
        if (!reset_context)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
decode_current_picture(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->current_picture;
    gboolean success = TRUE;

    if (picture) {
        if (!gst_vaapi_picture_decode(picture))
            success = FALSE;
        else if (!gst_vaapi_picture_output(picture))
            success = FALSE;
        gst_vaapi_picture_replace(&priv->current_picture, NULL);
    }
    return success;
}

static gboolean
fill_picture(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture,
    GstJpegFrameHdr     *jpeg_frame_hdr
)
{
    VAPictureParameterBufferJPEGBaseline *pic_param = picture->param;
    guint i;

    g_assert(pic_param);

    memset(pic_param, 0, sizeof(VAPictureParameterBufferJPEGBaseline));
    pic_param->picture_width    = jpeg_frame_hdr->width;
    pic_param->picture_height   = jpeg_frame_hdr->height;

    pic_param->num_components   = jpeg_frame_hdr->num_components;
    if (jpeg_frame_hdr->num_components > 4)
        return FALSE;
    for (i = 0; i < pic_param->num_components; i++) {
        pic_param->components[i].component_id =
            jpeg_frame_hdr->components[i].identifier;
        pic_param->components[i].h_sampling_factor =
            jpeg_frame_hdr->components[i].horizontal_factor;
        pic_param->components[i].v_sampling_factor =
            jpeg_frame_hdr->components[i].vertical_factor;
        pic_param->components[i].quantiser_table_selector =
            jpeg_frame_hdr->components[i].quant_table_selector;
    }
    return TRUE;
}

static gboolean
fill_quantization_table(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    VAIQMatrixBufferJPEGBaseline *iq_matrix;
    guint i, j, num_tables;

    if (!priv->has_quant_table)
        gst_jpeg_get_default_quantization_tables(&priv->quant_tables);
    
    picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(JPEGBaseline, decoder);
    g_assert(picture->iq_matrix);
    iq_matrix = picture->iq_matrix->param;

    num_tables = MIN(G_N_ELEMENTS(iq_matrix->quantiser_table),
                     GST_JPEG_MAX_QUANT_ELEMENTS);

    for (i = 0; i < num_tables; i++) {
        GstJpegQuantTable * const quant_table =
            &priv->quant_tables.quant_tables[i];

        iq_matrix->load_quantiser_table[i] = quant_table->valid;
        if (!iq_matrix->load_quantiser_table[i])
            continue;

        g_assert(quant_table->quant_precision == 0);
        for (j = 0; j < GST_JPEG_MAX_QUANT_ELEMENTS; j++)
            iq_matrix->quantiser_table[i][j] = quant_table->quant_table[j];
        iq_matrix->load_quantiser_table[i] = 1;
        quant_table->valid = FALSE;
    }
    return TRUE;
}

static gboolean
fill_huffman_table(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstJpegHuffmanTables * const huf_tables = &priv->huf_tables;
    VAHuffmanTableBufferJPEGBaseline *huffman_table;
    guint i, num_tables;

    if (!priv->has_huf_table)
        gst_jpeg_get_default_huffman_tables(&priv->huf_tables);
    
    picture->huf_table = GST_VAAPI_HUFFMAN_TABLE_NEW(JPEGBaseline, decoder);
    g_assert(picture->huf_table);
    huffman_table = picture->huf_table->param;

    num_tables = MIN(G_N_ELEMENTS(huffman_table->huffman_table),
                     GST_JPEG_MAX_SCAN_COMPONENTS);

    for (i = 0; i < num_tables; i++) {
        huffman_table->load_huffman_table[i] =
            huf_tables->dc_tables[i].valid && huf_tables->ac_tables[i].valid;
        if (!huffman_table->load_huffman_table[i])
            continue;

        memcpy(huffman_table->huffman_table[i].num_dc_codes,
               huf_tables->dc_tables[i].huf_bits,
               sizeof(huffman_table->huffman_table[i].num_dc_codes));
        memcpy(huffman_table->huffman_table[i].dc_values,
               huf_tables->dc_tables[i].huf_values,
               sizeof(huffman_table->huffman_table[i].dc_values));
        memcpy(huffman_table->huffman_table[i].num_ac_codes,
               huf_tables->ac_tables[i].huf_bits,
               sizeof(huffman_table->huffman_table[i].num_ac_codes));
        memcpy(huffman_table->huffman_table[i].ac_values,
               huf_tables->ac_tables[i].huf_values,
               sizeof(huffman_table->huffman_table[i].ac_values));
        memset(huffman_table->huffman_table[i].pad,
               0,
               sizeof(huffman_table->huffman_table[i].pad));
    }
    return TRUE;
}

static guint
get_max_horizontal_samples(GstJpegFrameHdr *frame_hdr)
{
    guint i, max_factor = 0;

    for (i = 0; i < frame_hdr->num_components; i++) {
        if (frame_hdr->components[i].horizontal_factor > max_factor)
            max_factor = frame_hdr->components[i].horizontal_factor;
    }
    return max_factor;
}

static guint
get_max_vertical_samples(GstJpegFrameHdr *frame_hdr)
{
    guint i, max_factor = 0;

    for (i = 0; i < frame_hdr->num_components; i++) {
        if (frame_hdr->components[i].vertical_factor > max_factor)
            max_factor = frame_hdr->components[i].vertical_factor;
    }
    return max_factor;
}

static GstVaapiDecoderStatus
decode_picture(
    GstVaapiDecoderJpeg *decoder, 
    guint8               profile,
    const guchar        *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstJpegFrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVaapiPicture *picture;
    GstVaapiDecoderStatus status;

    switch (profile) {
    case GST_JPEG_MARKER_SOF_MIN:
        priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
        break;
    default:
        GST_ERROR("unsupported profile %d", profile);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    memset(frame_hdr, 0, sizeof(*frame_hdr));
    if (!gst_jpeg_parse_frame_hdr(frame_hdr, buf, buf_size, 0)) {
        GST_ERROR("failed to parse image");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->height = frame_hdr->height;
    priv->width  = frame_hdr->width;

    status = ensure_context(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset context");
        return status;
    }

    if (priv->current_picture && !decode_current_picture(decoder))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    picture = GST_VAAPI_PICTURE_NEW(JPEGBaseline, decoder);
    if (!picture) {
        GST_ERROR("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_replace(&priv->current_picture, picture);
    gst_vaapi_picture_unref(picture);

    if (!fill_picture(decoder, picture, frame_hdr))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    /* Update presentation time */
    picture->pts = GST_VAAPI_DECODER_CODEC_FRAME(decoder)->pts;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_huffman_table(
    GstVaapiDecoderJpeg *decoder,
    const guchar        *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    if (!gst_jpeg_parse_huffman_table(&priv->huf_tables, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse Huffman table");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_huf_table = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_quant_table(
    GstVaapiDecoderJpeg *decoder,
    const guchar        *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    if (!gst_jpeg_parse_quant_table(&priv->quant_tables, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse quantization table");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_quant_table = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_restart_interval(
    GstVaapiDecoderJpeg *decoder,
    const guchar        *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    if (!gst_jpeg_parse_restart_interval(&priv->mcu_restart, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse restart interval");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_scan(
    GstVaapiDecoderJpeg *decoder,
    const guchar        *scan_header,
    guint                scan_header_size,
    const guchar        *scan_data,
    guint                scan_data_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture *picture = priv->current_picture;
    VASliceParameterBufferJPEGBaseline *slice_param;
    GstVaapiSlice *gst_slice;
    guint total_h_samples, total_v_samples;
    GstJpegScanHdr  scan_hdr;
    guint i;

    if (!picture) {
        GST_ERROR("There is no VAPicture before decoding scan.");
        return GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE;
    }

    if (!fill_quantization_table(decoder, picture)) {
        GST_ERROR("failed to fill in quantization table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!fill_huffman_table(decoder, picture)) {
        GST_ERROR("failed to fill in huffman table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    memset(&scan_hdr, 0, sizeof(scan_hdr));
    if (!gst_jpeg_parse_scan_hdr(&scan_hdr, scan_header, scan_header_size, 0)) {
        GST_DEBUG("Jpeg parsed scan failed.");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    gst_slice = GST_VAAPI_SLICE_NEW(JPEGBaseline, decoder, scan_data, scan_data_size);
    gst_vaapi_picture_add_slice(picture, gst_slice);

    slice_param = gst_slice->param;
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
    if (scan_hdr.num_components == 1) { /*non-interleaved*/
        slice_param->slice_horizontal_position = 0;
        slice_param->slice_vertical_position = 0;
        /* Y mcu numbers*/
        if (slice_param->components[0].component_selector == priv->frame_hdr.components[0].identifier) {
            slice_param->num_mcus = (priv->frame_hdr.width/8)*(priv->frame_hdr.height/8);
        } else { /*Cr, Cb mcu numbers*/
            slice_param->num_mcus = (priv->frame_hdr.width/16)*(priv->frame_hdr.height/16);
        }
    } else { /* interleaved */
        slice_param->slice_horizontal_position = 0;
        slice_param->slice_vertical_position = 0;
        total_v_samples = get_max_vertical_samples(&priv->frame_hdr);
        total_h_samples = get_max_horizontal_samples(&priv->frame_hdr);
        slice_param->num_mcus = ((priv->frame_hdr.width + total_h_samples*8 - 1)/(total_h_samples*8)) *
                                ((priv->frame_hdr.height + total_v_samples*8 -1)/(total_v_samples*8));
    }

    if (picture->slices && picture->slices->len)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;
    return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderJpeg *decoder, const guchar *buf, guint buf_size)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    GstJpegMarkerSegment seg;
    GstJpegScanSegment scan_seg;
    guint ofs;
    gboolean append_ecs;

    memset(&scan_seg, 0, sizeof(scan_seg));

    ofs = 0;
    while (gst_jpeg_parse(&seg, buf, buf_size, ofs)) {
        if (seg.size < 0) {
            GST_DEBUG("buffer to short for parsing");
            return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        }
        ofs += seg.size;

        /* Decode scan, if complete */
        if (seg.marker == GST_JPEG_MARKER_EOI && scan_seg.header_size > 0) {
            scan_seg.data_size = seg.offset - scan_seg.data_offset;
            scan_seg.is_valid  = TRUE;
        }
        if (scan_seg.is_valid) {
            status = decode_scan(
                decoder,
                buf + scan_seg.header_offset,
                scan_seg.header_size,
                buf + scan_seg.data_offset,
                scan_seg.data_size
            );
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                break;
            memset(&scan_seg, 0, sizeof(scan_seg));
        }

        append_ecs = TRUE;
        switch (seg.marker) {
        case GST_JPEG_MARKER_SOI:
            priv->has_quant_table = FALSE;
            priv->has_huf_table   = FALSE;
            priv->mcu_restart     = 0;
            status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            break;
        case GST_JPEG_MARKER_EOI:
            if (decode_current_picture(decoder)) {
                /* Get out of the loop, trailing data is not needed */
                status = GST_VAAPI_DECODER_STATUS_SUCCESS;
                goto end;
            }
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
            break;
        case GST_JPEG_MARKER_DHT:
            status = decode_huffman_table(decoder, buf + seg.offset, seg.size);
            break;
        case GST_JPEG_MARKER_DQT:
            status = decode_quant_table(decoder, buf + seg.offset, seg.size);
            break;
        case GST_JPEG_MARKER_DRI:
            status = decode_restart_interval(decoder, buf + seg.offset, seg.size);
            break;
        case GST_JPEG_MARKER_DAC:
            GST_ERROR("unsupported arithmetic coding mode");
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
            break;
        case GST_JPEG_MARKER_SOS:
            scan_seg.header_offset = seg.offset;
            scan_seg.header_size   = seg.size;
            scan_seg.data_offset   = seg.offset + seg.size;
            scan_seg.data_size     = 0;
            append_ecs             = FALSE;
            break;
        default:
            /* Restart marker */
            if (seg.marker >= GST_JPEG_MARKER_RST_MIN &&
                seg.marker <= GST_JPEG_MARKER_RST_MAX) {
                append_ecs = FALSE;
                break;
            }

            /* Frame header */
            if (seg.marker >= GST_JPEG_MARKER_SOF_MIN &&
                seg.marker <= GST_JPEG_MARKER_SOF_MAX) {
                status = decode_picture(
                    decoder,
                    seg.marker,
                    buf + seg.offset, seg.size
                );
                break;
            }

            /* Application segments */
            if (seg.marker >= GST_JPEG_MARKER_APP_MIN &&
                seg.marker <= GST_JPEG_MARKER_APP_MAX) {
                status = GST_VAAPI_DECODER_STATUS_SUCCESS;
                break;
            }

            GST_WARNING("unsupported marker (0x%02x)", seg.marker);
            status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
            break;
        }

        /* Append entropy coded segments */
        if (append_ecs)
            scan_seg.data_size = seg.offset - scan_seg.data_offset;

        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            break;
    }
end:
    return status;
}

static GstVaapiDecoderStatus
ensure_decoder(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_jpeg_open(decoder);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline gint
scan_for_start_code(GstAdapter *adapter, guint ofs, guint size, guint32 *scp)
{
    return (gint)gst_adapter_masked_scan_uint32_peek(adapter,
        0xffff0000, 0xffd80000, ofs, size, scp);
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_parse(GstVaapiDecoder *base_decoder,
    GstAdapter *adapter, gboolean at_eos, GstVaapiDecoderUnit *unit)
{
    GstVaapiDecoderJpeg * const decoder = GST_VAAPI_DECODER_JPEG(base_decoder);
    GstVaapiDecoderStatus status;
    guint size, buf_size, flags = 0;
    gint ofs;

    status = ensure_decoder(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    /* Expect at least 4 bytes, SOI .. EOI */
    size = gst_adapter_available(adapter);
    if (size < 4)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    ofs = scan_for_start_code(adapter, 0, size, NULL);
    if (ofs < 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    gst_adapter_flush(adapter, ofs);
    size -= ofs;

    ofs = G_UNLIKELY(size < 4) ? -1 :
        scan_for_start_code(adapter, 2, size - 2, NULL);
    if (ofs < 0) {
        // Assume the whole packet is present if end-of-stream
        if (!at_eos)
            return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        ofs = size;
    }
    buf_size = ofs;

    unit->size = buf_size;

    flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_START;
    flags |= GST_VAAPI_DECODER_UNIT_FLAG_FRAME_END;
    flags |= GST_VAAPI_DECODER_UNIT_FLAG_SLICE;
    GST_VAAPI_DECODER_UNIT_FLAG_SET(unit, flags);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_decode(GstVaapiDecoder *base_decoder,
    GstVaapiDecoderUnit *unit)
{
    GstVaapiDecoderJpeg * const decoder = GST_VAAPI_DECODER_JPEG(base_decoder);
    GstVaapiDecoderStatus status;
    GstBuffer * const buffer =
        GST_VAAPI_DECODER_CODEC_FRAME(decoder)->input_buffer;
    const guchar *buf;
    guint buf_size;

    status = ensure_decoder(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    buf      = GST_BUFFER_DATA(buffer) + unit->offset;
    buf_size = unit->size;

    status = decode_buffer(decoder, buf, buf_size);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static void
gst_vaapi_decoder_jpeg_finalize(GObject *object)
{
    GstVaapiDecoderJpeg * const decoder = GST_VAAPI_DECODER_JPEG(object);

    gst_vaapi_decoder_jpeg_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_jpeg_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_jpeg_constructed(GObject *object)
{
    GstVaapiDecoderJpeg * const decoder = GST_VAAPI_DECODER_JPEG(object);
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_jpeg_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_jpeg_create(decoder);
}

static void
gst_vaapi_decoder_jpeg_class_init(GstVaapiDecoderJpegClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderJpegPrivate));

    object_class->finalize      = gst_vaapi_decoder_jpeg_finalize;
    object_class->constructed   = gst_vaapi_decoder_jpeg_constructed;

    decoder_class->parse        = gst_vaapi_decoder_jpeg_parse;
    decoder_class->decode       = gst_vaapi_decoder_jpeg_decode;
}

static void
gst_vaapi_decoder_jpeg_init(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate *priv;

    priv                        = GST_VAAPI_DECODER_JPEG_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->profile               = GST_VAAPI_PROFILE_JPEG_BASELINE;
    priv->width                 = 0;
    priv->height                = 0;
    priv->current_picture       = NULL;
    priv->has_huf_table         = FALSE;
    priv->has_quant_table       = FALSE;
    priv->mcu_restart           = 0;
    priv->is_opened             = FALSE;
    priv->profile_changed       = TRUE;
    priv->is_constructed        = FALSE;
    memset(&priv->frame_hdr, 0, sizeof(priv->frame_hdr));
    memset(&priv->huf_tables, 0, sizeof(priv->huf_tables));
    memset(&priv->quant_tables, 0, sizeof(priv->quant_tables));
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
gst_vaapi_decoder_jpeg_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderJpeg *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_JPEG,
        "display",      display,
        "caps",         caps,
        NULL
    );
    if (!decoder->priv->is_constructed) {
        g_object_unref(decoder);
        return NULL;
    }
    return GST_VAAPI_DECODER_CAST(decoder);
}
