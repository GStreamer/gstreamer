/*
 *  gstvaapidecoder_h264.c - H.264 decoder
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
 * SECTION:gstvaapidecoder_h264
 * @short_description: H.264 decoder
 */

#include "sysdeps.h"
#include <string.h>
#include <gst/base/gstadapter.h>
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapidecoder_h264.h"
#include "gstvaapidecoder_objects.h"
#include "gstvaapidecoder_priv.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiPictureH264             GstVaapiPictureH264;
typedef struct _GstVaapiPictureH264Class        GstVaapiPictureH264Class;
typedef struct _GstVaapiSliceH264               GstVaapiSliceH264;
typedef struct _GstVaapiSliceH264Class          GstVaapiSliceH264Class;

/* ------------------------------------------------------------------------- */
/* --- H.264 Pictures                                                    --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_PICTURE_H264 \
    (gst_vaapi_picture_h264_get_type())

#define GST_VAAPI_PICTURE_H264_CAST(obj) \
    ((GstVaapiPictureH264 *)(obj))

#define GST_VAAPI_PICTURE_H264(obj)                             \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_PICTURE_H264,    \
                                GstVaapiPictureH264))

#define GST_VAAPI_PICTURE_H264_CLASS(klass)                     \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_PICTURE_H264,       \
                             GstVaapiPictureH264Class))

#define GST_VAAPI_IS_PICTURE_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_PICTURE_H264))

#define GST_VAAPI_IS_PICTURE_H264_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_PICTURE_H264))

#define GST_VAAPI_PICTURE_H264_GET_CLASS(obj)                   \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_PICTURE_H264,     \
                               GstVaapiPictureH264Class))

struct _GstVaapiPictureH264 {
    GstVaapiPicture             base;
    VAPictureH264               info;
    gint32                      poc;
    gint32                      frame_num;              // Original frame_num from slice_header()
    gint32                      frame_num_wrap;         // Temporary for ref pic marking: FrameNumWrap
    gint32                      pic_num;                // Temporary for ref pic marking: PicNum
    gint32                      long_term_pic_num;      // Temporary for ref pic marking: LongTermPicNum
    guint                       is_idr                  : 1;
    guint                       is_long_term            : 1;
    guint                       field_pic_flag          : 1;
    guint                       bottom_field_flag       : 1;
    guint                       has_mmco_5              : 1;
    guint                       output_flag             : 1;
    guint                       output_needed           : 1;
};

struct _GstVaapiPictureH264Class {
    /*< private >*/
    GstVaapiPictureClass        parent_class;
};

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiPictureH264,
                            gst_vaapi_picture_h264,
                            GST_VAAPI_TYPE_PICTURE)

static void
gst_vaapi_picture_h264_destroy(GstVaapiPictureH264 *decoder)
{
}

static gboolean
gst_vaapi_picture_h264_create(
    GstVaapiPictureH264                      *picture,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return TRUE;
}

static void
gst_vaapi_picture_h264_init(GstVaapiPictureH264 *picture)
{
    VAPictureH264 *va_pic;

    va_pic                      = &picture->info;
    va_pic->flags               = 0;
    va_pic->TopFieldOrderCnt    = 0;
    va_pic->BottomFieldOrderCnt = 0;

    picture->poc                = 0;
    picture->is_long_term       = FALSE;
    picture->is_idr             = FALSE;
    picture->has_mmco_5         = FALSE;
    picture->output_needed      = FALSE;
}

static inline GstVaapiPictureH264 *
gst_vaapi_picture_h264_new(GstVaapiDecoderH264 *decoder)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_PICTURE_H264,
        GST_VAAPI_CODEC_BASE(decoder),
        NULL, sizeof(VAPictureParameterBufferH264),
        NULL, 0
    );
    if (!object)
        return NULL;
    return GST_VAAPI_PICTURE_H264_CAST(object);
}

/* ------------------------------------------------------------------------- */
/* --- Slices                                                            --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_SLICE_H264 \
    (gst_vaapi_slice_h264_get_type())

#define GST_VAAPI_SLICE_H264_CAST(obj) \
    ((GstVaapiSliceH264 *)(obj))

#define GST_VAAPI_SLICE_H264(obj)                               \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_SLICE_H264,      \
                                GstVaapiSliceH264))

#define GST_VAAPI_SLICE_H264_CLASS(klass)                       \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_SLICE_H264,         \
                             GstVaapiSliceH264Class))

#define GST_VAAPI_IS_SLICE_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SLICE_H264))

#define GST_VAAPI_IS_SLICE_H264_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SLICE_H264))

#define GST_VAAPI_SLICE_H264_GET_CLASS(obj)                     \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_SLICE_H264,       \
                               GstVaapiSliceH264Class))

struct _GstVaapiSliceH264 {
    GstVaapiSlice               base;
    GstH264SliceHdr             slice_hdr;              // parsed slice_header()
};

struct _GstVaapiSliceH264Class {
    /*< private >*/
    GstVaapiSliceClass          parent_class;
};

GST_VAAPI_CODEC_DEFINE_TYPE(GstVaapiSliceH264,
                            gst_vaapi_slice_h264,
                            GST_VAAPI_TYPE_SLICE)

static void
gst_vaapi_slice_h264_destroy(GstVaapiSliceH264 *slice)
{
}

static gboolean
gst_vaapi_slice_h264_create(
    GstVaapiSliceH264                        *slice,
    const GstVaapiCodecObjectConstructorArgs *args
)
{
    return TRUE;
}

static void
gst_vaapi_slice_h264_init(GstVaapiSliceH264 *slice)
{
}

static inline GstVaapiSliceH264 *
gst_vaapi_slice_h264_new(
    GstVaapiDecoderH264 *decoder,
    const guint8        *data,
    guint                data_size
)
{
    GstVaapiCodecObject *object;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    object = gst_vaapi_codec_object_new(
        GST_VAAPI_TYPE_SLICE_H264,
        GST_VAAPI_CODEC_BASE(decoder),
        NULL, sizeof(VASliceParameterBufferH264),
        data, data_size
    );
    if (!object)
        return NULL;
    return GST_VAAPI_SLICE_H264_CAST(object);
}

/* ------------------------------------------------------------------------- */
/* --- H.264 Decoder                                                     --- */
/* ------------------------------------------------------------------------- */

G_DEFINE_TYPE(GstVaapiDecoderH264,
              gst_vaapi_decoder_h264,
              GST_VAAPI_TYPE_DECODER)

#define GST_VAAPI_DECODER_H264_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER_H264,   \
                                 GstVaapiDecoderH264Private))

// Used for field_poc[]
#define TOP_FIELD       0
#define BOTTOM_FIELD    1

struct _GstVaapiDecoderH264Private {
    GstAdapter                 *adapter;
    GstH264NalParser           *parser;
    GstH264SPS                 *sps;
    GstH264SPS                  last_sps;
    GstH264PPS                 *pps;
    GstH264PPS                  last_pps;
    GstVaapiPictureH264        *current_picture;
    GstVaapiPictureH264        *dpb[16];
    guint                       dpb_count;
    guint                       dpb_size;
    GstVaapiProfile             profile;
    GstVaapiPictureH264        *short_ref[32];
    guint                       short_ref_count;
    GstVaapiPictureH264        *long_ref[32];
    guint                       long_ref_count;
    GstVaapiPictureH264        *RefPicList0[32];
    guint                       RefPicList0_count;
    GstVaapiPictureH264        *RefPicList1[32];
    guint                       RefPicList1_count;
    guint                       nal_length_size;
    guint                       width;
    guint                       height;
    guint                       mb_x;
    guint                       mb_y;
    guint                       mb_width;
    guint                       mb_height;
    guint8                      scaling_list_4x4[6][16];
    guint8                      scaling_list_8x8[6][64];
    gint32                      field_poc[2];           // 0:TopFieldOrderCnt / 1:BottomFieldOrderCnt
    gint32                      poc_msb;                // PicOrderCntMsb
    gint32                      poc_lsb;                // pic_order_cnt_lsb (from slice_header())
    gint32                      prev_poc_msb;           // prevPicOrderCntMsb
    gint32                      prev_poc_lsb;           // prevPicOrderCntLsb
    gint32                      frame_num_offset;       // FrameNumOffset
    gint32                      prev_frame_num_offset;  // prevFrameNumOffset
    gint32                      frame_num;              // frame_num (from slice_header())
    gint32                      prev_frame_num;         // prevFrameNum
    guint                       is_constructed          : 1;
    guint                       is_opened               : 1;
    guint                       is_avc                  : 1;
    guint                       has_context             : 1;
};

static gboolean
decode_picture_end(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture);

static void
clear_references(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264 **pictures,
    guint                *picture_count
);

/* Get number of reference frames to use */
static guint
get_max_dec_frame_buffering(GstH264SPS *sps)
{
    guint max_dec_frame_buffering, MaxDpbMbs, PicSizeMbs;

    /* Table A-1 - Level limits */
    switch (sps->level_idc) {
    case 10: MaxDpbMbs = 396;    break;
    case 11: MaxDpbMbs = 900;    break;
    case 12: MaxDpbMbs = 2376;   break;
    case 13: MaxDpbMbs = 2376;   break;
    case 20: MaxDpbMbs = 2376;   break;
    case 21: MaxDpbMbs = 4752;   break;
    case 22: MaxDpbMbs = 8100;   break;
    case 30: MaxDpbMbs = 8100;   break;
    case 31: MaxDpbMbs = 18000;  break;
    case 32: MaxDpbMbs = 20480;  break;
    case 40: MaxDpbMbs = 32768;  break;
    case 41: MaxDpbMbs = 32768;  break;
    case 42: MaxDpbMbs = 34816;  break;
    case 50: MaxDpbMbs = 110400; break;
    case 51: MaxDpbMbs = 184320; break;
    default:
        g_assert(0 && "unhandled level");
        break;
    }

    PicSizeMbs = ((sps->pic_width_in_mbs_minus1 + 1) *
                  (sps->pic_height_in_map_units_minus1 + 1) *
                  (sps->frame_mbs_only_flag ? 1 : 2));
    max_dec_frame_buffering = MaxDpbMbs / PicSizeMbs;

    /* VUI parameters */
    if (sps->vui_parameters_present_flag) {
        GstH264VUIParams * const vui_params = &sps->vui_parameters;
        if (vui_params->bitstream_restriction_flag)
            max_dec_frame_buffering = vui_params->max_dec_frame_buffering;
        else {
            switch (sps->profile_idc) {
            case 44:  // CAVLC 4:4:4 Intra profile
            case 86:  // Scalable High profile
            case 100: // High profile
            case 110: // High 10 profile
            case 122: // High 4:2:2 profile
            case 244: // High 4:4:4 Predictive profile
                if (sps->constraint_set3_flag)
                    max_dec_frame_buffering = 0;
                break;
            }
        }
    }

    if (max_dec_frame_buffering > 16)
        max_dec_frame_buffering = 16;
    else if (max_dec_frame_buffering < sps->num_ref_frames)
        max_dec_frame_buffering = sps->num_ref_frames;
    return MAX(1, max_dec_frame_buffering);
}

static void
dpb_remove_index(GstVaapiDecoderH264 *decoder, guint index)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint num_pictures = --priv->dpb_count;

    if (index != num_pictures)
        gst_vaapi_picture_replace(&priv->dpb[index], priv->dpb[num_pictures]);
    gst_vaapi_picture_replace(&priv->dpb[num_pictures], NULL);
}

static inline gboolean
dpb_output(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    /* XXX: update cropping rectangle */
    picture->output_needed = FALSE;
    return gst_vaapi_picture_output(GST_VAAPI_PICTURE_CAST(picture));
}

static gboolean
dpb_bump(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i, lowest_poc_index;
    gboolean success;

    for (i = 0; i < priv->dpb_count; i++) {
        if (priv->dpb[i]->output_needed)
            break;
    }
    if (i == priv->dpb_count)
        return FALSE;

    lowest_poc_index = i++;
    for (; i < priv->dpb_count; i++) {
        GstVaapiPictureH264 * const picture = priv->dpb[i];
        if (picture->output_needed && picture->poc < priv->dpb[lowest_poc_index]->poc)
            lowest_poc_index = i;
    }

    success = dpb_output(decoder, priv->dpb[lowest_poc_index]);
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(priv->dpb[lowest_poc_index]))
        dpb_remove_index(decoder, lowest_poc_index);
    return success;
}

static void
dpb_flush(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    while (dpb_bump(decoder))
        ;
    clear_references(decoder, priv->dpb, &priv->dpb_count);
}

static gboolean
dpb_add(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    // Remove all unused pictures
    if (picture->is_idr)
        dpb_flush(decoder);
    else {
        i = 0;
        while (i < priv->dpb_count) {
            GstVaapiPictureH264 * const picture = priv->dpb[i];
            if (!picture->output_needed &&
                !GST_VAAPI_PICTURE_IS_REFERENCE(picture))
                dpb_remove_index(decoder, i);
            else
                i++;
        }
    }

    // C.4.5.1 - Storage and marking of a reference decoded picture into the DPB
    if (GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
        while (priv->dpb_count == priv->dpb_size) {
            if (!dpb_bump(decoder))
                return FALSE;
        }
        gst_vaapi_picture_replace(&priv->dpb[priv->dpb_count++], picture);
        if (picture->output_flag)
            picture->output_needed = TRUE;
    }

    // C.4.5.2 - Storage and marking of a non-reference decoded picture into the DPB
    else {
        if (!picture->output_flag)
            return TRUE;
        while (priv->dpb_count == priv->dpb_size) {
            for (i = 0; i < priv->dpb_count; i++) {
                if (priv->dpb[i]->output_needed &&
                    priv->dpb[i]->poc < picture->poc)
                    break;
            }
            if (i == priv->dpb_count)
                return dpb_output(decoder, picture);
            if (!dpb_bump(decoder))
                return FALSE;
        }
        gst_vaapi_picture_replace(&priv->dpb[priv->dpb_count++], picture);
        picture->output_needed = TRUE;
    }
    return TRUE;
}

static inline void
dpb_reset(GstVaapiDecoderH264 *decoder, GstH264SPS *sps)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    priv->dpb_size = get_max_dec_frame_buffering(sps);
    GST_DEBUG("DPB size %u", priv->dpb_size);
}

static GstVaapiDecoderStatus
get_status(GstH264ParserResult result)
{
    GstVaapiDecoderStatus status;

    switch (result) {
    case GST_H264_PARSER_OK:
        status = GST_VAAPI_DECODER_STATUS_SUCCESS;
        break;
    case GST_H264_PARSER_NO_NAL_END:
        status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        break;
    case GST_H264_PARSER_ERROR:
        status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
        break;
    default:
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        break;
    }
    return status;
}

static inline GstH264DecRefPicMarking *
get_dec_ref_pic_marking(GstVaapiPictureH264 *picture_h264)
{
    GstVaapiPicture * const picture = GST_VAAPI_PICTURE_CAST(picture_h264);
    GstVaapiSliceH264 *slice;

    slice = g_ptr_array_index(picture->slices, picture->slices->len - 1);
    return &slice->slice_hdr.dec_ref_pic_marking;
}

static void
gst_vaapi_decoder_h264_close(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    clear_references(decoder, priv->short_ref, &priv->short_ref_count);
    clear_references(decoder, priv->long_ref,  &priv->long_ref_count );
    clear_references(decoder, priv->dpb,       &priv->dpb_count      );

    if (priv->parser) {
        gst_h264_nal_parser_free(priv->parser);
        priv->parser = NULL;
    }

    if (priv->adapter) {
        gst_adapter_clear(priv->adapter);
        g_object_unref(priv->adapter);
        priv->adapter = NULL;
    }
}

static gboolean
gst_vaapi_decoder_h264_open(GstVaapiDecoderH264 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    gst_vaapi_decoder_h264_close(decoder);

    priv->adapter = gst_adapter_new();
    if (!priv->adapter)
        return FALSE;

    priv->parser = gst_h264_nal_parser_new();
    if (!priv->parser)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_decoder_h264_destroy(GstVaapiDecoderH264 *decoder)
{
    gst_vaapi_decoder_h264_close(decoder);
}

static gboolean
gst_vaapi_decoder_h264_create(GstVaapiDecoderH264 *decoder)
{
    if (!GST_VAAPI_DECODER_CODEC(decoder))
        return FALSE;
    return TRUE;
}

static GstVaapiDecoderStatus
ensure_context(GstVaapiDecoderH264 *decoder, GstH264SPS *sps)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiProfile profiles[2];
    GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_VLD;
    guint i, n_profiles = 0;
    gboolean success, reset_context = FALSE;

    if (!priv->has_context || priv->sps->profile_idc != sps->profile_idc) {
        GST_DEBUG("profile changed");
        reset_context = TRUE;

        switch (sps->profile_idc) {
        case 66:
            profiles[n_profiles++] = GST_VAAPI_PROFILE_H264_BASELINE;
            break;
        case 77:
            profiles[n_profiles++] = GST_VAAPI_PROFILE_H264_MAIN;
            // fall-through
        case 100:
            profiles[n_profiles++] = GST_VAAPI_PROFILE_H264_HIGH;
            break;
        default:
            GST_DEBUG("unsupported profile %d", sps->profile_idc);
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
        }

        for (i = 0; i < n_profiles; i++) {
            success = gst_vaapi_display_has_decoder(
                GST_VAAPI_DECODER_DISPLAY(decoder),
                profiles[i],
                entrypoint
            );
            if (success)
                break;
        }
        if (i == n_profiles)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE;
        priv->profile = profiles[i];
    }

    if (!priv->has_context ||
        priv->sps->chroma_format_idc != sps->chroma_format_idc) {
        GST_DEBUG("chroma format changed");
        reset_context = TRUE;

        /* XXX: theoritically, we could handle 4:2:2 format */
        if (sps->chroma_format_idc != 1)
            return GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT;
    }

    if (!priv->has_context              ||
        priv->sps->width  != sps->width ||
        priv->sps->height != sps->height) {
        GST_DEBUG("size changed");
        reset_context      = TRUE;

        priv->width      = sps->width;
        priv->height     = sps->height;
        priv->mb_width   = sps->pic_width_in_mbs_minus1 + 1;
        priv->mb_height  = sps->pic_height_in_map_units_minus1 + 1;
        priv->mb_height *= 2 - sps->frame_mbs_only_flag;
    }

    if (reset_context) {
        GstVaapiContextInfo info;

        info.profile    = priv->profile;
        info.entrypoint = entrypoint;
        info.width      = priv->width;
        info.height     = priv->height;
        info.ref_frames = get_max_dec_frame_buffering(sps);

        if (!gst_vaapi_decoder_ensure_context(GST_VAAPI_DECODER(decoder), &info))
            return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        priv->has_context = TRUE;

        /* Reset DPB */
        dpb_reset(decoder, sps);
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
ensure_quant_matrix(GstVaapiDecoderH264 *decoder, GstH264PPS *pps)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    if (priv->pps != pps) {
        memcpy(priv->scaling_list_4x4, pps->scaling_lists_4x4,
               sizeof(priv->scaling_list_4x4));
        memcpy(priv->scaling_list_8x8, pps->scaling_lists_8x8,
               sizeof(priv->scaling_list_8x8));
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
decode_current_picture(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 * const picture = priv->current_picture;
    gboolean success = FALSE;

    if (!picture)
        return TRUE;

    if (!decode_picture_end(decoder, picture))
        goto end;
    if (!gst_vaapi_picture_decode(GST_VAAPI_PICTURE_CAST(picture)))
        goto end;
    success = TRUE;
end:
    gst_vaapi_picture_replace(&priv->current_picture, NULL);
    return success;
}

static GstVaapiDecoderStatus
decode_sps(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SPS * const sps = &priv->last_sps;
    GstH264ParserResult result;

    GST_DEBUG("decode SPS");

    if (priv->current_picture && !decode_current_picture(decoder))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    memset(sps, 0, sizeof(*sps));
    result = gst_h264_parser_parse_sps(priv->parser, nalu, sps, TRUE);
    if (result != GST_H264_PARSER_OK)
        return get_status(result);

    return ensure_context(decoder, sps);
}

static GstVaapiDecoderStatus
decode_pps(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = &priv->last_pps;
    GstH264ParserResult result;

    GST_DEBUG("decode PPS");

    if (priv->current_picture && !decode_current_picture(decoder))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    memset(pps, 0, sizeof(*pps));
    result = gst_h264_parser_parse_pps(priv->parser, nalu, pps);
    if (result != GST_H264_PARSER_OK)
        return get_status(result);

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sei(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SEIMessage sei;
    GstH264ParserResult result;

    GST_DEBUG("decode SEI");

    memset(&sei, 0, sizeof(sei));
    result = gst_h264_parser_parse_sei(priv->parser, nalu, &sei);
    if (result != GST_H264_PARSER_OK) {
        GST_WARNING("failed to decode SEI, payload type:%d", sei.payloadType);
        return get_status(result);
    }

    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static GstVaapiDecoderStatus
decode_sequence_end(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;

    GST_DEBUG("decode sequence-end");

    if (priv->current_picture && !decode_current_picture(decoder))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    dpb_flush(decoder);
    return GST_VAAPI_DECODER_STATUS_END_OF_STREAM;
}

/* 8.2.1.1 - Decoding process for picture order count type 0 */
static void
init_picture_poc_0(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxPicOrderCntLsb = 1 << (sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

    GST_DEBUG("decode picture order count type 0");

    // (8-3)
    priv->poc_lsb = slice_hdr->pic_order_cnt_lsb;
    if (priv->poc_lsb < priv->prev_poc_lsb &&
        (priv->prev_poc_lsb - priv->poc_lsb) >= (MaxPicOrderCntLsb / 2))
        priv->poc_msb = priv->prev_poc_msb + MaxPicOrderCntLsb;
    else if (priv->poc_lsb > priv->prev_poc_lsb &&
             (priv->poc_lsb - priv->prev_poc_lsb) > (MaxPicOrderCntLsb / 2))
        priv->poc_msb = priv->prev_poc_msb - MaxPicOrderCntLsb;
    else
        priv->poc_msb = priv->prev_poc_msb;

    // (8-4)
    if (!slice_hdr->field_pic_flag || !slice_hdr->bottom_field_flag)
        priv->field_poc[TOP_FIELD] = priv->poc_msb + priv->poc_lsb;

    // (8-5)
    if (!slice_hdr->field_pic_flag)
        priv->field_poc[BOTTOM_FIELD] = priv->field_poc[TOP_FIELD] +
            slice_hdr->delta_pic_order_cnt_bottom;
    else if (slice_hdr->bottom_field_flag)
        priv->field_poc[BOTTOM_FIELD] = priv->poc_msb + priv->poc_lsb;
}

/* 8.2.1.2 - Decoding process for picture order count type 1 */
static void
init_picture_poc_1(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    gint32 abs_frame_num, expected_poc;
    guint i;

    GST_DEBUG("decode picture order count type 1");

    // (8-6)
    if (picture->is_idr)
        priv->frame_num_offset = 0;
    else if (priv->prev_frame_num > priv->frame_num)
        priv->frame_num_offset = priv->prev_frame_num_offset + MaxFrameNum;
    else
        priv->frame_num_offset = priv->prev_frame_num_offset;

    // (8-7)
    if (sps->num_ref_frames_in_pic_order_cnt_cycle != 0)
        abs_frame_num = priv->frame_num_offset + priv->frame_num;
    else
        abs_frame_num = 0;
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture) && abs_frame_num > 0)
        abs_frame_num = abs_frame_num - 1;

    if (abs_frame_num > 0) {
        gint32 expected_delta_per_poc_cycle;
        gint32 poc_cycle_cnt, frame_num_in_poc_cycle;

        expected_delta_per_poc_cycle = 0;
        for (i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++)
            expected_delta_per_poc_cycle += sps->offset_for_ref_frame[i];

        // (8-8)
        poc_cycle_cnt = (abs_frame_num - 1) /
            sps->num_ref_frames_in_pic_order_cnt_cycle;
        frame_num_in_poc_cycle = (abs_frame_num - 1) %
            sps->num_ref_frames_in_pic_order_cnt_cycle;

        // (8-9)
        expected_poc = poc_cycle_cnt * expected_delta_per_poc_cycle;
        for (i = 0; i <= frame_num_in_poc_cycle; i++)
            expected_poc += sps->offset_for_ref_frame[i];
    }
    else
        expected_poc = 0;
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        expected_poc += sps->offset_for_non_ref_pic;

    // (8-10)
    if (!slice_hdr->field_pic_flag) {
        priv->field_poc[TOP_FIELD] = expected_poc +
            slice_hdr->delta_pic_order_cnt[0];
        priv->field_poc[BOTTOM_FIELD] = priv->field_poc[TOP_FIELD] +
            sps->offset_for_top_to_bottom_field +
            slice_hdr->delta_pic_order_cnt[1];
    }
    else if (!slice_hdr->bottom_field_flag)
        priv->field_poc[TOP_FIELD] = expected_poc +
            slice_hdr->delta_pic_order_cnt[0];
    else
        priv->field_poc[BOTTOM_FIELD] = expected_poc + 
            sps->offset_for_top_to_bottom_field + slice_hdr->delta_pic_order_cnt[0];
}

/* 8.2.1.3 - Decoding process for picture order count type 2 */
static void
init_picture_poc_2(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    guint temp_poc;

    GST_DEBUG("decode picture order count type 2");

    // (8-11)
    if (picture->is_idr)
        priv->frame_num_offset = 0;
    else if (priv->prev_frame_num > priv->frame_num)
        priv->frame_num_offset = priv->prev_frame_num_offset + MaxFrameNum;
    else
        priv->frame_num_offset = priv->prev_frame_num_offset;

    // (8-12)
    if (picture->is_idr)
        temp_poc = 0;
    else if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        temp_poc = 2 * (priv->frame_num_offset + priv->frame_num) - 1;
    else
        temp_poc = 2 * (priv->frame_num_offset + priv->frame_num);

    // (8-13)
    if (!slice_hdr->field_pic_flag) {
        priv->field_poc[TOP_FIELD] = temp_poc;
        priv->field_poc[BOTTOM_FIELD] = temp_poc;
    }
    else if (slice_hdr->bottom_field_flag)
        priv->field_poc[BOTTOM_FIELD] = temp_poc;
    else
        priv->field_poc[TOP_FIELD] = temp_poc;
}

/* 8.2.1 - Decoding process for picture order count */
static void
init_picture_poc(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    VAPictureH264 * const pic = &picture->info;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;

    switch (sps->pic_order_cnt_type) {
    case 0:
        init_picture_poc_0(decoder, picture, slice_hdr);
        break;
    case 1:
        init_picture_poc_1(decoder, picture, slice_hdr);
        break;
    case 2:
        init_picture_poc_2(decoder, picture, slice_hdr);
        break;
    }

    if (!(pic->flags & VA_PICTURE_H264_BOTTOM_FIELD))
        pic->TopFieldOrderCnt = priv->field_poc[TOP_FIELD];
    if (!(pic->flags & VA_PICTURE_H264_TOP_FIELD))
        pic->BottomFieldOrderCnt = priv->field_poc[BOTTOM_FIELD];
    picture->poc = MIN(pic->TopFieldOrderCnt, pic->BottomFieldOrderCnt);
}

static int
compare_picture_pic_num_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->pic_num - picA->pic_num;
}

static int
compare_picture_long_term_pic_num_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->long_term_pic_num - picB->long_term_pic_num;
}

static int
compare_picture_poc_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->poc - picA->poc;
}

static int
compare_picture_poc_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->poc - picB->poc;
}

static int
compare_picture_frame_num_wrap_dec(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picB->frame_num_wrap - picA->frame_num_wrap;
}

static int
compare_picture_long_term_frame_idx_inc(const void *a, const void *b)
{
    const GstVaapiPictureH264 * const picA = *(GstVaapiPictureH264 **)a;
    const GstVaapiPictureH264 * const picB = *(GstVaapiPictureH264 **)b;

    return picA->info.frame_idx - picB->info.frame_idx;
}

/* 8.2.4.1 - Decoding process for picture numbers */
static void
init_picture_refs_pic_num(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    const gint32 MaxFrameNum = 1 << (sps->log2_max_frame_num_minus4 + 4);
    const guint field_flags = VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD;
    guint i;

    GST_DEBUG("decode picture numbers");

    for (i = 0; i < priv->short_ref_count; i++) {
        GstVaapiPictureH264 * const pic = priv->short_ref[i];

        // (8-27)
        if (pic->frame_num > priv->frame_num)
            pic->frame_num_wrap = pic->frame_num - MaxFrameNum;
        else
            pic->frame_num_wrap = pic->frame_num;

        // (8-28, 8-30, 8-31)
        if (!pic->field_pic_flag)
            pic->pic_num = pic->frame_num_wrap;
        else {
            if (((picture->info.flags ^ pic->info.flags) & field_flags) == 0)
                pic->pic_num = 2 * pic->frame_num_wrap + 1;
            else
                pic->pic_num = 2 * pic->frame_num_wrap;
        }
    }

    for (i = 0; i < priv->long_ref_count; i++) {
        GstVaapiPictureH264 * const pic = priv->long_ref[i];

        // (8-29, 8-32, 8-33)
        if (!pic->field_pic_flag)
            pic->long_term_pic_num = pic->info.frame_idx;
        else {
            if (((picture->info.flags ^ pic->info.flags) & field_flags) == 0)
                pic->long_term_pic_num = 2 * pic->info.frame_idx + 1;
            else
                pic->long_term_pic_num = 2 * pic->info.frame_idx;
        }
    }
}

#define SORT_REF_LIST(list, n, compare_func) \
    qsort(list, n, sizeof(*(list)), compare_picture_##compare_func)

static void
init_picture_refs_p_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 **ref_list;
    guint i;

    GST_DEBUG("decode reference picture list for P and SP slices");

    if (!picture->field_pic_flag) {
        /* 8.2.4.2.1 - P and SP slices in frames */
        if (priv->short_ref_count > 0) {
            ref_list = priv->RefPicList0;
            for (i = 0; i < priv->short_ref_count; i++)
                ref_list[i] = priv->short_ref[i];
            SORT_REF_LIST(ref_list, i, pic_num_dec);
            priv->RefPicList0_count += i;
        }

        if (priv->long_ref_count > 0) {
            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (i = 0; i < priv->long_ref_count; i++)
                ref_list[i] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, i, long_term_pic_num_inc);
            priv->RefPicList0_count += i;
        }
    }
    else {
        /* 8.2.4.2.2 - P and SP slices in fields */
        GstVaapiPictureH264 *short_ref[32];
        guint short_ref_count = 0;
        GstVaapiPictureH264 *long_ref[32];
        guint long_ref_count = 0;

        // XXX: handle second field if current field is marked as
        // "used for short-term reference"
        if (priv->short_ref_count > 0) {
            for (i = 0; i < priv->short_ref_count; i++)
                short_ref[i] = priv->short_ref[i];
            SORT_REF_LIST(short_ref, i, frame_num_wrap_dec);
            short_ref_count = i;
        }

        // XXX: handle second field if current field is marked as
        // "used for long-term reference"
        if (priv->long_ref_count > 0) {
            for (i = 0; i < priv->long_ref_count; i++)
                long_ref[i] = priv->long_ref[i];
            SORT_REF_LIST(long_ref, i, long_term_frame_idx_inc);
            long_ref_count = i;
        }

        // XXX: handle 8.2.4.2.5
    }
}

static void
init_picture_refs_b_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 **ref_list;
    guint i, n;

    GST_DEBUG("decode reference picture list for B slices");

    if (!picture->field_pic_flag) {
        /* 8.2.4.2.3 - B slices in frames */

        /* RefPicList0 */
        if (priv->short_ref_count > 0) {
            // 1. Short-term references
            ref_list = priv->RefPicList0;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc < picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            priv->RefPicList0_count += n;

            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc >= picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            priv->RefPicList0_count += n;
        }

        if (priv->long_ref_count > 0) {
            // 2. Long-term references
            ref_list = &priv->RefPicList0[priv->RefPicList0_count];
            for (n = 0, i = 0; i < priv->long_ref_count; i++)
                ref_list[n++] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, n, long_term_pic_num_inc);
            priv->RefPicList0_count += n;
        }

        /* RefPicList1 */
        if (priv->short_ref_count > 0) {
            // 1. Short-term references
            ref_list = priv->RefPicList1;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc > picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            priv->RefPicList1_count += n;

            ref_list = &priv->RefPicList1[priv->RefPicList1_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc <= picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            priv->RefPicList1_count += n;
        }

        if (priv->long_ref_count > 0) {
            // 2. Long-term references
            ref_list = &priv->RefPicList1[priv->RefPicList1_count];
            for (n = 0, i = 0; i < priv->long_ref_count; i++)
                ref_list[n++] = priv->long_ref[i];
            SORT_REF_LIST(ref_list, n, long_term_pic_num_inc);
            priv->RefPicList1_count += n;
        }
    }
    else {
        /* 8.2.4.2.4 - B slices in fields */
        GstVaapiPictureH264 *short_ref0[32];
        guint short_ref0_count = 0;
        GstVaapiPictureH264 *short_ref1[32];
        guint short_ref1_count = 0;
        GstVaapiPictureH264 *long_ref[32];
        guint long_ref_count = 0;

        /* refFrameList0ShortTerm */
        if (priv->short_ref_count > 0) {
            ref_list = short_ref0;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc <= picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            short_ref0_count += n;

            ref_list = &short_ref0[short_ref0_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc > picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            short_ref0_count += n;
        }

        /* refFrameList1ShortTerm */
        if (priv->short_ref_count > 0) {
            ref_list = short_ref1;
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc > picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_inc);
            short_ref1_count += n;

            ref_list = &short_ref1[short_ref1_count];
            for (n = 0, i = 0; i < priv->short_ref_count; i++) {
                if (priv->short_ref[i]->poc <= picture->poc)
                    ref_list[n++] = priv->short_ref[i];
            }
            SORT_REF_LIST(ref_list, n, poc_dec);
            short_ref1_count += n;
        }

        /* refFrameListLongTerm */
        if (priv->long_ref_count > 0) {
            for (i = 0; i < priv->long_ref_count; i++)
                long_ref[i] = priv->long_ref[i];
            SORT_REF_LIST(long_ref, i, long_term_frame_idx_inc);
            long_ref_count = i;
        }

        // XXX: handle 8.2.4.2.5
    }

    /* Check whether RefPicList1 is identical to RefPicList0, then
       swap if necessary */
    if (priv->RefPicList1_count > 1 &&
        priv->RefPicList1_count == priv->RefPicList0_count &&
        memcmp(priv->RefPicList0, priv->RefPicList1,
               priv->RefPicList0_count * sizeof(priv->RefPicList0[0])) == 0) {
        GstVaapiPictureH264 * const tmp = priv->RefPicList1[0];
        priv->RefPicList1[0] = priv->RefPicList1[1];
        priv->RefPicList1[1] = tmp;
    }
}

#undef SORT_REF_LIST

static void
clear_references(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264 **pictures,
    guint                *picture_count
)
{
    const guint num_pictures = *picture_count;
    guint i;

    for (i = 0; i < num_pictures; i++)
        gst_vaapi_picture_replace(&pictures[i], NULL);
    *picture_count = 0;
}

static gboolean
remove_reference_at(
    GstVaapiDecoderH264  *decoder,
    GstVaapiPictureH264 **pictures,
    guint                *picture_count,
    guint                 index
)
{
    guint num_pictures = *picture_count;

    g_return_val_if_fail(index < num_pictures, FALSE);

    GST_VAAPI_PICTURE_FLAG_UNSET(pictures[index], GST_VAAPI_PICTURE_FLAG_REFERENCE);
    if (index != --num_pictures)
        gst_vaapi_picture_replace(&pictures[index], pictures[num_pictures]);
    gst_vaapi_picture_replace(&pictures[num_pictures], NULL);
    *picture_count = num_pictures;
    return TRUE;
}

static gint
find_short_term_reference(GstVaapiDecoderH264 *decoder, gint32 pic_num)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    for (i = 0; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->pic_num == pic_num)
            return i;
    }
    GST_ERROR("found no short-term reference picture with PicNum = %d",
              pic_num);
    return -1;
}

static gint
find_long_term_reference(GstVaapiDecoderH264 *decoder, gint32 long_term_pic_num)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    guint i;

    for (i = 0; i < priv->long_ref_count; i++) {
        if (priv->long_ref[i]->long_term_pic_num == long_term_pic_num)
            return i;
    }
    GST_ERROR("found no long-term reference picture with LongTermPicNum = %d",
              long_term_pic_num);
    return -1;
}

static void
exec_picture_refs_modification_1(
    GstVaapiDecoderH264           *decoder,
    GstVaapiPictureH264           *picture,
    GstH264SliceHdr               *slice_hdr,
    guint                          list
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    GstH264RefPicListModification *ref_pic_list_modification;
    guint num_ref_pic_list_modifications;
    GstVaapiPictureH264 **ref_list;
    guint *ref_list_count_ptr, ref_list_count, ref_list_idx = 0;
    guint i, j, n, num_refs;
    gint found_ref_idx;
    gint32 MaxPicNum, CurrPicNum, picNumPred;

    GST_DEBUG("modification process of reference picture list %u", list);

    if (list == 0) {
        ref_pic_list_modification      = slice_hdr->ref_pic_list_modification_l0;
        num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l0;
        ref_list                       = priv->RefPicList0;
        ref_list_count_ptr             = &priv->RefPicList0_count;
        num_refs                       = slice_hdr->num_ref_idx_l0_active_minus1 + 1;
    }
    else {
        ref_pic_list_modification      = slice_hdr->ref_pic_list_modification_l1;
        num_ref_pic_list_modifications = slice_hdr->n_ref_pic_list_modification_l1;
        ref_list                       = priv->RefPicList1;
        ref_list_count_ptr             = &priv->RefPicList1_count;
        num_refs                       = slice_hdr->num_ref_idx_l1_active_minus1 + 1;
    }
    ref_list_count = *ref_list_count_ptr;

    if (picture->field_pic_flag) {
        MaxPicNum  = 1 << (sps->log2_max_frame_num_minus4 + 5); // 2 * MaxFrameNum
        CurrPicNum = 2 * slice_hdr->frame_num + 1;              // 2 * frame_num + 1
    }
    else {
        MaxPicNum  = 1 << (sps->log2_max_frame_num_minus4 + 4); // MaxFrameNum
        CurrPicNum = slice_hdr->frame_num;                      // frame_num
    }

    picNumPred = CurrPicNum;

    for (i = 0; i < num_ref_pic_list_modifications; i++) {
        GstH264RefPicListModification * const l = &ref_pic_list_modification[i];
        if (l->modification_of_pic_nums_idc == 3)
            break;

        /* 8.2.4.3.1 - Short-term reference pictures */
        if (l->modification_of_pic_nums_idc == 0 || l->modification_of_pic_nums_idc == 1) {
            gint32 abs_diff_pic_num = l->value.abs_diff_pic_num_minus1 + 1;
            gint32 picNum, picNumNoWrap;

            // (8-34)
            if (l->modification_of_pic_nums_idc == 0) {
                picNumNoWrap = picNumPred - abs_diff_pic_num;
                if (picNumNoWrap < 0)
                    picNumNoWrap += MaxPicNum;
            }

            // (8-35)
            else {
                picNumNoWrap = picNumPred + abs_diff_pic_num;
                if (picNumNoWrap >= MaxPicNum)
                    picNumNoWrap -= MaxPicNum;
            }
            picNumPred = picNumNoWrap;

            // (8-36)
            picNum = picNumNoWrap;
            if (picNum > CurrPicNum)
                picNum -= MaxPicNum;

            // (8-37)
            for (j = num_refs; j > ref_list_idx; j--)
                ref_list[j] = ref_list[j - 1];
            found_ref_idx = find_short_term_reference(decoder, picNum);
            ref_list[ref_list_idx++] =
                found_ref_idx >= 0 ? priv->short_ref[found_ref_idx] : NULL;
            n = ref_list_idx;
            for (j = ref_list_idx; j <= num_refs; j++) {
                gint32 PicNumF;
                if (!ref_list[j])
                    continue;
                PicNumF = ref_list[j]->is_long_term ?
                    MaxPicNum : ref_list[j]->pic_num;
                if (PicNumF != picNum)
                    ref_list[n++] = ref_list[j];
            }
        }

        /* 8.2.4.3.2 - Long-term reference pictures */
        else {

            for (j = num_refs; j > ref_list_idx; j--)
                ref_list[j] = ref_list[j - 1];
            found_ref_idx =
                find_long_term_reference(decoder, l->value.long_term_pic_num);
            ref_list[ref_list_idx++] =
                found_ref_idx >= 0 ? priv->long_ref[found_ref_idx] : NULL;
            n = ref_list_idx;
            for (j = ref_list_idx; j <= num_refs; j++) {
                gint32 LongTermPicNumF;
                if (!ref_list[j])
                    continue;
                LongTermPicNumF = ref_list[j]->is_long_term ?
                    ref_list[j]->long_term_pic_num : INT_MAX;
                if (LongTermPicNumF != l->value.long_term_pic_num)
                    ref_list[n++] = ref_list[j];
            }
        }
    }

#if DEBUG
    for (i = 0; i < num_refs; i++)
        if (!ref_list[i])
            GST_ERROR("list %u entry %u is empty", list, i);
#endif
    *ref_list_count_ptr = num_refs;
}

/* 8.2.4.3 - Modification process for reference picture lists */
static void
exec_picture_refs_modification(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GST_DEBUG("execute ref_pic_list_modification()");

    /* RefPicList0 */
    if (!GST_H264_IS_I_SLICE(slice_hdr) && !GST_H264_IS_SI_SLICE(slice_hdr) &&
        slice_hdr->ref_pic_list_modification_flag_l0)
        exec_picture_refs_modification_1(decoder, picture, slice_hdr, 0);

    /* RefPicList1 */
    if (GST_H264_IS_B_SLICE(slice_hdr) &&
        slice_hdr->ref_pic_list_modification_flag_l1)
        exec_picture_refs_modification_1(decoder, picture, slice_hdr, 1);
}

static gboolean
init_picture_refs(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;
    guint i, num_refs;

    init_picture_refs_pic_num(decoder, picture, slice_hdr);

    priv->RefPicList0_count = 0;
    priv->RefPicList1_count = 0;

    switch (base_picture->type) {
    case GST_VAAPI_PICTURE_TYPE_P:
    case GST_VAAPI_PICTURE_TYPE_SP:
        init_picture_refs_p_slice(decoder, picture, slice_hdr);
        break;
    case GST_VAAPI_PICTURE_TYPE_B:
        init_picture_refs_b_slice(decoder, picture, slice_hdr);
        break;
    default:
        break;
    }

    exec_picture_refs_modification(decoder, picture, slice_hdr);

    switch (base_picture->type) {
    case GST_VAAPI_PICTURE_TYPE_B:
        num_refs = 1 + slice_hdr->num_ref_idx_l1_active_minus1;
        for (i = priv->RefPicList1_count; i < num_refs; i++)
            priv->RefPicList1[i] = NULL;
        priv->RefPicList1_count = num_refs;

        // fall-through
    case GST_VAAPI_PICTURE_TYPE_P:
    case GST_VAAPI_PICTURE_TYPE_SP:
        num_refs = 1 + slice_hdr->num_ref_idx_l0_active_minus1;
        for (i = priv->RefPicList0_count; i < num_refs; i++)
            priv->RefPicList0[i] = NULL;
        priv->RefPicList0_count = num_refs;
        break;
    default:
        break;
    }
    return TRUE;
}

static gboolean
init_picture(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr,
    GstH264NalUnit      *nalu
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;
    VAPictureH264 *pic;
    guint i;

    priv->frame_num             = slice_hdr->frame_num;
    picture->frame_num          = priv->frame_num;
    picture->frame_num_wrap     = priv->frame_num;
    picture->is_idr             = nalu->type == GST_H264_NAL_SLICE_IDR;
    picture->field_pic_flag     = slice_hdr->field_pic_flag;
    picture->bottom_field_flag  = slice_hdr->bottom_field_flag;
    picture->output_flag        = TRUE; /* XXX: conformant to Annex A only */
    base_picture->pts           = gst_adapter_prev_timestamp(priv->adapter, NULL);

    /* Reset decoder state for IDR pictures */
    if (picture->is_idr) {
        GST_DEBUG("<IDR>");
        clear_references(decoder, priv->short_ref, &priv->short_ref_count);
        clear_references(decoder, priv->long_ref,  &priv->long_ref_count );
        priv->prev_poc_msb = 0;
        priv->prev_poc_lsb = 0;
    }

    /* Initialize VA picture info */
    pic = &picture->info;
    pic->picture_id = picture->base.surface_id;
    pic->frame_idx  = priv->frame_num;
    if (picture->field_pic_flag) {
        if (picture->bottom_field_flag)
            pic->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
        else
            pic->flags |= VA_PICTURE_H264_TOP_FIELD;
    }

    /* Initialize base picture */
    switch (slice_hdr->type % 5) {
    case GST_H264_P_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_P;
        break;
    case GST_H264_B_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_B;
        break;
    case GST_H264_I_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_I;
        break;
    case GST_H264_SP_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_SP;
        break;
    case GST_H264_SI_SLICE:
        base_picture->type = GST_VAAPI_PICTURE_TYPE_SI;
        break;
    }

    if (nalu->ref_idc) {
        GstH264DecRefPicMarking * const dec_ref_pic_marking =
            &slice_hdr->dec_ref_pic_marking;
        GST_VAAPI_PICTURE_FLAG_SET(picture, GST_VAAPI_PICTURE_FLAG_REFERENCE);
        if (picture->is_idr) {
            if (dec_ref_pic_marking->long_term_reference_flag)
                picture->is_long_term = TRUE;
        }
        else if (dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag) {
            for (i = 0; i < dec_ref_pic_marking->n_ref_pic_marking; i++) {
                GstH264RefPicMarking * const ref_pic_marking =
                    &dec_ref_pic_marking->ref_pic_marking[i];
                switch (ref_pic_marking->memory_management_control_operation) {
                case 3:
                case 6:
                    picture->is_long_term = TRUE;
                    pic->frame_idx = ref_pic_marking->long_term_frame_idx;
                    break;
                case 5:
                    picture->has_mmco_5 = TRUE;
                    break;
                }
            }
        }
        if (picture->is_long_term)
            pic->flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
        else
            pic->flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    }

    init_picture_poc(decoder, picture, slice_hdr);
    if (!init_picture_refs(decoder, picture, slice_hdr)) {
        GST_ERROR("failed to initialize references");
        return FALSE;
    }
    return TRUE;
}

/* 8.2.5.3 - Sliding window decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_sliding_window(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SPS * const sps = priv->sps;
    guint i, max_num_ref_frames, lowest_frame_num_index;
    gint32 lowest_frame_num;

    GST_DEBUG("reference picture marking process (sliding window)");

    max_num_ref_frames = sps->num_ref_frames;
    if (max_num_ref_frames == 0)
        max_num_ref_frames = 1;

    if (priv->short_ref_count + priv->long_ref_count < max_num_ref_frames)
        return TRUE;
    if (priv->short_ref_count < 1)
        return FALSE;

    lowest_frame_num = priv->short_ref[0]->frame_num_wrap;
    lowest_frame_num_index = 0;
    for (i = 1; i < priv->short_ref_count; i++) {
        if (priv->short_ref[i]->frame_num_wrap < lowest_frame_num) {
            lowest_frame_num = priv->short_ref[i]->frame_num_wrap;
            lowest_frame_num_index = i;
        }
    }

    remove_reference_at(
        decoder,
        priv->short_ref, &priv->short_ref_count,
        lowest_frame_num_index
    );
    return TRUE;
}

/* 8.2.5.4. Adaptive memory control decoded reference picture marking process */
static gboolean
exec_ref_pic_marking_adaptive(
    GstVaapiDecoderH264     *decoder,
    GstVaapiPictureH264     *picture,
    GstH264DecRefPicMarking *dec_ref_pic_marking
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    gint32 pic_num, ref_idx;
    guint i;

    GST_DEBUG("reference picture marking process (adaptive memory control)");

    for (i = 0; i < dec_ref_pic_marking->n_ref_pic_marking; i++) {
        GstH264RefPicMarking * const ref_pic_marking =
            &dec_ref_pic_marking->ref_pic_marking[i];

        switch (ref_pic_marking->memory_management_control_operation) {
        case 1:
            // Mark short-term reference picture as "unused for reference"
            if (!picture->field_pic_flag)
                pic_num = picture->frame_num_wrap;
            else
                pic_num = 2 * picture->frame_num_wrap + 1;
            pic_num -= ref_pic_marking->difference_of_pic_nums_minus1 + 1;
            ref_idx = find_short_term_reference(decoder, pic_num);
            if (ref_idx < 0)
                break;
            remove_reference_at(
                decoder,
                priv->short_ref, &priv->short_ref_count,
                ref_idx
            );
            break;
        case 2:
            // Mark long-term reference picture as "unused for reference"
            pic_num = picture->long_term_pic_num;
            ref_idx = find_long_term_reference(decoder, pic_num);
            if (ref_idx < 0)
                break;
            remove_reference_at(
                decoder,
                priv->long_ref, &priv->long_ref_count,
                ref_idx
            );
            break;
        case 3:
            // Assign LongTermFrameIdx to a short-term reference picture
            if (!picture->field_pic_flag)
                pic_num = picture->frame_num_wrap;
            else
                pic_num = 2 * picture->frame_num_wrap + 1;
            pic_num -= ref_pic_marking->difference_of_pic_nums_minus1 + 1;
            ref_idx = find_short_term_reference(decoder, pic_num);
            if (ref_idx < 0)
                break;
            break;
        case 5:
            // Mark all reference pictures as "unused for reference"
            clear_references(decoder, priv->short_ref, &priv->short_ref_count);
            clear_references(decoder, priv->long_ref,  &priv->long_ref_count );
            break;
        default:
            g_assert(0 && "unhandled MMCO");
            break;
        }
    }
    return TRUE;
}

/* 8.2.5 - Execute reference picture marking process */
static gboolean
exec_ref_pic_marking(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 **picture_ptr;

    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        return TRUE;

    if (!picture->is_idr) {
        GstH264DecRefPicMarking * const dec_ref_pic_marking =
            get_dec_ref_pic_marking(picture);
        if (dec_ref_pic_marking->adaptive_ref_pic_marking_mode_flag) {
            if (!exec_ref_pic_marking_adaptive(decoder, picture, dec_ref_pic_marking))
                return FALSE;
        }
        else {
            if (!exec_ref_pic_marking_sliding_window(decoder))
                return FALSE;
        }
    }

    if (picture->is_long_term)
        picture_ptr = &priv->long_ref[priv->long_ref_count++];
    else
        picture_ptr = &priv->short_ref[priv->short_ref_count++];
    gst_vaapi_picture_replace(picture_ptr, picture);
    return TRUE;
}

/* Update picture order count */
static void
exit_picture_poc(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SPS * const sps = priv->sps;

    switch (sps->pic_order_cnt_type) {
    case 0:
        if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
            break;
        if (picture->has_mmco_5) {
            priv->prev_poc_msb = 0;
            if (!picture->field_pic_flag || !picture->bottom_field_flag)
                priv->prev_poc_lsb = picture->info.TopFieldOrderCnt;
            else
                priv->prev_poc_lsb = 0;
        }
        else {
            priv->prev_poc_msb = priv->poc_msb;
            priv->prev_poc_lsb = priv->poc_lsb;
        }
        break;
    case 1:
    case 2:
        priv->prev_frame_num = priv->frame_num;
        if (picture->has_mmco_5)
            priv->prev_frame_num_offset = 0;
        else
            priv->prev_frame_num_offset = priv->frame_num_offset;
        break;
    }
}

static inline gboolean
exit_picture(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    /* Update picture order count */
    exit_picture_poc(decoder, picture);

    /* Decoded reference picture marking process */
    if (!exec_ref_pic_marking(decoder, picture))
        return FALSE;
    return TRUE;
}

static void
vaapi_init_picture(VAPictureH264 *pic)
{
    pic->picture_id           = VA_INVALID_ID;
    pic->frame_idx            = 0;
    pic->flags                = VA_PICTURE_H264_INVALID;
    pic->TopFieldOrderCnt     = 0;
    pic->BottomFieldOrderCnt  = 0;
}

static gboolean
fill_picture(
    GstVaapiDecoderH264 *decoder,
    GstVaapiPictureH264 *picture,
    GstH264SliceHdr     *slice_hdr,
    GstH264NalUnit      *nalu
)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPicture * const base_picture = &picture->base;
    GstH264SPS * const sps = priv->sps;
    GstH264PPS * const pps = priv->pps;
    VAPictureParameterBufferH264 * const pic_param = base_picture->param;
    guint i, n;

    /* Fill in VAPictureParameterBufferH264 */
    pic_param->CurrPic = picture->info;
    for (i = 0, n = 0; i < priv->short_ref_count; i++)
        pic_param->ReferenceFrames[n++] = priv->short_ref[i]->info;
    for (i = 0; i < priv->long_ref_count; i++)
        pic_param->ReferenceFrames[n++] = priv->long_ref[i]->info;
    for (; n < G_N_ELEMENTS(pic_param->ReferenceFrames); n++)
        vaapi_init_picture(&pic_param->ReferenceFrames[n]);

#define COPY_FIELD(s, f) \
    pic_param->f = (s)->f

#define COPY_BFM(a, s, f) \
    pic_param->a.bits.f = (s)->f

    pic_param->picture_width_in_mbs_minus1      = priv->mb_width  - 1;
    pic_param->picture_height_in_mbs_minus1     = priv->mb_height - 1;
    pic_param->frame_num                        = priv->frame_num;

    COPY_FIELD(sps, bit_depth_luma_minus8);
    COPY_FIELD(sps, bit_depth_chroma_minus8);
    COPY_FIELD(sps, num_ref_frames);
    COPY_FIELD(pps, num_slice_groups_minus1);
    COPY_FIELD(pps, slice_group_map_type);
    COPY_FIELD(pps, slice_group_change_rate_minus1);
    COPY_FIELD(pps, pic_init_qp_minus26);
    COPY_FIELD(pps, pic_init_qs_minus26);
    COPY_FIELD(pps, chroma_qp_index_offset);
    COPY_FIELD(pps, second_chroma_qp_index_offset);

    pic_param->seq_fields.value                                         = 0; /* reset all bits */
    pic_param->seq_fields.bits.residual_colour_transform_flag           = sps->separate_colour_plane_flag;
    pic_param->seq_fields.bits.MinLumaBiPredSize8x8                     = sps->level_idc >= 31; /* A.3.3.2 */

    COPY_BFM(seq_fields, sps, chroma_format_idc);
    COPY_BFM(seq_fields, sps, gaps_in_frame_num_value_allowed_flag);
    COPY_BFM(seq_fields, sps, frame_mbs_only_flag); 
    COPY_BFM(seq_fields, sps, mb_adaptive_frame_field_flag); 
    COPY_BFM(seq_fields, sps, direct_8x8_inference_flag); 
    COPY_BFM(seq_fields, sps, log2_max_frame_num_minus4);
    COPY_BFM(seq_fields, sps, pic_order_cnt_type);
    COPY_BFM(seq_fields, sps, log2_max_pic_order_cnt_lsb_minus4);
    COPY_BFM(seq_fields, sps, delta_pic_order_always_zero_flag);

    pic_param->pic_fields.value                                         = 0; /* reset all bits */
    pic_param->pic_fields.bits.field_pic_flag                           = slice_hdr->field_pic_flag;
    pic_param->pic_fields.bits.reference_pic_flag                       = GST_VAAPI_PICTURE_IS_REFERENCE(picture);

    COPY_BFM(pic_fields, pps, entropy_coding_mode_flag);
    COPY_BFM(pic_fields, pps, weighted_pred_flag);
    COPY_BFM(pic_fields, pps, weighted_bipred_idc);
    COPY_BFM(pic_fields, pps, transform_8x8_mode_flag);
    COPY_BFM(pic_fields, pps, constrained_intra_pred_flag);
    COPY_BFM(pic_fields, pps, pic_order_present_flag);
    COPY_BFM(pic_fields, pps, deblocking_filter_control_present_flag);
    COPY_BFM(pic_fields, pps, redundant_pic_cnt_present_flag);
    return TRUE;
}

static gboolean
fill_quant_matrix(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    VAIQMatrixBufferH264 * const iq_matrix = picture->base.iq_matrix->param;

    /* XXX: we can only support 4:2:0 or 4:2:2 since ScalingLists8x8[]
       is not large enough to hold lists for 4:4:4 */
    if (priv->sps->chroma_format_idc == 3 &&
        sizeof(iq_matrix->ScalingList8x8) != sizeof(priv->scaling_list_8x8))
        return FALSE;

    /* Fill in VAIQMatrixBufferH264 */
    memcpy(iq_matrix->ScalingList4x4, priv->scaling_list_4x4,
           sizeof(iq_matrix->ScalingList4x4));
    memcpy(iq_matrix->ScalingList8x8, priv->scaling_list_8x8,
           sizeof(iq_matrix->ScalingList8x8));
    return TRUE;
}

static GstVaapiDecoderStatus
decode_picture(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu, GstH264SliceHdr *slice_hdr)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiPictureH264 *picture;
    GstVaapiDecoderStatus status;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;

    status = ensure_context(decoder, sps);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset context");
        return status;
    }

    if (priv->current_picture && !decode_current_picture(decoder))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;

    picture = gst_vaapi_picture_h264_new(decoder);
    if (!picture) {
        GST_ERROR("failed to allocate picture");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }
    priv->current_picture = picture;

    picture->base.iq_matrix = GST_VAAPI_IQ_MATRIX_NEW(H264, decoder);
    if (!picture->base.iq_matrix) {
        GST_ERROR("failed to allocate IQ matrix");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }

    status = ensure_quant_matrix(decoder, pps);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to reset quantizer matrix");
        return status;
    }

    priv->sps = sps;
    priv->pps = pps;

    if (!init_picture(decoder, picture, slice_hdr, nalu))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    if (!fill_picture(decoder, picture, slice_hdr, nalu))
        return GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
    return GST_VAAPI_DECODER_STATUS_SUCCESS;
}

static gboolean
decode_picture_end(GstVaapiDecoderH264 *decoder, GstVaapiPictureH264 *picture)
{
    if (!fill_quant_matrix(decoder, picture))
        return FALSE;
    if (!exit_picture(decoder, picture))
        return FALSE;
    if (!dpb_add(decoder, picture))
        return FALSE;
    return TRUE;
}

#ifndef HAVE_GST_H264_SLICE_HDR_EPB_COUNT
static guint
get_epb_count(const guint8 *buf, guint buf_size, guint header_size)
{
    guint i, n = 0;

    if (buf_size > header_size)
        buf_size = header_size;

    for (i = 2; i < buf_size; i++) {
        if (!buf[i - 2] && !buf[i - 1] && buf[i] == 0x03)
            i += 2, n++;
    }
    return n;
}
#endif

static inline guint
get_slice_data_bit_offset(GstH264SliceHdr *slice_hdr, GstH264NalUnit *nalu)
{
    guint epb_count;

#ifdef HAVE_GST_H264_SLICE_HDR_EPB_COUNT
    epb_count = slice_hdr->n_emulation_prevention_bytes;
#else
    epb_count = get_epb_count(
        nalu->data + nalu->offset,
        nalu->size,
        slice_hdr->header_size / 8
    );
#endif
    return 8 /* nal_unit_type */ + slice_hdr->header_size - epb_count * 8;
}

static gboolean
fill_pred_weight_table(GstVaapiDecoderH264 *decoder, GstVaapiSliceH264 *slice)
{
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    GstH264PPS * const pps = slice_hdr->pps;
    GstH264SPS * const sps = pps->sequence;
    GstH264PredWeightTable * const w = &slice_hdr->pred_weight_table;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;
    guint num_weight_tables = 0;
    gint i, j;

    if (pps->weighted_pred_flag &&
        (GST_H264_IS_P_SLICE(slice_hdr) || GST_H264_IS_SP_SLICE(slice_hdr)))
        num_weight_tables = 1;
    else if (pps->weighted_bipred_idc == 1 && GST_H264_IS_B_SLICE(slice_hdr))
        num_weight_tables = 2;
    else
        num_weight_tables = 0;

    slice_param->luma_log2_weight_denom   = w->luma_log2_weight_denom;
    slice_param->chroma_log2_weight_denom = w->chroma_log2_weight_denom;
    slice_param->luma_weight_l0_flag      = 0;
    slice_param->chroma_weight_l0_flag    = 0;
    slice_param->luma_weight_l1_flag      = 0;
    slice_param->chroma_weight_l1_flag    = 0;

    if (num_weight_tables < 1)
        return TRUE;

    slice_param->luma_weight_l0_flag = 1;
    for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
        slice_param->luma_weight_l0[i] = w->luma_weight_l0[i];
        slice_param->luma_offset_l0[i] = w->luma_offset_l0[i];
    }

    slice_param->chroma_weight_l0_flag = sps->chroma_array_type != 0;
    if (slice_param->chroma_weight_l0_flag) {
        for (i = 0; i <= slice_param->num_ref_idx_l0_active_minus1; i++) {
            for (j = 0; j < 2; j++) {
                slice_param->chroma_weight_l0[i][j] = w->chroma_weight_l0[i][j];
                slice_param->chroma_offset_l0[i][j] = w->chroma_offset_l0[i][j];
            }
        }
    }

    if (num_weight_tables < 2)
        return TRUE;

    slice_param->luma_weight_l1_flag = 1;
    for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
        slice_param->luma_weight_l1[i] = w->luma_weight_l1[i];
        slice_param->luma_offset_l1[i] = w->luma_offset_l1[i];
    }

    slice_param->chroma_weight_l1_flag = sps->chroma_array_type != 0;
    if (slice_param->chroma_weight_l1_flag) {
        for (i = 0; i <= slice_param->num_ref_idx_l1_active_minus1; i++) {
            for (j = 0; j < 2; j++) {
                slice_param->chroma_weight_l1[i][j] = w->chroma_weight_l1[i][j];
                slice_param->chroma_offset_l1[i][j] = w->chroma_offset_l1[i][j];
            }
        }
    }
    return TRUE;
}

static gboolean
fill_RefPicList(GstVaapiDecoderH264 *decoder, GstVaapiSliceH264 *slice)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;
    guint i, num_ref_lists = 0;

    slice_param->num_ref_idx_l0_active_minus1 = 0;
    slice_param->num_ref_idx_l1_active_minus1 = 0;

    if (GST_H264_IS_B_SLICE(slice_hdr))
        num_ref_lists = 2;
    else if (GST_H264_IS_I_SLICE(slice_hdr))
        num_ref_lists = 0;
    else
        num_ref_lists = 1;

    if (num_ref_lists < 1)
        return TRUE;

    slice_param->num_ref_idx_l0_active_minus1 =
        slice_hdr->num_ref_idx_l0_active_minus1;

    for (i = 0; i < priv->RefPicList0_count && priv->RefPicList0[i]; i++)
        slice_param->RefPicList0[i] = priv->RefPicList0[i]->info;
    for (; i <= slice_param->num_ref_idx_l0_active_minus1; i++)
        vaapi_init_picture(&slice_param->RefPicList0[i]);

    if (num_ref_lists < 2)
        return TRUE;

    slice_param->num_ref_idx_l1_active_minus1 =
        slice_hdr->num_ref_idx_l1_active_minus1;

    for (i = 0; i < priv->RefPicList1_count && priv->RefPicList1[i]; i++)
        slice_param->RefPicList1[i] = priv->RefPicList1[i]->info;
    for (; i <= slice_param->num_ref_idx_l1_active_minus1; i++)
        vaapi_init_picture(&slice_param->RefPicList1[i]);
    return TRUE;
}

static gboolean
fill_slice(
    GstVaapiDecoderH264 *decoder,
    GstVaapiSliceH264   *slice,
    GstH264NalUnit      *nalu
)
{
    GstH264SliceHdr * const slice_hdr = &slice->slice_hdr;
    VASliceParameterBufferH264 * const slice_param = slice->base.param;

    /* Fill in VASliceParameterBufferH264 */
    slice_param->slice_data_bit_offset          = get_slice_data_bit_offset(slice_hdr, nalu);
    slice_param->first_mb_in_slice              = slice_hdr->first_mb_in_slice;
    slice_param->slice_type                     = slice_hdr->type % 5;
    slice_param->direct_spatial_mv_pred_flag    = slice_hdr->direct_spatial_mv_pred_flag;
    slice_param->cabac_init_idc                 = slice_hdr->cabac_init_idc;
    slice_param->slice_qp_delta                 = slice_hdr->slice_qp_delta;
    slice_param->disable_deblocking_filter_idc  = slice_hdr->disable_deblocking_filter_idc;
    slice_param->slice_alpha_c0_offset_div2     = slice_hdr->slice_alpha_c0_offset_div2;
    slice_param->slice_beta_offset_div2         = slice_hdr->slice_beta_offset_div2;

    if (!fill_RefPicList(decoder, slice))
        return FALSE;
    if (!fill_pred_weight_table(decoder, slice))
        return FALSE;
    return TRUE;
}

static GstVaapiDecoderStatus
decode_slice(GstVaapiDecoderH264 *decoder, GstH264NalUnit *nalu)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstVaapiPictureH264 *picture;
    GstVaapiSliceH264 *slice = NULL;
    GstH264SliceHdr *slice_hdr;
    GstH264ParserResult result;

    GST_DEBUG("slice (%u bytes)", nalu->size);

    slice = gst_vaapi_slice_h264_new(
        decoder,
        nalu->data + nalu->offset,
        nalu->size
    );
    if (!slice) {
        GST_ERROR("failed to allocate slice");
        return GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED;
    }

    slice_hdr = &slice->slice_hdr;
    memset(slice_hdr, 0, sizeof(*slice_hdr));
    result = gst_h264_parser_parse_slice_hdr(priv->parser, nalu, slice_hdr, TRUE, TRUE);
    if (result != GST_H264_PARSER_OK) {
        status = get_status(result);
        goto error;
    }

    if (slice_hdr->first_mb_in_slice == 0) {
        status = decode_picture(decoder, nalu, slice_hdr);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            goto error;
    }
    picture = priv->current_picture;

    priv->mb_x = slice_hdr->first_mb_in_slice % priv->mb_width;
    priv->mb_y = slice_hdr->first_mb_in_slice / priv->mb_width; // FIXME: MBAFF or field

    if (!fill_slice(decoder, slice, nalu)) {
        status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
        goto error;
    }
    gst_vaapi_picture_add_slice(
        GST_VAAPI_PICTURE_CAST(picture),
        GST_VAAPI_SLICE_CAST(slice)
    );

    /* Commit picture for decoding if we reached the last slice */
    if (++priv->mb_y >= priv->mb_height) {
        if (!decode_current_picture(decoder)) {
            status = GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN;
            goto error;
        }
        GST_DEBUG("done");
    }
    return GST_VAAPI_DECODER_STATUS_SUCCESS;

error:
    if (slice)
        gst_mini_object_unref(GST_MINI_OBJECT(slice));
    return status;
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
decode_buffer(GstVaapiDecoderH264 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstH264ParserResult result;
    GstH264NalUnit nalu;
    const guchar *buf;
    guint i, buf_size, nalu_size, size;
    guint32 start_code;
    gint ofs;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf && buf_size == 0)
        return decode_sequence_end(decoder);

    gst_adapter_push(priv->adapter, gst_buffer_ref(buffer));

    size = gst_adapter_available(priv->adapter);
    do {
        status = gst_vaapi_decoder_check_status(GST_VAAPI_DECODER(decoder));
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            break;

        status = GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;
        if (priv->is_avc) {
            if (size < priv->nal_length_size)
                break;
            buf = gst_adapter_peek(priv->adapter, priv->nal_length_size);

            nalu_size = 0;
            for (i = 0; i < priv->nal_length_size; i++)
                nalu_size = (nalu_size << 8) | buf[i];

            buf_size = priv->nal_length_size + nalu_size;
            if (size < buf_size)
                break;
            buffer = gst_adapter_take_buffer(priv->adapter, buf_size);
            size -= buf_size;

            buf      = GST_BUFFER_DATA(buffer);
            buf_size = GST_BUFFER_SIZE(buffer);

            result = gst_h264_parser_identify_nalu_avc(
                priv->parser,
                buf, 0, buf_size, priv->nal_length_size,
                &nalu
            );
        }
        else {
            if (size < 8)
                break;
            ofs = scan_for_start_code(priv->adapter, 0, size, &start_code);
            if (ofs < 0)
                break;
            gst_adapter_flush(priv->adapter, ofs);
            size -= ofs;

            if (size < 8)
                break;
            ofs = scan_for_start_code(priv->adapter, 4, size - 4, NULL);
            if (ofs < 0)
                break;
            buffer = gst_adapter_take_buffer(priv->adapter, ofs);
            size -= ofs;

            buf      = GST_BUFFER_DATA(buffer);
            buf_size = GST_BUFFER_SIZE(buffer);

            result = gst_h264_parser_identify_nalu_unchecked(
                priv->parser,
                buf, 0, buf_size,
                &nalu
            );
        }
        status = get_status(result);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
            gst_buffer_unref(buffer);
            break;
        }

        switch (nalu.type) {
        case GST_H264_NAL_SLICE_IDR:
            /* fall-through. IDR specifics are handled in init_picture() */
        case GST_H264_NAL_SLICE:
            status = decode_slice(decoder, &nalu);
            break;
        case GST_H264_NAL_SPS:
            status = decode_sps(decoder, &nalu);
            break;
        case GST_H264_NAL_PPS:
            status = decode_pps(decoder, &nalu);
            break;
        case GST_H264_NAL_SEI:
            status = decode_sei(decoder, &nalu);
            break;
        case GST_H264_NAL_SEQ_END:
            status = decode_sequence_end(decoder);
            break;
        case GST_H264_NAL_AU_DELIMITER:
            /* skip all Access Unit NALs */
            status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            break;
        case GST_H264_NAL_FILLER_DATA:
            /* skip all Filler Data NALs */
            status = GST_VAAPI_DECODER_STATUS_SUCCESS;
            break;
        default:
            GST_WARNING("unsupported NAL unit type %d", nalu.type);
            status = GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
            break;
        }
        gst_buffer_unref(buffer);
    } while (status == GST_VAAPI_DECODER_STATUS_SUCCESS);
    return status;
}

static GstVaapiDecoderStatus
decode_codec_data(GstVaapiDecoderH264 *decoder, GstBuffer *buffer)
{
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstH264NalUnit nalu;
    GstH264ParserResult result;
    guchar *buf;
    guint buf_size;
    guint i, ofs, num_sps, num_pps;

    buf      = GST_BUFFER_DATA(buffer);
    buf_size = GST_BUFFER_SIZE(buffer);
    if (!buf || buf_size == 0)
        return GST_VAAPI_DECODER_STATUS_SUCCESS;

    if (buf_size < 8)
        return GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA;

    if (buf[0] != 1) {
        GST_ERROR("failed to decode codec-data, not in avcC format");
        return GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER;
    }

    priv->nal_length_size = (buf[4] & 0x03) + 1;

    num_sps = buf[5] & 0x1f;
    ofs = 6;

    for (i = 0; i < num_sps; i++) {
        result = gst_h264_parser_identify_nalu_avc(
            priv->parser,
            buf, ofs, buf_size, 2,
            &nalu
        );
        if (result != GST_H264_PARSER_OK)
            return get_status(result);

        status = decode_sps(decoder, &nalu);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        ofs = nalu.offset + nalu.size;
    }

    num_pps = buf[ofs];
    ofs++;

    for (i = 0; i < num_pps; i++) {
        result = gst_h264_parser_identify_nalu_avc(
            priv->parser,
            buf, ofs, buf_size, 2,
            &nalu
        );
        if (result != GST_H264_PARSER_OK)
            return get_status(result);

        status = decode_pps(decoder, &nalu);
        if (status != GST_VAAPI_DECODER_STATUS_SUCCESS)
            return status;
        ofs = nalu.offset + nalu.size;
    }

    priv->is_avc = TRUE;
    return status;
}

GstVaapiDecoderStatus
gst_vaapi_decoder_h264_decode(GstVaapiDecoder *base, GstBuffer *buffer)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(base);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GstVaapiDecoderStatus status;
    GstBuffer *codec_data;

    g_return_val_if_fail(priv->is_constructed,
                         GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED);

    if (!priv->is_opened) {
        priv->is_opened = gst_vaapi_decoder_h264_open(decoder, buffer);
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
gst_vaapi_decoder_h264_finalize(GObject *object)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(object);

    gst_vaapi_decoder_h264_destroy(decoder);

    G_OBJECT_CLASS(gst_vaapi_decoder_h264_parent_class)->finalize(object);
}

static void
gst_vaapi_decoder_h264_constructed(GObject *object)
{
    GstVaapiDecoderH264 * const decoder = GST_VAAPI_DECODER_H264(object);
    GstVaapiDecoderH264Private * const priv = decoder->priv;
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_decoder_h264_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);

    priv->is_constructed = gst_vaapi_decoder_h264_create(decoder);
}

static void
gst_vaapi_decoder_h264_class_init(GstVaapiDecoderH264Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDecoderClass * const decoder_class = GST_VAAPI_DECODER_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDecoderH264Private));

    object_class->finalize      = gst_vaapi_decoder_h264_finalize;
    object_class->constructed   = gst_vaapi_decoder_h264_constructed;

    decoder_class->decode       = gst_vaapi_decoder_h264_decode;
}

static void
gst_vaapi_decoder_h264_init(GstVaapiDecoderH264 *decoder)
{
    GstVaapiDecoderH264Private *priv;

    priv                        = GST_VAAPI_DECODER_H264_GET_PRIVATE(decoder);
    decoder->priv               = priv;
    priv->parser                = NULL;
    priv->sps                   = &priv->last_sps;
    priv->pps                   = &priv->last_pps;
    priv->current_picture       = NULL;
    priv->dpb_count             = 0;
    priv->dpb_size              = 0;
    priv->profile               = GST_VAAPI_PROFILE_H264_HIGH;
    priv->short_ref_count       = 0;
    priv->long_ref_count        = 0;
    priv->RefPicList0_count     = 0;
    priv->RefPicList1_count     = 0;
    priv->nal_length_size       = 0;
    priv->width                 = 0;
    priv->height                = 0;
    priv->mb_x                  = 0;
    priv->mb_y                  = 0;
    priv->mb_width              = 0;
    priv->mb_height             = 0;
    priv->adapter               = NULL;
    priv->field_poc[0]          = 0;
    priv->field_poc[1]          = 0;
    priv->poc_msb               = 0;
    priv->poc_lsb               = 0;
    priv->prev_poc_msb          = 0;
    priv->prev_poc_lsb          = 0;
    priv->frame_num_offset      = 0;
    priv->prev_frame_num_offset = 0;
    priv->frame_num             = 0;
    priv->prev_frame_num        = 0;
    priv->is_constructed        = FALSE;
    priv->is_opened             = FALSE;
    priv->is_avc                = FALSE;
    priv->has_context           = FALSE;

    memset(priv->dpb, 0, sizeof(priv->dpb));
    memset(priv->short_ref, 0, sizeof(priv->short_ref));
    memset(priv->long_ref, 0, sizeof(priv->long_ref));
    memset(priv->RefPicList0, 0, sizeof(priv->RefPicList0));
    memset(priv->RefPicList1, 0, sizeof(priv->RefPicList1));
}

/**
 * gst_vaapi_decoder_h264_new:
 * @display: a #GstVaapiDisplay
 * @caps: a #GstCaps holding codec information
 *
 * Creates a new #GstVaapiDecoder for MPEG-2 decoding.  The @caps can
 * hold extra information like codec-data and pictured coded size.
 *
 * Return value: the newly allocated #GstVaapiDecoder object
 */
GstVaapiDecoder *
gst_vaapi_decoder_h264_new(GstVaapiDisplay *display, GstCaps *caps)
{
    GstVaapiDecoderH264 *decoder;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    decoder = g_object_new(
        GST_VAAPI_TYPE_DECODER_H264,
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
