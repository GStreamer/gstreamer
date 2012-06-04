/*
 *  gstvaapidecoder_jpeg.c - JPEG decoder
 *
 *  Copyright (C) 2011-2012 Intel Corporation
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
#include "gstvaapidecoder_jpeg.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderJpeg,
              gst_vaapi_decoder_jpeg,
              GST_VAAPI_TYPE_DECODER);

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
    GstJpegQuantTable           quant_tables[GST_JPEG_MAX_SCAN_COMPONENTS];
    gboolean                    has_huf_table;
    gboolean                    has_quant_table;
    guint                       mcu_restart;
    guint                       is_opened       : 1;
    guint                       profile_changed : 1;
    guint                       is_constructed  : 1;
};


static GstVaapiDecoderStatus
get_status(GstJpegParserResult result)
{
    GstVaapiDecoderStatus status;

    switch (result) {
    case GST_JPEG_PARSER_OK:
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    case GST_JPEG_PARSER_BROKEN_DATA:
    case GST_JPEG_PARSER_NO_SCAN_FOUND:
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    default:
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        break;
    }
    return status;
}

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
    priv->is_constructed        = FALSE;
}

static gboolean
gst_vaapi_decoder_jpeg_open(GstVaapiDecoderJpeg *decoder, GstBuffer *buffer)
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
        reset_context = gst_vaapi_decoder_ensure_context(
            GST_VAAPI_DECODER(decoder),
            priv->profile,
            entrypoint,
            priv->width, priv->height
        );
        if (!reset_context)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline GstVaapiDecoderStatus
decode_current_picture(GstVaapiDecoderJpeg *decoder)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->current_picture;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (picture) {
        if (!gst_vaapi_picture_decode(picture))
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        else if (!gst_vaapi_picture_output(picture))
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        gst_vaapi_picture_replace(&priv->current_picture, NULL);
    }
    return status;
}

static gboolean
fill_picture(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture,
    GstJpegFrameHdr     *jpeg_frame_hdr
)
{
    VAPictureParameterBufferJPEG *pic_param = picture->param;
    guint i;

    g_assert(pic_param);

    memset(pic_param, 0, sizeof(VAPictureParameterBufferJPEG));
    pic_param->type             = jpeg_frame_hdr->profile;
    pic_param->sample_precision = jpeg_frame_hdr->sample_precision;
    pic_param->image_width      = jpeg_frame_hdr->width;
    pic_param->image_height     = jpeg_frame_hdr->height;

    /* XXX: ROI + rotation */

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
    VAIQMatrixBufferJPEG *iq_matrix;
    guint i, j;

    if (!priv->has_quant_table)
        gst_jpeg_get_default_quantization_table(priv->quant_tables, GST_JPEG_MAX_SCAN_COMPONENTS);
    
    picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(JPEG, decoder);
    g_assert(picture->iq_matrix);
    iq_matrix = picture->iq_matrix->param;
    memset(iq_matrix, 0, sizeof(VAIQMatrixBufferJPEG));
    for (i = 0; i < GST_JPEG_MAX_SCAN_COMPONENTS; i++) {
        iq_matrix->precision[i] = priv->quant_tables[i].quant_precision;
        if (iq_matrix->precision[i] == 0) /* 8-bit values */
            for (j = 0; j < GST_JPEG_MAX_QUANT_ELEMENTS; j++) {
                iq_matrix->quantiser_matrix[i][j] =
                    priv->quant_tables[i].quant_table[j];
            }
        else
            memcpy(iq_matrix->quantiser_matrix[i],
                   priv->quant_tables[i].quant_table,
                   128);
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
    VAHuffmanTableBufferJPEG *huffman_table;
    guint i;

    if (!priv->has_huf_table)
        gst_jpeg_get_default_huffman_table(&priv->huf_tables);
    
    picture->huf_table = GST_VAAPI_HUFFMAN_TABLE_NEW(JPEG, decoder);
    g_assert(picture->huf_table);
    huffman_table = picture->huf_table->param;
    memset(huffman_table, 0, sizeof(VAHuffmanTableBufferJPEG));
    for (i = 0; i < GST_JPEG_MAX_SCAN_COMPONENTS; i++) {
        memcpy(huffman_table->huffman_table[i].dc_bits,
               priv->huf_tables.dc_tables[i].huf_bits,
               16);
        memcpy(huffman_table->huffman_table[i].dc_huffval,
               priv->huf_tables.dc_tables[i].huf_values,
               16);
        memcpy(huffman_table->huffman_table[i].ac_bits,
               priv->huf_tables.ac_tables[i].huf_bits,
               16);
        memcpy(huffman_table->huffman_table[i].ac_huffval,
               priv->huf_tables.ac_tables[i].huf_values,
               256);
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
    guchar              *buf,
    guint                buf_size,
    GstClockTime         pts
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture *picture = NULL;
    GstJpegFrameHdr *jpeg_frame_hdr = &priv->frame_hdr;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    GstJpegParserResult result;

    /* parse jpeg */
    memset(jpeg_frame_hdr, 0, sizeof(GstJpegFrameHdr));
    switch (profile) {
        case GST_JPEG_MARKER_SOF_MIN:
            jpeg_frame_hdr->profile = GST_JPEG_MARKER_SOF_MIN;
            priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
            break;
        default:
            GST_WARNING("Jpeg profile was not supported.");
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    result = gst_jpeg_parse_frame_hdr(jpeg_frame_hdr, buf, buf_size, 0);
    if (result != GST_JPEG_PARSER_OK) {
        GST_ERROR("failed to parse image");
        return get_status(result);
    }

    /* set info to va parameters in current picture*/
    priv->height  = jpeg_frame_hdr->height;
    priv->width   = jpeg_frame_hdr->width;

    status = ensure_context(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset context");
        return status;
    }

    if (priv->current_picture &&
        (status = decode_current_picture(decoder)) != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;

    picture = GST_VAAPI_PICTURE_NEW(JPEG, decoder);
    if (!picture) {
        GST_ERROR("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_replace(&priv->current_picture, picture);
    gst_vaapi_picture_unref(picture);

    if (!fill_picture(decoder, picture, jpeg_frame_hdr))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    /* Update presentation time */
    picture->pts = pts;
    return status;
}

static GstVaapiDecoderStatus
decode_huffman_table(
    GstVaapiDecoderJpeg *decoder,
    guchar              *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstJpegParserResult result;

    result = gst_jpeg_parse_huffman_table(
        &priv->huf_tables,
        buf, buf_size, 0
    );
    if (result != GST_JPEG_PARSER_OK) {
        GST_DEBUG("failed to parse Huffman table");
        return get_status(result);
    }
    priv->has_huf_table = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_quant_table(
    GstVaapiDecoderJpeg *decoder,
    guchar              *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstJpegParserResult result;

    result = gst_jpeg_parse_quant_table(
        priv->quant_tables,
        GST_JPEG_MAX_SCAN_COMPONENTS,
        buf, buf_size, 0
    );
    if (result != GST_JPEG_PARSER_OK) {
        GST_DEBUG("failed to parse quantization table");
        return get_status(result);
    }
    priv->has_quant_table = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_restart_interval(
    GstVaapiDecoderJpeg *decoder,
    guchar              *buf,
    guint                buf_size
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstJpegParserResult result;

    result = gst_jpeg_parse_restart_interval(
        &priv->mcu_restart,
        buf, buf_size, 0
    );
    if (result != GST_JPEG_PARSER_OK) {
        GST_DEBUG("failed to parse restart interval");
        return get_status(result);
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_scan(
    GstVaapiDecoderJpeg *decoder,
    guchar              *scan_header,
    guint                scan_header_size,
    guchar              *scan_data,
    guint                scan_data_size)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture *picture = priv->current_picture;
    GstJpegParserResult result = GST_JPEG_PARSER_OK;
    VASliceParameterBufferJPEG *slice_param;
    GstVaapiSlice *gst_slice;
    guint total_h_samples, total_v_samples;
    GstJpegScanHdr  scan_hdr;
    guint i;

    if (!picture) {
        GST_ERROR ("There is no VAPicture before decoding scan.");
        return GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE;
    }

    if (!fill_quantization_table(decoder, picture)) {
        GST_ERROR("Failed to fill in quantization table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!fill_huffman_table(decoder, picture)) {
        GST_ERROR("Failed to fill in huffman table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    memset(&scan_hdr, 0, sizeof(scan_hdr));
    result = gst_jpeg_parse_scan_hdr(&scan_hdr, scan_header, scan_header_size, 0);
    if (result != GST_JPEG_PARSER_OK) {
        GST_DEBUG("Jpeg parsed scan failed.");
        return get_status(result);
    }

    gst_slice = GST_VAAPI_SLICE_NEW(JPEG, decoder, scan_data, scan_data_size);
    gst_vaapi_picture_add_slice(picture, gst_slice);

    slice_param = gst_slice->param;
    slice_param->num_components = scan_hdr.num_components;
    for (i = 0; i < scan_hdr.num_components; i++) {
        slice_param->components[i].component_id = scan_hdr.components[i].component_selector;
        slice_param->components[i].dc_selector = scan_hdr.components[i].dc_selector;
        slice_param->components[i].ac_selector = scan_hdr.components[i].ac_selector;
    }
    slice_param->restart_interval = priv->mcu_restart;
    if (scan_hdr.num_components == 1) { /*non-interleaved*/
        slice_param->slice_horizontal_position = 0;
        slice_param->slice_vertical_position = 0;
        /* Y mcu numbers*/
        if (slice_param->components[0].component_id == priv->frame_hdr.components[0].identifier) {
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
decode_buffer(GstVaapiDecoderJpeg *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    GstClockTime pts;
    guchar *buf;
    guint buf_size;
    GList *seg_list = NULL;
    gboolean scan_found = FALSE;
    GstJpegTypeOffsetSize *seg;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    pts = GST_BUFFER_TIMESTAMP(buffer);
    g_assert(GST_CLOCK_TIME_IS_VALID(pts));

    priv->has_quant_table = FALSE;
    priv->has_huf_table = FALSE;
    priv->mcu_restart = 0;

    seg_list = gst_jpeg_parse(buf, buf_size, 0);
    for (; seg_list && !scan_found; seg_list = seg_list->next) {
        seg = seg_list->data;
        g_assert(seg);
        switch (seg->type) {
        case GST_JPEG_MARKER_DHT:
            status = decode_huffman_table(decoder, buf + seg->offset, seg->size);
            break;
        case GST_JPEG_MARKER_DAC:
            GST_ERROR("unsupported arithmetic decoding");
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
            break;
        case GST_JPEG_MARKER_DQT:
            status = decode_quant_table(decoder, buf + seg->offset, seg->size);
            break;
        case GST_JPEG_MARKER_DRI:
            status = decode_restart_interval(decoder, buf + seg->offset, seg->size);
            break;
        case GST_JPEG_MARKER_SOS: {
            GstJpegScanOffsetSize * const scan = (GstJpegScanOffsetSize *)seg;
            status = decode_scan(
                decoder, buf + scan->header.offset, scan->header.size,
                buf + scan->data_offset, scan->data_size
            );
            scan_found = TRUE;
            break;
        }
        default:
            if (seg->type >= GST_JPEG_MARKER_SOF_MIN &&
                seg->type <= GST_JPEG_MARKER_SOF_MAX)
                status = decode_picture(decoder, seg->type, buf + seg->offset, seg->size, pts);
            else {
                GST_WARNING("unsupported marker (0x%02x)", seg->type);
                status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
            }
            break;
        }
    }

    /* clean seg_list */
    g_list_free_full(seg_list, g_free);

    if (status == GST_VAAPI_DECODER_STATUS_SUCCESS) {
        if (scan_found)
            return decode_current_picture(decoder);
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    }
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_jpeg_decode(GstVaapiDecoder *base, GstBuffer *buffer)
{
    GstVaapiDecoderJpeg * const decoder = GST_VAAPI_DECODER_JPEG(base);
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_jpeg_open(decoder, buffer);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    }
    return decode_buffer(decoder, buffer);
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
    memset(priv->quant_tables, 0, sizeof(priv->quant_tables));
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
