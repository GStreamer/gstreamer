/*
 *  gstvaapidecoder_mpeg2.c - MPEG-2 decoder
 *
 *  Copyright (C) 2011 Intel Corporation
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
 * SECTION:gstvaapidecoder_mpeg2
 * @short_description: MPEG-2 decoder
 */

#include "sysdeps.h"
#include <string.h>
#include <gst/base/gstbitreader.h>
#include <gst/codecparsers/gstmpegvideoparser.h>
#include "gstvaapidecoder_mpeg2.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderMpeg2,
              gst_vaapi_decoder_mpeg2,
              GST_VAAPI_TYPE_DECODER);

#define GST_VAAPI_DECODER_MPEG2_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_MPEG2,  \
                                 GstVaapiDecoderMpeg2Private))

#define READ_UINT8(br, val, nbits) G_STMT_START {  \
  if (!gst_bit_reader_get_bits_uint8 (br, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

#define SKIP(reader, nbits) G_STMT_START { \
  if (!gst_bit_reader_skip (reader, nbits)) { \
    GST_WARNING ("failed to skip nbits: %d", nbits); \
    goto failed; \
  } \
} G_STMT_END

struct _GstVaapiDecoderMpeg2Private {
    GstVaapiProfile             profile;
    guint                       width;
    guint                       height;
    guint                       fps_n;
    guint                       fps_d;
    GstMpegVideoSequenceHdr     seq_hdr;
    GstMpegVideoSequenceExt     seq_ext;
    GstMpegVideoPictureHdr      pic_hdr;
    GstMpegVideoPictureExt      pic_ext;
    GstMpegVideoQuantMatrixExt  quant_matrix_ext;
    GstVaapiPicture            *current_picture;
    GstVaapiPicture            *next_picture;
    GstVaapiPicture            *prev_picture;
    GstAdapter                 *adapter;
    GstClockTime                seq_pts;
    GstClockTime                gop_pts;
    GstClockTime                pts_diff;
    guint                       is_constructed          : 1;
    guint                       is_opened               : 1;
    guint                       is_first_field          : 1;
    guint                       has_seq_ext             : 1;
    guint                       has_seq_scalable_ext    : 1;
    guint                       has_pic_ext             : 1;
    guint                       has_quant_matrix_ext    : 1;
    guint                       size_changed            : 1;
    guint                       profile_changed         : 1;
    guint                       quant_matrix_changed    : 1;
    guint                       progressive_sequence    : 1;
    guint                       closed_gop              : 1;
    guint                       broken_link             : 1;
};

/* VLC decoder from gst-plugins-bad */
typedef struct _VLCTable VLCTable;
struct _VLCTable {
    gint  value;
    guint cword;
    guint cbits;
};

static gboolean
decode_vlc(GstBitReader *br, gint *res, const VLCTable *table, guint length)
{
    guint8 i;
    guint cbits = 0;
    guint32 value = 0;

    for (i = 0; i < length; i++) {
        if (cbits != table[i].cbits) {
            cbits = table[i].cbits;
            if (!gst_bit_reader_peek_bits_uint32(br, &value, cbits)) {
                goto failed;
            }
        }

        if (value == table[i].cword) {
            SKIP(br, cbits);
            if (res)
                *res = table[i].value;
            return TRUE;
        }
    }
    GST_DEBUG("failed to find VLC code");

failed:
    GST_WARNING("failed to decode VLC, returning");
    return FALSE;
}

enum {
    GST_MPEG_VIDEO_MACROBLOCK_ESCAPE = -1,
};

/* Table B-1: Variable length codes for macroblock_address_increment */
static const VLCTable mpeg2_mbaddr_vlc_table[] = {
    {  1, 0x01,  1 },
    {  2, 0x03,  3 },
    {  3, 0x02,  3 },
    {  4, 0x03,  4 },
    {  5, 0x02,  4 },
    {  6, 0x03,  5 },
    {  7, 0x02,  5 },
    {  8, 0x07,  7 },
    {  9, 0x06,  7 },
    { 10, 0x0b,  8 },
    { 11, 0x0a,  8 },
    { 12, 0x09,  8 },
    { 13, 0x08,  8 },
    { 14, 0x07,  8 },
    { 15, 0x06,  8 },
    { 16, 0x17, 10 },
    { 17, 0x16, 10 },
    { 18, 0x15, 10 },
    { 19, 0x14, 10 },
    { 20, 0x13, 10 },
    { 21, 0x12, 10 },
    { 22, 0x23, 11 },
    { 23, 0x22, 11 },
    { 24, 0x21, 11 },
    { 25, 0x20, 11 },
    { 26, 0x1f, 11 },
    { 27, 0x1e, 11 },
    { 28, 0x1d, 11 },
    { 29, 0x1c, 11 },
    { 30, 0x1b, 11 },
    { 31, 0x1a, 11 },
    { 32, 0x19, 11 },
    { 33, 0x18, 11 },
    { GST_MPEG_VIDEO_MACROBLOCK_ESCAPE, 0x08, 11 }
};

static void
gst_vaapi_decoder_mpeg2_close(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    gst_vaapi_picture_replace(&priv->next_picture,    NULL);
    gst_vaapi_picture_replace(&priv->prev_picture,    NULL);

    if (priv->adapter) {
        gst_adapter_clear(priv->adapter);
        g_object_unref(priv->adapter);
        priv->adapter = NULL;
    }
}

static gboolean
gst_vaapi_decoder_mpeg2_open(GstVaapiDecoderMpeg2 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;

    gst_vaapi_decoder_mpeg2_close(decoder);

    priv->adapter = gst_adapter_new();
    if (!priv->adapter)
	return FALSE;
    return TRUE;
}

static void
gst_vaapi_decoder_mpeg2_destroy(GstVaapiDecoderMpeg2 *decoder)
{
    gst_vaapi_decoder_mpeg2_close(decoder);
}

static gboolean
gst_vaapi_decoder_mpeg2_create(GstVaapiDecoderMpeg2 *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static inline void
copy_quant_matrix(guint8 dst[64], const guint8 src[64])
{
    memcpy(dst, src, 64);
}

static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiProfile profiles[2];
    GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
    guint i, n_profiles = 0;
    gboolean reset_context = FALSE;

    if (priv->profile_changed) {
        GST_DEBUG("profile changed");
        priv->profile_changed = FALSE;
        reset_context         = TRUE;

        profiles[n_profiles++] = priv->profile;
        if (priv->profile == GST_VAAPI_PROFILE_MPEG2_SIMPLE)
            profiles[n_profiles++] = GST_VAAPI_PROFILE_MPEG2_MAIN;

        for (i = 0; i < n_profiles; i++) {
            if (gst_vaapi_display_has_decoder(GST_VAAPI_DECODER_DISPLAY(decoder),
                                              profiles[i], entrypoint))
                break;
        }
        if (i == n_profiles)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
        priv->profile = profiles[i];
    }

    if (priv->size_changed) {
        GST_DEBUG("size changed");
        priv->size_changed = FALSE;
        reset_context      = TRUE;
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

static GstVaapiDecoderStatus
ensure_quant_matrix(GstVaapiDecoderMpeg2 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    VAIQMatrixBufferMPEG2 *iq_matrix;
    guint8 *intra_quant_matrix = NULL;
    guint8 *non_intra_quant_matrix = NULL;
    guint8 *chroma_intra_quant_matrix = NULL;
    guint8 *chroma_non_intra_quant_matrix = NULL;

    if (!priv->quant_matrix_changed)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    priv->quant_matrix_changed = FALSE;

    picture->iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(MPEG2, decoder);
    if (!picture->iq_matrix) {
        GST_DEBUG("failed to allocate IQ matrix");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    iq_matrix = picture->iq_matrix->param;

    intra_quant_matrix     = priv->seq_hdr.intra_quantizer_matrix;
    non_intra_quant_matrix = priv->seq_hdr.non_intra_quantizer_matrix;
    if (priv->has_quant_matrix_ext) {
        if (priv->quant_matrix_ext.load_intra_quantiser_matrix)
            intra_quant_matrix = priv->quant_matrix_ext.intra_quantiser_matrix;
        if (priv->quant_matrix_ext.load_non_intra_quantiser_matrix)
            non_intra_quant_matrix = priv->quant_matrix_ext.non_intra_quantiser_matrix;
        if (priv->quant_matrix_ext.load_chroma_intra_quantiser_matrix)
            chroma_intra_quant_matrix = priv->quant_matrix_ext.chroma_intra_quantiser_matrix;
        if (priv->quant_matrix_ext.load_chroma_non_intra_quantiser_matrix)
            chroma_non_intra_quant_matrix = priv->quant_matrix_ext.chroma_non_intra_quantiser_matrix;
    }

    iq_matrix->load_intra_quantiser_matrix = intra_quant_matrix != NULL;
    if (intra_quant_matrix)
        copy_quant_matrix(iq_matrix->intra_quantiser_matrix,
                          intra_quant_matrix);

    iq_matrix->load_non_intra_quantiser_matrix = non_intra_quant_matrix != NULL;
    if (non_intra_quant_matrix)
        copy_quant_matrix(iq_matrix->non_intra_quantiser_matrix,
                          non_intra_quant_matrix);

    iq_matrix->load_chroma_intra_quantiser_matrix = chroma_intra_quant_matrix != NULL;
    if (chroma_intra_quant_matrix)
        copy_quant_matrix(iq_matrix->chroma_intra_quantiser_matrix,
                          chroma_intra_quant_matrix);

    iq_matrix->load_chroma_non_intra_quantiser_matrix = chroma_non_intra_quant_matrix != NULL;
    if (chroma_non_intra_quant_matrix)
        copy_quant_matrix(iq_matrix->chroma_non_intra_quantiser_matrix,
                          chroma_non_intra_quant_matrix);
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline GstVaapiDecoderStatus
render_picture(GstVaapiDecoderMpeg2 *decoder, GstVaapiPicture *picture)
{
    if (!gst_vaapi_picture_output(picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_current_picture(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->current_picture;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (picture) {
        if (!gst_vaapi_picture_decode(picture))
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
            if ((priv->prev_picture && priv->next_picture) ||
                (priv->closed_gop && priv->next_picture))
                status = render_picture(decoder, picture);
        }
        gst_vaapi_picture_replace(&priv->current_picture, NULL);
    }
    return status;
}

static GstVaapiDecoderStatus
decode_sequence(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoder * const base_decoder = GST_VAAPI_DECODER(decoder);
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoSequenceHdr * const seq_hdr = &priv->seq_hdr;

    if (!gst_mpeg_video_parse_sequence_header(seq_hdr, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse sequence header");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->fps_n = seq_hdr->fps_n;
    priv->fps_d = seq_hdr->fps_d;
    gst_vaapi_decoder_set_framerate(base_decoder, priv->fps_n, priv->fps_d);

    priv->seq_pts = gst_adapter_prev_timestamp(priv->adapter, NULL);

    priv->width                 = seq_hdr->width;
    priv->height                = seq_hdr->height;
    priv->has_seq_ext           = FALSE;
    priv->size_changed          = TRUE;
    priv->quant_matrix_changed  = TRUE;
    priv->progressive_sequence  = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_ext(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoder * const base_decoder = GST_VAAPI_DECODER(decoder);
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoSequenceExt * const seq_ext = &priv->seq_ext;
    GstVaapiProfile profile;
    guint width, height;

    if (!gst_mpeg_video_parse_sequence_extension(seq_ext, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse sequence-extension");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_seq_ext = TRUE;
    priv->progressive_sequence = seq_ext->progressive;

    width  = (priv->width  & 0x0fff) | ((guint32)seq_ext->horiz_size_ext << 12);
    height = (priv->height & 0x0fff) | ((guint32)seq_ext->vert_size_ext  << 12);
    GST_DEBUG("video resolution %ux%u", width, height);

    if (seq_ext->fps_n_ext && seq_ext->fps_d_ext) {
        priv->fps_n *= seq_ext->fps_n_ext + 1;
        priv->fps_d *= seq_ext->fps_d_ext + 1;
        gst_vaapi_decoder_set_framerate(base_decoder, priv->fps_n, priv->fps_d);
    }

    if (priv->width != width) {
        priv->width = width;
        priv->size_changed = TRUE;
    }

    if (priv->height != height) {
        priv->height = height;
        priv->size_changed = TRUE;
    }

    switch (seq_ext->profile) {
    case GST_MPEG_VIDEO_PROFILE_SIMPLE:
        profile = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
        break;
    case GST_MPEG_VIDEO_PROFILE_MAIN:
        profile = GST_VAAPI_PROFILE_MPEG2_MAIN;
        break;
    default:
        GST_DEBUG("unsupported profile %d", seq_ext->profile);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }
    if (priv->profile != profile) {
        priv->profile = profile;
        priv->profile_changed = TRUE;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;

    if (priv->current_picture) {
        status = decode_current_picture(decoder);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    if (priv->next_picture) {
        status = render_picture(decoder, priv->next_picture);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }
    return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
}

static GstVaapiDecoderStatus
decode_quant_matrix_ext(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoQuantMatrixExt * const quant_matrix_ext = &priv->quant_matrix_ext;

    if (!gst_mpeg_video_parse_quant_matrix_extension(quant_matrix_ext, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse quant-matrix-extension");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_quant_matrix_ext = TRUE;
    priv->quant_matrix_changed = TRUE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_gop(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoGop gop;
    GstClockTime pts;

    if (!gst_mpeg_video_parse_gop(&gop, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse GOP");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->closed_gop  = gop.closed_gop;
    priv->broken_link = gop.broken_link;

    GST_DEBUG("GOP %02u:%02u:%02u:%02u (closed_gop %d, broken_link %d)",
              gop.hour, gop.minute, gop.second, gop.frame,
              priv->closed_gop, priv->broken_link);

    pts = GST_SECOND * (gop.hour * 3600 + gop.minute * 60 + gop.second);
    pts += gst_util_uint64_scale(gop.frame, GST_SECOND * priv->fps_d, priv->fps_n);
    priv->gop_pts = pts;
    if (!priv->pts_diff)
        priv->pts_diff = priv->seq_pts - priv->gop_pts;

    priv->is_first_field = FALSE;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_picture(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoPictureHdr * const pic_hdr = &priv->pic_hdr;
    GstVaapiPicture *picture;
    GstVaapiDecoderStatus status;
    GstClockTime pts;

    status = ensure_context(decoder);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_DEBUG("failed to reset context");
        return status;
    }

    if (priv->current_picture) {
        status = decode_current_picture(decoder);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
    }

    priv->current_picture = GST_VAAPI_PICTURE_NEW(MPEG2, decoder);
    if (!priv->current_picture) {
        GST_DEBUG("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    picture = priv->current_picture;

    status = ensure_quant_matrix(decoder, picture);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_DEBUG("failed to reset quantizer matrix");
        return status;
    }

    if (!gst_mpeg_video_parse_picture_header(pic_hdr, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse picture header");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_pic_ext = FALSE;

    switch (pic_hdr->pic_type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_I:
        picture->type = GST_VAAPI_PICTURE_TYPE_I;
        break;
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:
        picture->type = GST_VAAPI_PICTURE_TYPE_P;
        break;
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:
        picture->type = GST_VAAPI_PICTURE_TYPE_B;
        break;
    default:
        GST_DEBUG("unsupported picture type %d", pic_hdr->pic_type);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    /* Update presentation time */
    pts = priv->gop_pts;
    pts += gst_util_uint64_scale(pic_hdr->tsn, GST_SECOND * priv->fps_d, priv->fps_n);
    picture->pts = pts + priv->pts_diff;

    /* Update reference pictures */
    if (pic_hdr->pic_type != GST_MPEG_VIDEO_PICTURE_TYPE_B) {
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        if (priv->next_picture)
            status = render_picture(decoder, priv->next_picture);
        gst_vaapi_picture_replace(&priv->prev_picture, priv->next_picture);
        gst_vaapi_picture_replace(&priv->next_picture, picture);
    }
    return status;
}

static GstVaapiDecoderStatus
decode_picture_ext(GstVaapiDecoderMpeg2 *decoder, guchar *buf, guint buf_size)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoPictureExt * const pic_ext = &priv->pic_ext;
    GstVaapiPicture * const picture = priv->current_picture;

    if (!gst_mpeg_video_parse_picture_extension(pic_ext, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse picture-extension");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_pic_ext = TRUE;

    if (pic_ext->picture_structure == 0 ||
        (pic_ext->progressive_frame &&
         pic_ext->picture_structure != GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME)) {
        GST_WARNING("invalid picture_structure %d, replacing with \"frame\"",
                    pic_ext->picture_structure);
        pic_ext->picture_structure = GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME;
    }

    priv->is_first_field ^= 1;
    switch (pic_ext->picture_structure) {
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_TOP_FIELD:
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_TOP_FIELD);
        break;
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_BOTTOM_FIELD:
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_BOTTOM_FIELD);
        break;
    case GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME:
        priv->is_first_field = TRUE;
        break;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static inline guint32
pack_f_code(guint8 f_code[2][2])
{
    return (((guint32)f_code[0][0] << 12) |
            ((guint32)f_code[0][1] <<  8) |
            ((guint32)f_code[1][0] <<  4) |
            (         f_code[1][1]      ));
}

static gboolean
fill_picture(GstVaapiDecoderMpeg2 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    VAPictureParameterBufferMPEG2 * const pic_param = picture->param;
    GstMpegVideoPictureHdr * const pic_hdr = &priv->pic_hdr;
    GstMpegVideoPictureExt * const pic_ext = &priv->pic_ext;

    if (!priv->has_pic_ext)
        return FALSE;

    /* Fill in VAPictureParameterBufferMPEG2 */
    pic_param->horizontal_size                                          = priv->width;
    pic_param->vertical_size                                            = priv->height;
    pic_param->forward_reference_picture                                = VA_INVALID_ID;
    pic_param->backward_reference_picture                               = VA_INVALID_ID;
    pic_param->picture_coding_type                                      = pic_hdr->pic_type;
    pic_param->f_code                                                   = pack_f_code(pic_ext->f_code);

#define COPY_FIELD(a, b, f) \
    pic_param->a.b.f = pic_ext->f
    pic_param->picture_coding_extension.value                           = 0;
    pic_param->picture_coding_extension.bits.is_first_field             = priv->is_first_field;
    COPY_FIELD(picture_coding_extension, bits, intra_dc_precision);
    COPY_FIELD(picture_coding_extension, bits, picture_structure);
    COPY_FIELD(picture_coding_extension, bits, top_field_first);
    COPY_FIELD(picture_coding_extension, bits, frame_pred_frame_dct);
    COPY_FIELD(picture_coding_extension, bits, concealment_motion_vectors);
    COPY_FIELD(picture_coding_extension, bits, q_scale_type);
    COPY_FIELD(picture_coding_extension, bits, intra_vlc_format);
    COPY_FIELD(picture_coding_extension, bits, alternate_scan);
    COPY_FIELD(picture_coding_extension, bits, repeat_first_field);
    COPY_FIELD(picture_coding_extension, bits, progressive_frame);

    switch (pic_hdr->pic_type) {
    case GST_MPEG_VIDEO_PICTURE_TYPE_B:
        if (priv->next_picture)
            pic_param->backward_reference_picture = priv->next_picture->surface_id;
        // fall-through
    case GST_MPEG_VIDEO_PICTURE_TYPE_P:
        if (priv->prev_picture)
            pic_param->forward_reference_picture = priv->prev_picture->surface_id;
        break;
    }
    return TRUE;
}

static GstVaapiDecoderStatus
decode_slice(
    GstVaapiDecoderMpeg2 *decoder,
    int                   slice_no,
    guchar               *buf,
    guint                 buf_size
)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->current_picture;
    GstVaapiSlice *slice;
    VASliceParameterBufferMPEG2 *slice_param;
    GstBitReader br;
    gint mb_x, mb_y, mb_inc;
    guint macroblock_offset;
    guint8 slice_vertical_position_extension;
    guint8 quantiser_scale_code;
    guint8 intra_slice_flag, intra_slice = 0;
    guint8 extra_bit_slice, junk8;

    GST_DEBUG("slice %d @ %p, %u bytes)", slice_no, buf, buf_size);

    if (picture->slices->len == 0 && !fill_picture(decoder, picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    slice = GST_VAAPI_SLICE_NEW(MPEG2, decoder, buf, buf_size);
    if (!slice) {
        GST_DEBUG("failed to allocate slice");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_add_slice(picture, slice);

    /* Parse slice */
    gst_bit_reader_init(&br, buf, buf_size);
    if (priv->height > 2800)
        READ_UINT8(&br, slice_vertical_position_extension, 3);
    if (priv->has_seq_scalable_ext) {
        GST_DEBUG("failed to parse slice %d. Unsupported sequence_scalable_extension()", slice_no);
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    READ_UINT8(&br, quantiser_scale_code, 5);
    READ_UINT8(&br, extra_bit_slice, 1);
    if (extra_bit_slice == 1) {
        READ_UINT8(&br, intra_slice_flag, 1);
        if (intra_slice_flag) {
            READ_UINT8(&br, intra_slice, 1);
            READ_UINT8(&br, junk8, 7);
        }
        READ_UINT8(&br, extra_bit_slice, 1);
        while (extra_bit_slice == 1) {
            READ_UINT8(&br, junk8, 8);
            READ_UINT8(&br, extra_bit_slice, 1);
        }
    }
    macroblock_offset = gst_bit_reader_get_pos(&br);

    mb_y = slice_no;
    mb_x = -1;
    do {
        if (!decode_vlc(&br, &mb_inc, mpeg2_mbaddr_vlc_table,
                        G_N_ELEMENTS(mpeg2_mbaddr_vlc_table))) {
            GST_WARNING("failed to decode first macroblock_address_increment");
            goto failed;
        }
        mb_x += mb_inc == GST_MPEG_VIDEO_MACROBLOCK_ESCAPE ? 33 : mb_inc;
    } while (mb_inc == GST_MPEG_VIDEO_MACROBLOCK_ESCAPE);

    /* Fill in VASliceParameterBufferMPEG2 */
    slice_param                            = slice->param;
    slice_param->macroblock_offset         = macroblock_offset;
    slice_param->slice_horizontal_position = mb_x;
    slice_param->slice_vertical_position   = mb_y;
    slice_param->quantiser_scale_code      = quantiser_scale_code;
    slice_param->intra_slice_flag          = intra_slice;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

failed:
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
}

static inline gint
scan_for_start_code(GstAdapter *adapter, guint ofs, guint size, guint32 *scp)
{
    return (gint)gst_adapter_masked_scan_uint32_peek(adapter,
                                                     0xffffff00, 0x00000100,
                                                     ofs, size,
                                                     scp);
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderMpeg2 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    guchar *buf;
    guint buf_size, size;
    guint32 start_code;
    guint8 type;
    gint ofs;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return decode_sequence_end(decoder);

    gst_adapter_push(priv->adapter, gst_buffer_ref(buffer));

    size   = gst_adapter_available(priv->adapter);
    status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
    do {
        if (size < 8)
            break;
        ofs = scan_for_start_code(priv->adapter, 0, size, &start_code);
        if (ofs < 0)
            break;
        gst_adapter_flush(priv->adapter, ofs);
        size -= ofs;

        status = gst_vaapi_decoder_check_status(GST_VAAPI_DECODER(decoder));
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            break;

        if (size < 8)
            break;
        ofs = scan_for_start_code(priv->adapter, 4, size - 4, NULL);
        if (ofs < 0)
            break;
        gst_adapter_flush(priv->adapter, 4);
        size -= ofs;

        buffer   = gst_adapter_take_buffer(priv->adapter, ofs - 4);
        buf      = GST_BUFFER_DATA(buffer);
        buf_size = GST_BUFFER_SIZE(buffer);

        type = start_code & 0xff;
        switch (type) {
        case GST_MPEG_VIDEO_PACKET_PICTURE:
            if (!priv->width || !priv->height)
                break;
            status = decode_picture(decoder, buf, buf_size);
            break;
        case GST_MPEG_VIDEO_PACKET_SEQUENCE:
            status = decode_sequence(decoder, buf, buf_size);
            break;
        case GST_MPEG_VIDEO_PACKET_EXTENSION: {
            const guchar id = buf[0] >> 4;
            switch (id) {
            case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE:
                status = decode_sequence_ext(decoder, buf, buf_size);
                break;
            case GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX:
                status = decode_quant_matrix_ext(decoder, buf, buf_size);
                break;
            case GST_MPEG_VIDEO_PACKET_EXT_PICTURE:
                if (!priv->width || !priv->height)
                    break;
                status = decode_picture_ext(decoder, buf, buf_size);
                break;
            default:
                // Ignore unknown extensions
                GST_DEBUG("unsupported start-code extension (0x%02x)", id);
                break;
            }
            break;
        }
        case GST_MPEG_VIDEO_PACKET_SEQUENCE_END:
            status = decode_sequence_end(decoder);
            break;
        case GST_MPEG_VIDEO_PACKET_GOP:
            status = decode_gop(decoder, buf, buf_size);
            break;
        case GST_MPEG_VIDEO_PACKET_USER_DATA:
            // Ignore user-data packets
            status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            break;
        default:
            if (type >= GST_MPEG_VIDEO_PACKET_SLICE_MIN &&
                type <= GST_MPEG_VIDEO_PACKET_SLICE_MAX) {
                if (!priv->current_picture)
                    break;
                status = decode_slice(
                    decoder,
                    type - GST_MPEG_VIDEO_PACKET_SLICE_MIN,
                    buf, buf_size
                );
                break;
            }
            else if (type >= 0xb9 && type <= 0xff) {
                // Ignore system start codes (PES headers)
                status = GST_VAAPI_DECODER_STATUS_SUCCESS;
                break;
            }
            GST_DEBUG("unsupported start code (0x%02x)", type);
            status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
            break;
        }
        gst_buffer_unref(buffer);
    } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_mpeg2_decode(GstVaapiDecoder *base, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg2 * const decoder = GST_VAAPI_DECODER_MPEG2(base);
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_mpeg2_open(decoder, buffer);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    }
    return decode_buffer(decoder, buffer);
}

static void
gst_vaapi_decoder_mpeg2_finalize(GObject *object)
{
    GstVaapiDecoderMpeg2 * const decoder = GST_VAAPI_DECODER_MPEG2(object);

    gst_vaapi_decoder_mpeg2_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_mpeg2_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_mpeg2_constructed(GObject *object)
{
    GstVaapiDecoderMpeg2 * const decoder = GST_VAAPI_DECODER_MPEG2(object);
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_mpeg2_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_mpeg2_create(decoder);
}

static void
gst_vaapi_decoder_mpeg2_class_init(GstVaapiDecoderMpeg2Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderMpeg2Private));

    object_class->finalize      = gst_vaapi_decoder_mpeg2_finalize;
    object_class->constructed   = gst_vaapi_decoder_mpeg2_constructed;

    decoder_class->decode       = gst_vaapi_decoder_mpeg2_decode;
}

static void
gst_vaapi_decoder_mpeg2_init(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private *priv;

    priv                        = GST_VAAPI_DECODER_MPEG2_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->width                 = 0;
    priv->height                = 0;
    priv->fps_n                 = 0;
    priv->fps_d                 = 0;
    priv->profile               = GST_VAAPI_PROFILE_MPEG2_SIMPLE;
    priv->current_picture       = NULL;
    priv->next_picture          = NULL;
    priv->prev_picture          = NULL;
    priv->adapter               = NULL;
    priv->seq_pts               = GST_CLOCK_TIME_NONE;
    priv->gop_pts               = GST_CLOCK_TIME_NONE;
    priv->pts_diff              = 0;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
    priv->is_first_field        = FALSE;
    priv->has_seq_ext           = FALSE;
    priv->has_seq_scalable_ext  = FALSE;
    priv->has_pic_ext           = FALSE;
    priv->has_quant_matrix_ext  = FALSE;
    priv->size_changed          = FALSE;
    priv->profile_changed       = FALSE;
    priv->quant_matrix_changed  = FALSE;
    priv->progressive_sequence  = FALSE;
    priv->closed_gop            = FALSE;
    priv->broken_link           = FALSE;
}

/**
 * gst_vaapi_decoder_mpeg2_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_mpeg2_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderMpeg2 *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_MPEG2,
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
