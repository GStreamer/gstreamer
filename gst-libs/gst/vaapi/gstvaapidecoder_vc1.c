/*
 *  gstvaapidecoder_vc1.c - VC-1 decoder
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
 * SECTION:gstvaapidecoder_vc1
 * @short_description: VC-1 decoder
 */

#include "sysdeps.h"
#include <string.h>
#include <gst/codecparsers/gstvc1parser.h>
#include "gstvaapidecoder_vc1.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDecoderVC1,
              gst_vaapi_decoder_vc1,
              GST_VAAPI_TYPE_DECODER)

#define GST_VAAPI_DECODER_VC1_GET_PRIVATE(obj)                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_VC1,    \
                                 GstVaapiDecoderVC1Private))

struct _GstVaapiDecoderVC1Private {
    GstVaapiProfile             profile;
    guint                       width;
    guint                       height;
    GstVC1SeqHdr                seq_hdr;
    GstVC1EntryPointHdr         entrypoint_hdr;
    GstVC1FrameHdr              frame_hdr;
    GstVC1BitPlanes            *bitplanes;
    GstVaapiPicture            *current_picture;
    GstVaapiPicture            *next_picture;
    GstVaapiPicture            *prev_picture;
    GstAdapter                 *adapter;
    GstBuffer                  *sub_buffer;
    guint8                     *rbdu_buffer;
    guint                       rbdu_buffer_size;
    guint                       is_constructed          : 1;
    guint                       is_opened               : 1;
    guint                       is_first_field          : 1;
    guint                       has_entrypoint          : 1;
    guint                       size_changed            : 1;
    guint                       profile_changed         : 1;
    guint                       closed_entry            : 1;
    guint                       broken_link             : 1;
};

static GstVaapiDecoderStatus
get_status(GstVC1ParserResult result)
{
    GstVaapiDecoderStatus status;

    switch (result) {
    case GST_VC1_PARSER_OK:
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    case GST_VC1_PARSER_NO_BDU_END:
        status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        break;
    case GST_VC1_PARSER_ERROR:
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    default:
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        break;
    }
    return status;
}

static void
gst_vaapi_decoder_vc1_close(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    gst_vaapi_picture_replace(&priv->next_picture,    NULL);
    gst_vaapi_picture_replace(&priv->prev_picture,    NULL);

    if (priv->sub_buffer) {
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

    if (priv->bitplanes) {
        gst_vc1_bitplanes_free(priv->bitplanes);
        priv->bitplanes = NULL;
    }

    if (priv->adapter) {
        gst_adapter_clear(priv->adapter);
        g_object_unref(priv->adapter);
        priv->adapter = NULL;
    }
}

static gboolean
gst_vaapi_decoder_vc1_open(GstVaapiDecoderVC1 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;

    gst_vaapi_decoder_vc1_close(decoder);

    priv->adapter = gst_adapter_new();
    if (!priv->adapter)
        return FALSE;

    priv->bitplanes = gst_vc1_bitplanes_new();
    if (!priv->bitplanes)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_decoder_vc1_destroy(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;

    gst_vaapi_decoder_vc1_close(decoder);

    if (priv->rbdu_buffer) {
        g_free(priv->rbdu_buffer);
        priv->rbdu_buffer = NULL;
        priv->rbdu_buffer_size = 0;
    }
}

static gboolean
gst_vaapi_decoder_vc1_create(GstVaapiDecoderVC1 *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVaapiProfile profiles[2];
    GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
    guint i, n_profiles = 0;
    gboolean reset_context = FALSE;

    if (priv->profile_changed) {
        GST_DEBUG("profile changed");
        priv->profile_changed = FALSE;
        reset_context         = TRUE;

        profiles[n_profiles++] = priv->profile;
        if (priv->profile == GST_VAAPI_PROFILE_VC1_SIMPLE)
            profiles[n_profiles++] = GST_VAAPI_PROFILE_VC1_MAIN;

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

static inline GstVaapiDecoderStatus
render_picture(GstVaapiDecoderVC1 *decoder, GstVaapiPicture *picture)
{
    if (!gst_vaapi_picture_output(picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_current_picture(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVaapiPicture * const picture = priv->current_picture;
    GstVaapiDecoderStatus status = GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (picture) {
        if (!gst_vaapi_picture_decode(picture))
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
            if (priv->prev_picture && priv->next_picture)
                status = render_picture(decoder, picture);
        }
        gst_vaapi_picture_replace(&priv->current_picture, NULL);
    }
    return status;
}

static GstVaapiDecoderStatus
decode_sequence(GstVaapiDecoderVC1 *decoder, GstVC1BDU *rbdu, GstVC1BDU *ebdu)
{
    GstVaapiDecoder * const base_decoder = GST_VAAPI_DECODER(decoder);
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1AdvancedSeqHdr * const adv_hdr = &seq_hdr->advanced;
    GstVC1SeqStructC * const structc = &seq_hdr->struct_c;
    GstVC1ParserResult result;
    GstVaapiProfile profile;
    guint width, height, fps_n, fps_d, par_n, par_d;

    result = gst_vc1_parse_sequence_header(
        rbdu->data + rbdu->offset,
        rbdu->size,
        seq_hdr
    );
    if (result != GST_VC1_PARSER_OK) {
        GST_DEBUG("failed to parse sequence layer");
        return get_status(result);
    }

    priv->has_entrypoint = FALSE;

    /* Validate profile */
    switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
    case GST_VC1_PROFILE_ADVANCED:
        break;
    default:
        GST_DEBUG("unsupported profile %d", seq_hdr->profile);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    }

    fps_n = 0;
    fps_d = 0;
    par_n = 0;
    par_d = 0;
    switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
        if (structc->wmvp) {
            fps_n = structc->framerate;
            fps_d = 1;
        }
        break;
    case GST_VC1_PROFILE_ADVANCED:
        fps_n = adv_hdr->fps_n;
        fps_d = adv_hdr->fps_d;
        par_n = adv_hdr->par_n;
        par_d = adv_hdr->par_d;
        break;
    default:
        g_assert(0 && "XXX: we already validated the profile above");
        break;
    }

    if (fps_n && fps_d)
        gst_vaapi_decoder_set_framerate(base_decoder, fps_n, fps_d);

    if (par_n > 0 && par_d > 0)
        gst_vaapi_decoder_set_pixel_aspect_ratio(base_decoder, par_n, par_d);

    switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
    case GST_VC1_PROFILE_MAIN:
        width  = seq_hdr->struct_c.coded_width;
        height = seq_hdr->struct_c.coded_height;
        break;
    case GST_VC1_PROFILE_ADVANCED:
        width  = seq_hdr->advanced.max_coded_width;
        height = seq_hdr->advanced.max_coded_height;
        break;
    default:
        g_assert(0 && "XXX: we already validated the profile above");
        break;
    }

    if (priv->width != width) {
        priv->width = width;
        priv->size_changed = TRUE;
    }

    if (priv->height != height) {
        priv->height = height;
        priv->size_changed = TRUE;
    }

    switch (seq_hdr->profile) {
    case GST_VC1_PROFILE_SIMPLE:
        profile = GST_VAAPI_PROFILE_VC1_SIMPLE;
        break;
    case GST_VC1_PROFILE_MAIN:
        profile = GST_VAAPI_PROFILE_VC1_MAIN;
        break;
    case GST_VC1_PROFILE_ADVANCED:
        profile = GST_VAAPI_PROFILE_VC1_ADVANCED;
        break;
    default:
        g_assert(0 && "XXX: we already validated the profile above");
        break;
    }
    if (priv->profile != profile) {
        priv->profile = profile;
        priv->profile_changed = TRUE;
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
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
decode_entry_point(GstVaapiDecoderVC1 *decoder, GstVC1BDU *rbdu, GstVC1BDU *ebdu)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1EntryPointHdr * const entrypoint_hdr = &priv->entrypoint_hdr;
    GstVC1ParserResult result;

    result = gst_vc1_parse_entry_point_header(
        rbdu->data + rbdu->offset,
        rbdu->size,
        entrypoint_hdr,
        seq_hdr
    );
    if (result != GST_VC1_PARSER_OK) {
        GST_DEBUG("failed to parse entrypoint layer");
        return get_status(result);
    }

    if (entrypoint_hdr->coded_size_flag) {
        priv->width        = entrypoint_hdr->coded_width;
        priv->height       = entrypoint_hdr->coded_height;
        priv->size_changed = TRUE;
    }

    priv->has_entrypoint = TRUE;
    priv->closed_entry   = entrypoint_hdr->closed_entry;
    priv->broken_link    = entrypoint_hdr->broken_link;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

/* Reconstruct bitstream PTYPE (7.1.1.4, index into Table-35) */
static guint
get_PTYPE(guint ptype)
{
    switch (ptype) {
    case GST_VC1_PICTURE_TYPE_I:  return 0;
    case GST_VC1_PICTURE_TYPE_P:  return 1;
    case GST_VC1_PICTURE_TYPE_B:  return 2;
    case GST_VC1_PICTURE_TYPE_BI: return 3;
    }
    return 4; /* skipped P-frame */
}

/* Reconstruct bitstream BFRACTION (7.1.1.14, index into Table-40) */
static guint
get_BFRACTION(guint bfraction)
{
    guint i;

    static const struct {
        guint16 index;
        guint16 value;
    }
    bfraction_map[] = {
        {  0,  GST_VC1_BFRACTION_BASIS      / 2 },
        {  1,  GST_VC1_BFRACTION_BASIS      / 3 },
        {  2, (GST_VC1_BFRACTION_BASIS * 2) / 3 },
        {  3,  GST_VC1_BFRACTION_BASIS      / 4 },
        {  4, (GST_VC1_BFRACTION_BASIS * 3) / 4 },
        {  5,  GST_VC1_BFRACTION_BASIS      / 5 },
        {  6, (GST_VC1_BFRACTION_BASIS * 2) / 5 },
        {  7, (GST_VC1_BFRACTION_BASIS * 3) / 5 },
        {  8, (GST_VC1_BFRACTION_BASIS * 4) / 5 },
        {  9,  GST_VC1_BFRACTION_BASIS      / 6 },
        { 10, (GST_VC1_BFRACTION_BASIS * 5) / 6 },
        { 11,  GST_VC1_BFRACTION_BASIS      / 7 },
        { 12, (GST_VC1_BFRACTION_BASIS * 2) / 7 },
        { 13, (GST_VC1_BFRACTION_BASIS * 3) / 7 },
        { 14, (GST_VC1_BFRACTION_BASIS * 4) / 7 },
        { 15, (GST_VC1_BFRACTION_BASIS * 5) / 7 },
        { 16, (GST_VC1_BFRACTION_BASIS * 6) / 7 },
        { 17,  GST_VC1_BFRACTION_BASIS      / 8 },
        { 18, (GST_VC1_BFRACTION_BASIS * 3) / 8 },
        { 19, (GST_VC1_BFRACTION_BASIS * 5) / 8 },
        { 20, (GST_VC1_BFRACTION_BASIS * 7) / 8 },
        { 21,  GST_VC1_BFRACTION_RESERVED },
        { 22,  GST_VC1_BFRACTION_PTYPE_BI }
    };

    if (!bfraction)
        return 0;

    for (i = 0; i < G_N_ELEMENTS(bfraction_map); i++) {
        if (bfraction_map[i].value == bfraction)
            return bfraction_map[i].index;
    }
    return 21; /* RESERVED */
}

/* Translate GStreamer MV modes to VA-API */
static guint
get_VAMvModeVC1(guint mvmode)
{
    switch (mvmode) {
    case GST_VC1_MVMODE_1MV_HPEL_BILINEAR: return VAMvMode1MvHalfPelBilinear;
    case GST_VC1_MVMODE_1MV:               return VAMvMode1Mv;
    case GST_VC1_MVMODE_1MV_HPEL:          return VAMvMode1MvHalfPel;
    case GST_VC1_MVMODE_MIXED_MV:          return VAMvModeMixedMv;
    case GST_VC1_MVMODE_INTENSITY_COMP:    return VAMvModeIntensityCompensation;
    }
    return 0;
}

/* Reconstruct bitstream MVMODE (7.1.1.32) */
static guint
get_MVMODE(GstVC1FrameHdr *frame_hdr)
{
    guint mvmode;

    if (frame_hdr->profile == GST_VC1_PROFILE_ADVANCED)
        mvmode = frame_hdr->pic.advanced.mvmode;
    else
        mvmode = frame_hdr->pic.simple.mvmode;

    if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P ||
        frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B)
        return get_VAMvModeVC1(mvmode);
    return 0;
}

/* Reconstruct bitstream MVMODE2 (7.1.1.33) */
static guint
get_MVMODE2(GstVC1FrameHdr *frame_hdr)
{
    guint mvmode, mvmode2;

    if (frame_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
        mvmode  = frame_hdr->pic.advanced.mvmode;
        mvmode2 = frame_hdr->pic.advanced.mvmode2;
    }
    else {
        mvmode  = frame_hdr->pic.simple.mvmode;
        mvmode2 = frame_hdr->pic.simple.mvmode2;
    }

    if (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P &&
        mvmode == GST_VC1_MVMODE_INTENSITY_COMP)
        return get_VAMvModeVC1(mvmode2);
    return 0;
}

static inline int
has_MVTYPEMB_bitplane(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    guint mvmode, mvmode2;

    if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
        GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;
        if (pic->mvtypemb)
            return 0;
        mvmode  = pic->mvmode;
        mvmode2 = pic->mvmode2;
    }
    else {
        GstVC1PicSimpleMain * const pic = &frame_hdr->pic.simple;
        if (pic->mvtypemb)
            return 0;
        mvmode  = pic->mvmode;
        mvmode2 = pic->mvmode2;
    }
    return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P &&
            (mvmode == GST_VC1_MVMODE_MIXED_MV ||
             (mvmode == GST_VC1_MVMODE_INTENSITY_COMP &&
              mvmode2 == GST_VC1_MVMODE_MIXED_MV)));
}

static inline int
has_SKIPMB_bitplane(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;

    if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
        GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;
        if (pic->skipmb)
            return 0;
    }
    else {
        GstVC1PicSimpleMain * const pic = &frame_hdr->pic.simple;
        if (pic->skipmb)
            return 0;
    }
    return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_P ||
            frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B);
}

static inline int
has_DIRECTMB_bitplane(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;

    if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
        GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;
        if (pic->directmb)
            return 0;
    }
    else {
        GstVC1PicSimpleMain * const pic = &frame_hdr->pic.simple;
        if (pic->directmb)
            return 0;
    }
    return frame_hdr->ptype == GST_VC1_PICTURE_TYPE_B;
}

static inline int
has_ACPRED_bitplane(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;

    if (seq_hdr->profile != GST_VC1_PROFILE_ADVANCED)
        return 0;
    if (pic->acpred)
        return 0;
    return (frame_hdr->ptype == GST_VC1_PICTURE_TYPE_I ||
            frame_hdr->ptype == GST_VC1_PICTURE_TYPE_BI);
}

static inline int
has_OVERFLAGS_bitplane(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1EntryPointHdr * const entrypoint_hdr = &priv->entrypoint_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;

    if (seq_hdr->profile != GST_VC1_PROFILE_ADVANCED)
        return 0;
    if (pic->overflags)
        return 0;
    return ((frame_hdr->ptype == GST_VC1_PICTURE_TYPE_I ||
             frame_hdr->ptype == GST_VC1_PICTURE_TYPE_BI) &&
            (entrypoint_hdr->overlap && frame_hdr->pquant <= 8) &&
            pic->condover == GST_VC1_CONDOVER_SELECT);
}

static inline void
pack_bitplanes(GstVaapiBitPlane *bitplane, guint n, const guint8 *bitplanes[3], guint x, guint y, guint stride)
{
    const guint dst_index = n / 2;
    const guint src_index = y * stride + x;
    guint8 v = 0;

    if (bitplanes[0])
        v |= bitplanes[0][src_index];
    if (bitplanes[1])
        v |= bitplanes[1][src_index] << 1;
    if (bitplanes[2])
        v |= bitplanes[2][src_index] << 2;
    bitplane->data[dst_index] = (bitplane->data[dst_index] << 4) | v;
}

static gboolean
fill_picture_structc(GstVaapiDecoderVC1 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    VAPictureParameterBufferVC1 * const pic_param = picture->param;
    GstVC1SeqStructC * const structc = &priv->seq_hdr.struct_c;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVC1PicSimpleMain * const pic = &frame_hdr->pic.simple;

    /* Fill in VAPictureParameterBufferVC1 (simple/main profile bits) */
    pic_param->sequence_fields.bits.finterpflag                     = structc->finterpflag;
    pic_param->sequence_fields.bits.multires                        = structc->multires;
    pic_param->sequence_fields.bits.overlap                         = structc->overlap;
    pic_param->sequence_fields.bits.syncmarker                      = structc->syncmarker;
    pic_param->sequence_fields.bits.rangered                        = structc->rangered;
    pic_param->sequence_fields.bits.max_b_frames                    = structc->maxbframes;
    pic_param->conditional_overlap_flag                             = 0; /* advanced profile only */
    pic_param->fast_uvmc_flag                                       = structc->fastuvmc;
    pic_param->b_picture_fraction                                   = get_BFRACTION(pic->bfraction);
    pic_param->cbp_table                                            = pic->cbptab;
    pic_param->mb_mode_table                                        = 0; /* XXX: interlaced frame */
    pic_param->range_reduction_frame                                = pic->rangeredfrm;
    pic_param->rounding_control                                     = 0; /* advanced profile only */
    pic_param->post_processing                                      = 0; /* advanced profile only */
    pic_param->picture_resolution_index                             = pic->respic;
    pic_param->luma_scale                                           = pic->lumscale;
    pic_param->luma_shift                                           = pic->lumshift;
    pic_param->raw_coding.flags.mv_type_mb                          = pic->mvtypemb;
    pic_param->raw_coding.flags.direct_mb                           = pic->directmb;
    pic_param->raw_coding.flags.skip_mb                             = pic->skipmb;
    pic_param->bitplane_present.flags.bp_mv_type_mb                 = has_MVTYPEMB_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_direct_mb                  = has_DIRECTMB_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_skip_mb                    = has_SKIPMB_bitplane(decoder);
    pic_param->mv_fields.bits.mv_table                              = pic->mvtab;
    pic_param->mv_fields.bits.extended_mv_flag                      = structc->extended_mv;
    pic_param->mv_fields.bits.extended_mv_range                     = pic->mvrange;
    pic_param->transform_fields.bits.variable_sized_transform_flag  = structc->vstransform;
    pic_param->transform_fields.bits.mb_level_transform_type_flag   = pic->ttmbf;
    pic_param->transform_fields.bits.frame_level_transform_type     = pic->ttfrm;
    pic_param->transform_fields.bits.transform_ac_codingset_idx2    = pic->transacfrm2;
    return TRUE;
}

static gboolean
fill_picture_advanced(GstVaapiDecoderVC1 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    VAPictureParameterBufferVC1 * const pic_param = picture->param;
    GstVC1AdvancedSeqHdr * const adv_hdr = &priv->seq_hdr.advanced;
    GstVC1EntryPointHdr * const entrypoint_hdr = &priv->entrypoint_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVC1PicAdvanced * const pic = &frame_hdr->pic.advanced;

    if (!priv->has_entrypoint)
        return FALSE;

    /* Fill in VAPictureParameterBufferVC1 (advanced profile bits) */
    pic_param->sequence_fields.bits.pulldown                        = adv_hdr->pulldown;
    pic_param->sequence_fields.bits.interlace                       = adv_hdr->interlace;
    pic_param->sequence_fields.bits.tfcntrflag                      = adv_hdr->tfcntrflag;
    pic_param->sequence_fields.bits.finterpflag                     = adv_hdr->finterpflag;
    pic_param->sequence_fields.bits.psf                             = adv_hdr->psf;
    pic_param->sequence_fields.bits.overlap                         = entrypoint_hdr->overlap;
    pic_param->entrypoint_fields.bits.broken_link                   = entrypoint_hdr->broken_link;
    pic_param->entrypoint_fields.bits.closed_entry                  = entrypoint_hdr->closed_entry;
    pic_param->entrypoint_fields.bits.panscan_flag                  = entrypoint_hdr->panscan_flag;
    pic_param->entrypoint_fields.bits.loopfilter                    = entrypoint_hdr->loopfilter;
    pic_param->conditional_overlap_flag                             = pic->condover;
    pic_param->fast_uvmc_flag                                       = entrypoint_hdr->fastuvmc;
    pic_param->range_mapping_fields.bits.luma_flag                  = entrypoint_hdr->range_mapy_flag;
    pic_param->range_mapping_fields.bits.luma                       = entrypoint_hdr->range_mapy;
    pic_param->range_mapping_fields.bits.chroma_flag                = entrypoint_hdr->range_mapuv_flag;
    pic_param->range_mapping_fields.bits.chroma                     = entrypoint_hdr->range_mapuv;
    pic_param->b_picture_fraction                                   = get_BFRACTION(pic->bfraction);
    pic_param->cbp_table                                            = pic->cbptab;
    pic_param->mb_mode_table                                        = 0; /* XXX: interlaced frame */
    pic_param->range_reduction_frame                                = 0; /* simple/main profile only */
    pic_param->rounding_control                                     = pic->rndctrl;
    pic_param->post_processing                                      = pic->postproc;
    pic_param->picture_resolution_index                             = 0; /* simple/main profile only */
    pic_param->luma_scale                                           = pic->lumscale;
    pic_param->luma_shift                                           = pic->lumshift;
    pic_param->picture_fields.bits.frame_coding_mode                = pic->fcm;
    pic_param->picture_fields.bits.top_field_first                  = pic->tff;
    pic_param->picture_fields.bits.is_first_field                   = pic->fcm == 0; /* XXX: interlaced frame */
    pic_param->picture_fields.bits.intensity_compensation           = pic->mvmode == GST_VC1_MVMODE_INTENSITY_COMP;
    pic_param->raw_coding.flags.mv_type_mb                          = pic->mvtypemb;
    pic_param->raw_coding.flags.direct_mb                           = pic->directmb;
    pic_param->raw_coding.flags.skip_mb                             = pic->skipmb;
    pic_param->raw_coding.flags.ac_pred                             = pic->acpred;
    pic_param->raw_coding.flags.overflags                           = pic->overflags;
    pic_param->bitplane_present.flags.bp_mv_type_mb                 = has_MVTYPEMB_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_direct_mb                  = has_DIRECTMB_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_skip_mb                    = has_SKIPMB_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_ac_pred                    = has_ACPRED_bitplane(decoder);
    pic_param->bitplane_present.flags.bp_overflags                  = has_OVERFLAGS_bitplane(decoder);
    pic_param->reference_fields.bits.reference_distance_flag        = entrypoint_hdr->refdist_flag;
    pic_param->mv_fields.bits.mv_table                              = pic->mvtab;
    pic_param->mv_fields.bits.extended_mv_flag                      = entrypoint_hdr->extended_mv;
    pic_param->mv_fields.bits.extended_mv_range                     = pic->mvrange;
    pic_param->mv_fields.bits.extended_dmv_flag                     = entrypoint_hdr->extended_dmv;
    pic_param->pic_quantizer_fields.bits.dquant                     = entrypoint_hdr->dquant;
    pic_param->pic_quantizer_fields.bits.quantizer                  = entrypoint_hdr->quantizer;
    pic_param->transform_fields.bits.variable_sized_transform_flag  = entrypoint_hdr->vstransform;
    pic_param->transform_fields.bits.mb_level_transform_type_flag   = pic->ttmbf;
    pic_param->transform_fields.bits.frame_level_transform_type     = pic->ttfrm;
    pic_param->transform_fields.bits.transform_ac_codingset_idx2    = pic->transacfrm2;
    return TRUE;
}

static gboolean
fill_picture(GstVaapiDecoderVC1 *decoder, GstVaapiPicture *picture)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    VAPictureParameterBufferVC1 * const pic_param = picture->param;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;

    /* Fill in VAPictureParameterBufferVC1 (common fields) */
    pic_param->forward_reference_picture                            = VA_INVALID_ID;
    pic_param->backward_reference_picture                           = VA_INVALID_ID;
    pic_param->inloop_decoded_picture                               = VA_INVALID_ID;
    pic_param->sequence_fields.value                                = 0;
#if VA_CHECK_VERSION(0,32,0)
    pic_param->sequence_fields.bits.profile                         = seq_hdr->profile;
#endif
    pic_param->coded_width                                          = priv->width;
    pic_param->coded_height                                         = priv->height;
    pic_param->entrypoint_fields.value                              = 0;
    pic_param->range_mapping_fields.value                           = 0;
    pic_param->picture_fields.value                                 = 0;
    pic_param->picture_fields.bits.picture_type                     = get_PTYPE(frame_hdr->ptype);
    pic_param->raw_coding.value                                     = 0;
    pic_param->bitplane_present.value                               = 0;
    pic_param->reference_fields.value                               = 0;
    pic_param->mv_fields.value                                      = 0;
    pic_param->mv_fields.bits.mv_mode                               = get_MVMODE(frame_hdr);
    pic_param->mv_fields.bits.mv_mode2                              = get_MVMODE2(frame_hdr);
    pic_param->pic_quantizer_fields.value                           = 0;
    pic_param->pic_quantizer_fields.bits.half_qp                    = frame_hdr->halfqp;
    pic_param->pic_quantizer_fields.bits.pic_quantizer_scale        = frame_hdr->pquant;
    pic_param->pic_quantizer_fields.bits.pic_quantizer_type         = frame_hdr->pquantizer;
    pic_param->pic_quantizer_fields.bits.dq_frame                   = frame_hdr->vopdquant.dquantfrm;
    pic_param->pic_quantizer_fields.bits.dq_profile                 = frame_hdr->vopdquant.dqprofile;
    pic_param->pic_quantizer_fields.bits.dq_sb_edge                 = frame_hdr->vopdquant.dqsbedge;
    pic_param->pic_quantizer_fields.bits.dq_db_edge                 = frame_hdr->vopdquant.dqsbedge;
    pic_param->pic_quantizer_fields.bits.dq_binary_level            = frame_hdr->vopdquant.dqbilevel;
    pic_param->pic_quantizer_fields.bits.alt_pic_quantizer          = frame_hdr->vopdquant.altpquant;
    pic_param->transform_fields.value                               = 0;
    pic_param->transform_fields.bits.transform_ac_codingset_idx1    = frame_hdr->transacfrm;
    pic_param->transform_fields.bits.intra_transform_dc_table       = frame_hdr->transdctab;

    if (seq_hdr->profile == GST_VC1_PROFILE_ADVANCED) {
        if (!fill_picture_advanced(decoder, picture))
            return FALSE;
    }
    else {
        if (!fill_picture_structc(decoder, picture))
            return FALSE;
    }

    switch (picture->type) {
    case GST_VAAPI_PICTURE_TYPE_B:
        if (priv->next_picture)
            pic_param->backward_reference_picture = priv->next_picture->surface_id;
        // fall-through
    case GST_VAAPI_PICTURE_TYPE_P:
        if (priv->prev_picture)
            pic_param->forward_reference_picture = priv->prev_picture->surface_id;
        break;
    default:
        break;
    }

    if (pic_param->bitplane_present.value) {
        const guint8 *bitplanes[3];
        guint x, y, n;

        switch (picture->type) {
        case GST_VAAPI_PICTURE_TYPE_P:
            bitplanes[0] = pic_param->bitplane_present.flags.bp_direct_mb  ? priv->bitplanes->directmb  : NULL;
            bitplanes[1] = pic_param->bitplane_present.flags.bp_skip_mb    ? priv->bitplanes->skipmb    : NULL;
            bitplanes[2] = pic_param->bitplane_present.flags.bp_mv_type_mb ? priv->bitplanes->mvtypemb  : NULL;
            break;
        case GST_VAAPI_PICTURE_TYPE_B:
            bitplanes[0] = pic_param->bitplane_present.flags.bp_direct_mb  ? priv->bitplanes->directmb  : NULL;
            bitplanes[1] = pic_param->bitplane_present.flags.bp_skip_mb    ? priv->bitplanes->skipmb    : NULL;
            bitplanes[2] = NULL; /* XXX: interlaced frame (FORWARD plane) */
            break;
        case GST_VAAPI_PICTURE_TYPE_BI:
        case GST_VAAPI_PICTURE_TYPE_I:
            bitplanes[0] = NULL; /* XXX: interlaced frame (FIELDTX plane) */
            bitplanes[1] = pic_param->bitplane_present.flags.bp_ac_pred    ? priv->bitplanes->acpred    : NULL;
            bitplanes[2] = pic_param->bitplane_present.flags.bp_overflags  ? priv->bitplanes->overflags : NULL;
            break;
        default:
            bitplanes[0] = NULL;
            bitplanes[1] = NULL;
            bitplanes[2] = NULL;
            break;
        }

        picture->bitplane = GST_VAAPI_BITPLANE_NEW(
            decoder,
            (seq_hdr->mb_width * seq_hdr->mb_height + 1) / 2
        );
        if (!picture->bitplane)
            return FALSE;

        n = 0;
        for (y = 0; y < seq_hdr->mb_height; y++)
            for (x = 0; x < seq_hdr->mb_width; x++, n++)
                pack_bitplanes(picture->bitplane, n, bitplanes, x, y, seq_hdr->mb_stride);
        if (n & 1) /* move last nibble to the high order */
            picture->bitplane->data[n/2] <<= 4;
    }
    return TRUE;
}

static GstVaapiDecoderStatus
decode_frame(GstVaapiDecoderVC1 *decoder, GstVC1BDU *rbdu, GstVC1BDU *ebdu)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVC1FrameHdr * const frame_hdr = &priv->frame_hdr;
    GstVC1ParserResult result;
    GstVaapiPicture *picture;
    GstVaapiSlice *slice;
    GstVaapiDecoderStatus status;
    VASliceParameterBufferVC1 *slice_param;
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

    priv->current_picture = GST_VAAPI_PICTURE_NEW(VC1, decoder);
    if (!priv->current_picture) {
        GST_DEBUG("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    picture = priv->current_picture;

    if (!gst_vc1_bitplanes_ensure_size(priv->bitplanes, seq_hdr)) {
        GST_DEBUG("failed to allocate bitplanes");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }

    memset(frame_hdr, 0, sizeof(*frame_hdr));
    result = gst_vc1_parse_frame_header(
        rbdu->data + rbdu->offset,
        rbdu->size,
        frame_hdr,
        seq_hdr,
        priv->bitplanes
    );
    if (result != GST_VC1_PARSER_OK) {
        GST_DEBUG("failed to parse frame layer");
        return get_status(result);
    }

    switch (frame_hdr->ptype) {
    case GST_VC1_PICTURE_TYPE_I:
        picture->type   = GST_VAAPI_PICTURE_TYPE_I;
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        break;
    case GST_VC1_PICTURE_TYPE_SKIPPED:
    case GST_VC1_PICTURE_TYPE_P:
        picture->type   = GST_VAAPI_PICTURE_TYPE_P;
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        break;
    case GST_VC1_PICTURE_TYPE_B:
        picture->type   = GST_VAAPI_PICTURE_TYPE_B;
        break;
    case GST_VC1_PICTURE_TYPE_BI:
        picture->type   = GST_VAAPI_PICTURE_TYPE_BI;
        break;
    default:
        GST_DEBUG("unsupported picture type %d", frame_hdr->ptype);
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    /* Update presentation time */
    pts = gst_adapter_prev_timestamp(priv->adapter, NULL);
    picture->pts = pts;

    /* Update reference pictures */
    if (GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
        if (priv->next_picture)
            status = render_picture(decoder, priv->next_picture);
        gst_vaapi_picture_replace(&priv->prev_picture, priv->next_picture);
        gst_vaapi_picture_replace(&priv->next_picture, picture);
    }

    if (!fill_picture(decoder, picture))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    slice = GST_VAAPI_SLICE_NEW(
        VC1,
        decoder,
        ebdu->data + ebdu->sc_offset,
        ebdu->size + ebdu->offset - ebdu->sc_offset
    );
    if (!slice) {
        GST_DEBUG("failed to allocate slice");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    gst_vaapi_picture_add_slice(picture, slice);

    /* Fill in VASliceParameterBufferVC1 */
    slice_param                            = slice->param;
    slice_param->macroblock_offset         = 8 * (ebdu->offset - ebdu->sc_offset) + frame_hdr->header_size;
    slice_param->slice_vertical_position   = 0;

    /* Decode picture right away, we got the full frame */
    return decode_current_picture(decoder);
}

static gboolean
decode_rbdu(GstVaapiDecoderVC1 *decoder, GstVC1BDU *rbdu, GstVC1BDU *ebdu)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    guint8 *rbdu_buffer;
    guint i, j, rbdu_buffer_size;

    /* BDU are encapsulated in advanced profile mode only */
    if (priv->profile != GST_VAAPI_PROFILE_VC1_ADVANCED) {
        memcpy(rbdu, ebdu, sizeof(*rbdu));
        return TRUE;
    }

    /* Reallocate unescaped bitstream buffer */
    rbdu_buffer = priv->rbdu_buffer;
    if (!rbdu_buffer || ebdu->size > priv->rbdu_buffer_size) {
        rbdu_buffer = g_realloc(priv->rbdu_buffer, ebdu->size);
        if (!rbdu_buffer)
            return FALSE;
        priv->rbdu_buffer = rbdu_buffer;
        priv->rbdu_buffer_size = ebdu->size;
    }

    /* Unescape bitstream buffer */
    if (ebdu->size < 4) {
        memcpy(rbdu_buffer, ebdu->data + ebdu->offset, ebdu->size);
        rbdu_buffer_size = ebdu->size;
    }
    else {
        guint8 * const bdu_buffer = ebdu->data + ebdu->offset;
        for (i = 0, j = 0; i < ebdu->size; i++) {
            if (i >= 2 && i < ebdu->size - 1 &&
                bdu_buffer[i - 1] == 0x00   &&
                bdu_buffer[i - 2] == 0x00   &&
                bdu_buffer[i    ] == 0x03   &&
                bdu_buffer[i + 1] <= 0x03)
                i++;
            rbdu_buffer[j++] = bdu_buffer[i];
        }
        rbdu_buffer_size = j;
    }

    /* Reconstruct RBDU */
    rbdu->type      = ebdu->type;
    rbdu->size      = rbdu_buffer_size;
    rbdu->sc_offset = 0;
    rbdu->offset    = 0;
    rbdu->data      = rbdu_buffer;
    return TRUE;
}

static GstVaapiDecoderStatus
decode_ebdu(GstVaapiDecoderVC1 *decoder, GstVC1BDU *ebdu)
{
    GstVaapiDecoderStatus status;
    GstVC1BDU rbdu;

    if (!decode_rbdu(decoder, &rbdu, ebdu))
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;

    switch (ebdu->type) {
    case GST_VC1_SEQUENCE:
        status = decode_sequence(decoder, &rbdu, ebdu);
        break;
    case GST_VC1_ENTRYPOINT:
        status = decode_entry_point(decoder, &rbdu, ebdu);
        break;
    case GST_VC1_FRAME:
        status = decode_frame(decoder, &rbdu, ebdu);
        break;
    case GST_VC1_SLICE:
        GST_DEBUG("decode slice");
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    case GST_VC1_END_OF_SEQ:
        status = decode_sequence_end(decoder);
        break;
    default:
        GST_DEBUG("unsupported BDU type %d", ebdu->type);
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    }
    return status;
}

static GstVaapiDecoderStatus
decode_buffer(GstVaapiDecoderVC1 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstVC1ParserResult result;
    GstVC1BDU ebdu;
    GstBuffer *codec_data;
    guchar *buf;
    guint buf_size, ofs;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return decode_sequence_end(decoder);

    gst_buffer_ref(buffer);
    gst_adapter_push(priv->adapter, buffer);

    /* Assume demuxer sends out plain frames if codec-data */
    codec_data = GST_VAAPI_DECODER_CODEC_DATA(decoder);
    if (codec_data && codec_data != buffer) {
        ebdu.type      = GST_VC1_FRAME;
        ebdu.size      = buf_size;
        ebdu.sc_offset = 0;
        ebdu.offset    = 0;
        ebdu.data      = buf;
        status = decode_ebdu(decoder, &ebdu);

        if (gst_adapter_available(priv->adapter) >= buf_size)
            gst_adapter_flush(priv->adapter, buf_size);
        return status;
    }

    if (priv->sub_buffer) {
        buffer = gst_buffer_merge(priv->sub_buffer, buffer);
        if (!buffer)
            return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
        gst_buffer_unref(priv->sub_buffer);
        priv->sub_buffer = NULL;
    }

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    ofs      = 0;
    do {
        result = gst_vc1_identify_next_bdu(
            buf + ofs,
            buf_size - ofs,
            &ebdu
        );
        status = get_status(result);

        if (status == GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA) {
            priv->sub_buffer = gst_buffer_create_sub(buffer, ofs, buf_size - ofs);
            break;
        }
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            break;

        ofs += ebdu.offset + ebdu.size;
        if (gst_adapter_available(priv->adapter) >= ebdu.offset)
            gst_adapter_flush(priv->adapter, ebdu.offset);

        status = decode_ebdu(decoder, &ebdu);
        if (gst_adapter_available(priv->adapter) >= ebdu.size)
            gst_adapter_flush(priv->adapter, ebdu.size);
    } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
    return status;
}

static GstVaapiDecoderStatus
decode_codec_data(GstVaapiDecoderVC1 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVC1SeqHdr * const seq_hdr = &priv->seq_hdr;
    GstVaapiDecoderStatus status;
    GstVC1ParserResult result;
    GstVC1BDU ebdu;
    GstCaps *caps;
    GstStructure *structure;
    guchar *buf;
    guint buf_size, ofs;
    gint width, height;
    guint32 format;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf || buf_size == 0)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    caps      = GST_VAAPI_DECODER_CAST(decoder)->priv->caps;
    structure = gst_caps_get_structure(caps, 0);

    if (!gst_structure_get_int(structure, "width", &width) ||
        !gst_structure_get_int(structure, "height", &height)) {
        GST_DEBUG("failed to parse size from codec-data");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    }

    if (!gst_structure_get_fourcc(structure, "format", &format)) {
        GST_DEBUG("failed to parse profile from codec-data");
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;
    }

    /* WMV3 -- expecting sequence header */
    if (format == GST_MAKE_FOURCC('W','M','V','3')) {
        seq_hdr->struct_c.coded_width  = width;
        seq_hdr->struct_c.coded_height = height;
        ebdu.type      = GST_VC1_SEQUENCE;
        ebdu.size      = buf_size;
        ebdu.sc_offset = 0;
        ebdu.offset    = 0;
        ebdu.data      = buf;
        return decode_ebdu(decoder, &ebdu);
    }

    /* WVC1 -- expecting bitstream data units */
    if (format != GST_MAKE_FOURCC('W','V','C','1'))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
    seq_hdr->advanced.max_coded_width  = width;
    seq_hdr->advanced.max_coded_height = height;

    ofs = 0;
    do {
        result = gst_vc1_identify_next_bdu(
            buf + ofs,
            buf_size - ofs,
            &ebdu
        );

        switch (result) {
        case GST_VC1_PARSER_NO_BDU_END:
            /* Assume the EBDU is complete within codec-data bounds */
            ebdu.size = buf_size - ofs - (ebdu.offset - ebdu.sc_offset);
            // fall-through
        case GST_VC1_PARSER_OK:
            status = decode_ebdu(decoder, &ebdu);
            ofs += ebdu.offset + ebdu.size;
            break;
        default:
            status = get_status(result);
            break;
        }
    } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS && ofs < buf_size);
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_vc1_decode(GstVaapiDecoder *base, GstBuffer *buffer)
{
    GstVaapiDecoderVC1 * const decoder = GST_VAAPI_DECODER_VC1(base);
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstBuffer *codec_data;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_vc1_open(decoder, buffer);
        if (!priv->is_opened)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC;

        codec_data = GST_VAAPI_DECODER_CODEC_DATA(decoder);
        if (codec_data) {
            status = decode_codec_data(decoder, codec_data);
            if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
                return status;
        }
    }
    return decode_buffer(decoder, buffer);
}

static void
gst_vaapi_decoder_vc1_finalize(GObject *object)
{
    GstVaapiDecoderVC1 * const decoder = GST_VAAPI_DECODER_VC1(object);

    gst_vaapi_decoder_vc1_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_vc1_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_vc1_constructed(GObject *object)
{
    GstVaapiDecoderVC1 * const decoder = GST_VAAPI_DECODER_VC1(object);
    GstVaapiDecoderVC1Private * const priv = decoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_vc1_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_vc1_create(decoder);
}

static void
gst_vaapi_decoder_vc1_class_init(GstVaapiDecoderVC1Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderVC1Private));

    object_class->finalize      = gst_vaapi_decoder_vc1_finalize;
    object_class->constructed   = gst_vaapi_decoder_vc1_constructed;

    decoder_class->decode       = gst_vaapi_decoder_vc1_decode;
}

static void
gst_vaapi_decoder_vc1_init(GstVaapiDecoderVC1 *decoder)
{
    GstVaapiDecoderVC1Private *priv;

    priv                        = GST_VAAPI_DECODER_VC1_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->width                 = 0;
    priv->height                = 0;
    priv->profile               = (GstVaapiProfile)0;
    priv->current_picture       = NULL;
    priv->next_picture          = NULL;
    priv->prev_picture          = NULL;
    priv->adapter               = NULL;
    priv->sub_buffer            = NULL;
    priv->rbdu_buffer           = NULL;
    priv->rbdu_buffer_size      = 0;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
    priv->is_first_field        = FALSE;
    priv->has_entrypoint        = FALSE;
    priv->size_changed          = FALSE;
    priv->profile_changed       = FALSE;
    priv->closed_entry          = FALSE;
    priv->broken_link           = FALSE;
}

/**
 * gst_vaapi_decoder_vc1_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for VC-1 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_vc1_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderVC1 *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_VC1,
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
