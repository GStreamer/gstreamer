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

#include "config.h"
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
    GstBuffer                  *sub_buffer;
    guint                       mb_y;
    guint                       mb_height;
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

static void
gst_vaapi_decoder_mpeg2_close(GstVaapiDecoderMpeg2 *decoder)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    gst_vaapi_picture_replace(&priv->next_picture,    NULL);
    gst_vaapi_picture_replace(&priv->prev_picture,    NULL);

    if (priv->sub_buffer) {
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

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

        if (priv->progressive_sequence)
            priv->mb_height = (priv->height + 15) / 16;
        else
            priv->mb_height = (priv->height + 31) / 32 * 2;
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

    width  = (priv->width  & 0xffff) | ((guint32)seq_ext->horiz_size_ext << 16);
    height = (priv->height & 0xffff) | ((guint32)seq_ext->vert_size_ext  << 16);
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
        status = render_picture(decoder, priv->current_picture);
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

    priv->is_first_field = TRUE;
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

    priv->mb_y = 0;

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

    if (!gst_mpeg_video_parse_picture_extension(pic_ext, buf, buf_size, 0)) {
        GST_DEBUG("failed to parse picture-extension");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }
    priv->has_pic_ext = TRUE;
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
    GstVaapiDecoderStatus status;
    GstBitReader br;
    guint8 slice_vertical_position_extension;
    guint8 quantiser_scale_code;
    guint8 intra_slice_flag, intra_slice = 0;
    guint8 extra_bit_slice, junk8;

    GST_DEBUG("slice %d @ %p, %u bytes)", slice_no, buf, buf_size);

    if (picture->slices->len == 0) {
        if (!fill_picture(decoder, picture))
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

        if (!priv->pic_ext.progressive_frame)
            priv->is_first_field ^= 1;
    }

    priv->mb_y = slice_no;

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

    /* Fill in VASliceParameterBufferMPEG2 */
    slice_param                            = slice->param;
    slice_param->macroblock_offset         = gst_bit_reader_get_pos(&br);
    slice_param->slice_horizontal_position = 0;
    slice_param->slice_vertical_position   = priv->mb_y;
    slice_param->quantiser_scale_code      = quantiser_scale_code;
    slice_param->intra_slice_flag          = intra_slice;

    /* Commit picture for decoding if we reached the last slice */
    if (++priv->mb_y >= priv->mb_height) {
        status = decode_current_picture(decoder);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        GST_DEBUG("done");
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

failed:
    return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
}

static GstVaapiDecoderStatus
decode_chunks(GstVaapiDecoderMpeg2 *decoder, GstBuffer *buffer, GList *chunks)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstMpegVideoTypeOffsetSize *tos;
    GstVaapiDecoderStatus status;
    guchar * const buf = GST_BUFFER_DATA(buffer);
    const guint buf_size = GST_BUFFER_SIZE(buffer);
    guchar *data;
    guint data_size, ofs, pos = 0;
    GList *l;

    status = GST_VAAPI_DECODER_STATUS_SUCCESS;
    for (l = chunks; l; l = g_list_next(l)) {
        tos       = l->data;
        data      = buf + tos->offset;
        data_size = tos->size;
        if (tos->size < 0)
            break;

        ofs = tos->offset - pos + tos->size;
        if (gst_adapter_available(priv->adapter) >= ofs)
            gst_adapter_flush(priv->adapter, ofs);
        pos += ofs;

        switch (tos->type) {
        case GST_MPEG_VIDEO_PACKET_PICTURE:
            status = decode_picture(decoder, data, data_size);
            break;
        case GST_MPEG_VIDEO_PACKET_SEQUENCE:
            status = decode_sequence(decoder, data, data_size);
            break;
        case GST_MPEG_VIDEO_PACKET_EXTENSION: {
            const guchar id = data[0] >> 4;
            switch (id) {
            case GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE:
                status = decode_sequence_ext(decoder, data, data_size);
                break;
            case GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX:
                status = decode_quant_matrix_ext(decoder, data, data_size);
                break;
            case GST_MPEG_VIDEO_PACKET_EXT_PICTURE:
                status = decode_picture_ext(decoder, data, data_size);
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
            status = decode_gop(decoder, data, data_size);
            break;
        case GST_MPEG_VIDEO_PACKET_USER_DATA:
            // Ignore user-data packets
            status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            break;
        default:
            if (tos->type >= GST_MPEG_VIDEO_PACKET_SLICE_MIN &&
                tos->type <= GST_MPEG_VIDEO_PACKET_SLICE_MAX) {
                status = decode_slice(
                    decoder,
                    tos->type - GST_MPEG_VIDEO_PACKET_SLICE_MIN,
                    data, data_size
                );
                break;
            }
            else if (tos->type >= 0xb9 && tos->type <= 0xff) {
                // Ignore system start codes (PES headers)
                status = GST_VAAPI_DECODER_STATUS_SUCCESS;
                break;
            }
            GST_DEBUG("unsupported start code (0x%02x)", tos->type);
            status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
            break;
        }
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            break;
    }

    if (status == GST_VAAPI_DECODER_STATUS_SUCCESS && pos < buf_size)
        priv->sub_buffer = gst_buffer_create_sub(buffer, pos, buf_size - pos);
    return status;
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderMpeg2 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderMpeg2Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    guchar *buf;
    guint buf_size;
    GList *chunks;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return decode_sequence_end(decoder);

    gst_buffer_ref(buffer);
    gst_adapter_push(priv->adapter, buffer);
    if (priv->sub_buffer) {
        buffer = gst_buffer_merge(priv->sub_buffer, buffer);
        if (!buffer)
            return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    chunks   = gst_mpeg_video_parse(buf, buf_size, 0);
    if (!chunks)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    status = decode_chunks(decoder, buffer, chunks);
    g_list_free_full(chunks, (GDestroyNotify)g_free);
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
    priv->sub_buffer            = NULL;
    priv->mb_y                  = 0;
    priv->mb_height             = 0;
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
