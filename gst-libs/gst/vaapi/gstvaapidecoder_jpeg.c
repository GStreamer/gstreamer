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
    case GST_JPEG_PARSER_FRAME_ERROR:
    case GST_JPEG_PARSER_SCAN_ERROR:
    case GST_JPEG_PARSER_HUFFMAN_ERROR:
    case GST_JPEG_PARSER_QUANT_ERROR:
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
    GstJpegImage        *jpeg_image
)
{
    VAPictureParameterBufferJPEG *pic_param = picture->param;
    guint i;

    g_assert(pic_param);

    memset(pic_param, 0, sizeof(VAPictureParameterBufferJPEG));
    pic_param->type             = jpeg_image->frame_type;
    pic_param->sample_precision = jpeg_image->sample_precision;
    pic_param->image_width      = jpeg_image->width;
    pic_param->image_height     = jpeg_image->height;

    /* XXX: ROI + rotation */

    pic_param->num_components   = jpeg_image->num_components;
    for (i = 0; i < pic_param->num_components; i++) {
        pic_param->components[i].component_id =
            jpeg_image->components[i].identifier;
        pic_param->components[i].h_sampling_factor =
            jpeg_image->components[i].horizontal_factor;
        pic_param->components[i].v_sampling_factor =
            jpeg_image->components[i].vertical_factor;
        pic_param->components[i].quantiser_table_selector =
            jpeg_image->components[i].quant_table_selector;
    }
    return TRUE;
}

static gboolean
fill_quantization_table(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture,
    GstJpegImage        *jpeg_image
)
{
    VAIQMatrixBufferJPEG *iq_matrix;
    int i = 0;
    
    picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(JPEG, decoder);
    g_assert(picture->iq_matrix);
    iq_matrix = picture->iq_matrix->param;
    memset(iq_matrix, 0, sizeof(VAIQMatrixBufferJPEG));
    for (i = 0; i < GST_JPEG_MAX_COMPONENTS; i++) {
        iq_matrix->precision[i] = jpeg_image->quant_tables[i].quant_precision;
        if (iq_matrix->precision[i] == 0) /* 8-bit values*/
          memcpy(iq_matrix->quantiser_matrix[i], jpeg_image->quant_tables[i].quant_table, 64);
        else
          memcpy(iq_matrix->quantiser_matrix[i], jpeg_image->quant_tables[i].quant_table, 128);
    }
    return TRUE;
}

static gboolean
fill_huffman_table(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture,
    GstJpegImage        *jpeg_image
)
{
    VAHuffmanTableBufferJPEG *huffman_table;
    int i;
    
    picture->huf_table = GST_VAAPI_HUFFMAN_TABLE_NEW(JPEG, decoder);
    g_assert(picture->huf_table);
    huffman_table = picture->huf_table->param;
    memset(huffman_table, 0, sizeof(VAHuffmanTableBufferJPEG));
    for (i = 0; i < GST_JPEG_MAX_COMPONENTS; i++) {
        memcpy(huffman_table->huffman_table[i].dc_bits,
               jpeg_image->dc_huf_tables[i].huf_bits, 
               16);
        memcpy(huffman_table->huffman_table[i].dc_huffval,
               jpeg_image->dc_huf_tables[i].huf_values, 
               16);
        memcpy(huffman_table->huffman_table[i].ac_bits,
               jpeg_image->ac_huf_tables[i].huf_bits, 
               16);
        memcpy(huffman_table->huffman_table[i].ac_huffval,
               jpeg_image->ac_huf_tables[i].huf_values, 
               256);
    }
    return TRUE;
}

static guint
get_max_horizontal_samples(GstJpegImage *jpeg)
{
    guint i, max_factor = 0;

    for (i = 0; i < jpeg->num_components; i++) {
        if (jpeg->components[i].horizontal_factor > max_factor)
            max_factor = jpeg->components[i].horizontal_factor;
    }
    return max_factor;
}

static guint
get_max_vertical_samples(GstJpegImage *jpeg)
{
    guint i, max_factor = 0;

    for (i = 0; i < jpeg->num_components; i++) {
        if (jpeg->components[i].vertical_factor > max_factor)
            max_factor = jpeg->components[i].vertical_factor;
    }
    return max_factor;
}

static gboolean
fill_slices(
    GstVaapiDecoderJpeg *decoder, 
    GstVaapiPicture     *picture,
    GstJpegImage        *jpeg_image
)
{
    VASliceParameterBufferJPEG *slice_param;
    GstVaapiSlice *gst_slice;
    int i;
    const guint8 *sos_src;
    guint32 sos_length;
    guint total_h_samples, total_v_samples;

    while (gst_jpeg_get_left_size(jpeg_image)) {
        sos_src = gst_jpeg_get_position(jpeg_image);
        sos_length = gst_jpeg_skip_to_scan_end(jpeg_image);
        if (!sos_length)
            break;
        gst_slice = GST_VAAPI_SLICE_NEW(JPEG, decoder, sos_src, sos_length);
        slice_param = gst_slice->param;        
        slice_param->num_components = jpeg_image->current_scan.num_components;
        for (i = 0; i < slice_param->num_components; i++) {
            slice_param->components[i].component_id = jpeg_image->current_scan.components[i].component_selector;
            slice_param->components[i].dc_selector = jpeg_image->current_scan.components[i].dc_selector;
            slice_param->components[i].ac_selector = jpeg_image->current_scan.components[i].ac_selector;
        }
        slice_param->restart_interval = jpeg_image->restart_interval;
        
        if (jpeg_image->current_scan.num_components == 1) { /*non-interleaved*/
            slice_param->slice_horizontal_position = 0;
            slice_param->slice_vertical_position = 0;
            /* Y mcu numbers*/
            if (slice_param->components[0].component_id == jpeg_image->components[0].identifier) {
                slice_param->num_mcus = (jpeg_image->width/8)*(jpeg_image->height/8);
            } else { /*Cr, Cb mcu numbers*/
                slice_param->num_mcus = (jpeg_image->width/16)*(jpeg_image->height/16);
            }
        } else { /* interleaved */
            slice_param->slice_horizontal_position = 0;
            slice_param->slice_vertical_position = 0;
            total_v_samples = get_max_vertical_samples(jpeg_image);
            total_h_samples = get_max_horizontal_samples(jpeg_image);
            slice_param->num_mcus = ((jpeg_image->width + total_h_samples*8 - 1)/(total_h_samples*8)) * 
                                    ((jpeg_image->height + total_v_samples*8 -1)/(total_v_samples*8));
        }
        
        gst_vaapi_picture_add_slice(picture, gst_slice);
        if (!gst_jpeg_parse_next_scan(jpeg_image)) {
            break;
        }
    }

    if (picture->slices && picture->slices->len)
        return TRUE;
    return FALSE;
}

static GstVaapiDecoderStatus
decode_picture(
    GstVaapiDecoderJpeg *decoder, 
    guchar              *buf,
    guint                buf_size,
    GstClockTime         pts
)
{
    GstVaapiDecoderJpegPrivate * const priv = decoder->priv;
    GstVaapiPicture *picture = NULL;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    GstJpegImage jpeg_image;
    GstJpegParserResult result;

    /* parse jpeg */
    memset(&jpeg_image, 0, sizeof(jpeg_image));
    result = gst_jpeg_parse_image(&jpeg_image, buf, buf_size);
    if (result != GST_JPEG_PARSER_OK) {
        GST_ERROR("failed to parse image");
        return get_status(result);
    }

    /* set info to va parameters in current picture*/
    priv->profile = GST_VAAPI_PROFILE_JPEG_BASELINE;
    priv->height  = jpeg_image.height;
    priv->width   = jpeg_image.width;
    
    status = ensure_context(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset context");
        return status;
    }

    picture = GST_VAAPI_PICTURE_NEW(JPEG, decoder);
    if (!picture) {
        GST_ERROR("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_replace(&priv->current_picture, picture);
    gst_vaapi_picture_unref(picture);

    if (!fill_picture(decoder, picture, &jpeg_image))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    if (!fill_quantization_table(decoder, picture, &jpeg_image)) {
        GST_ERROR("failed to fill in quantization table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!fill_huffman_table(decoder, picture, &jpeg_image)) {
        GST_ERROR("failed to fill in huffman table");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!fill_slices(decoder, picture, &jpeg_image)) {
        GST_ERROR("failed to fill in all scans");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    /* Update presentation time */
    picture->pts = pts;
    return status;
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderJpeg *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderStatus status;
    GstClockTime pts;
    guchar *buf;
    guint buf_size;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    pts = GST_BUFFER_TIMESTAMP(buffer);
    g_assert(GST_CLOCK_TIME_IS_VALID(pts));

    status = decode_picture(decoder, buf, buf_size, pts);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
        return status;
    return decode_current_picture(decoder);
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
    priv->is_opened             = FALSE;
    priv->profile_changed       = TRUE;
    priv->is_constructed        = FALSE;
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
