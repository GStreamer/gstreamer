/*
 *  gstvaapidecoder_priv.h - VA decoder abstraction (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_DECODER_PRIV_H
#define GST_VAAPI_DECODER_PRIV_H

#include <glib.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_unit.h>
#include <gst/vaapi/gstvaapicontext.h>

G_BEGIN_DECLS

#define GST_VAAPI_DECODER_CAST(decoder) ((GstVaapiDecoder *)(decoder))

/**
 * GST_VAAPI_PARSER_STATE:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiParserState of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_PARSER_STATE
#define GST_VAAPI_PARSER_STATE(decoder) \
    (&GST_VAAPI_DECODER_CAST(decoder)->priv->parser_state)

/**
 * GST_VAAPI_DECODER_DISPLAY:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiDisplay of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_DISPLAY
#define GST_VAAPI_DECODER_DISPLAY(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->display

/**
 * GST_VAAPI_DECODER_CONTEXT:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiContext of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CONTEXT
#define GST_VAAPI_DECODER_CONTEXT(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->context

/**
 * GST_VAAPI_DECODER_CODEC:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiCodec of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC
#define GST_VAAPI_DECODER_CODEC(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->codec

/**
 * GST_VAAPI_DECODER_CODEC_STATE:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVideoCodecState holding codec state
 * for @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC_STATE
#define GST_VAAPI_DECODER_CODEC_STATE(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->codec_state

/**
 * GST_VAAPI_DECODER_CODEC_DATA:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstBuffer holding optional codec data
 * for @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC_DATA
#define GST_VAAPI_DECODER_CODEC_DATA(decoder) \
    GST_VAAPI_DECODER_CODEC_STATE(decoder)->codec_data

/**
 * GST_VAAPI_DECODER_CODEC_FRAME:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVideoCodecFrame holding decoder
 * units for the current frame.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC_FRAME
#define GST_VAAPI_DECODER_CODEC_FRAME(decoder) \
    GST_VAAPI_PARSER_STATE(decoder)->current_frame

/**
 * GST_VAAPI_DECODER_WIDTH:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the coded width of the picture
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_WIDTH
#define GST_VAAPI_DECODER_WIDTH(decoder) \
    GST_VAAPI_DECODER_CODEC_STATE(decoder)->info.width

/**
 * GST_VAAPI_DECODER_HEIGHT:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the coded height of the picture
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_HEIGHT
#define GST_VAAPI_DECODER_HEIGHT(decoder) \
    GST_VAAPI_DECODER_CODEC_STATE(decoder)->info.height

/* End-of-Stream buffer */
#define GST_BUFFER_FLAG_EOS (GST_BUFFER_FLAG_LAST + 0)

#define GST_BUFFER_IS_EOS(buffer) \
    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_EOS)

#define GST_VAAPI_DECODER_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER,        \
                                 GstVaapiDecoderPrivate))

typedef enum {
    GST_VAAPI_DECODER_STATUS_DROP_FRAME = -2
} GstVaapiDecoderStatusPrivate;

typedef struct _GstVaapiParserState GstVaapiParserState;
struct _GstVaapiParserState {
    GstVideoCodecFrame *current_frame;
    GstAdapter         *current_adapter;
    GstAdapter         *input_adapter;
    gint                input_offset2;
    GstAdapter         *output_adapter;
    GstVaapiDecoderUnit next_unit;
    guint               next_unit_pending       : 1;
    guint               at_eos                  : 1;
};

struct _GstVaapiDecoderPrivate {
    GstVaapiDisplay    *display;
    VADisplay           va_display;
    GstVaapiContext    *context;
    VAContextID         va_context;
    GstVaapiCodec       codec;
    GstVideoCodecState *codec_state;
    GQueue             *buffers;
    GQueue             *frames;
    GstVaapiParserState parser_state;
};

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_picture_size(
    GstVaapiDecoder    *decoder,
    guint               width,
    guint               height
);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_framerate(
    GstVaapiDecoder    *decoder,
    guint               fps_n,
    guint               fps_d
);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_pixel_aspect_ratio(
    GstVaapiDecoder    *decoder,
    guint               par_n,
    guint               par_d
);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_interlace_mode(GstVaapiDecoder *decoder,
    GstVideoInterlaceMode mode);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_interlaced(GstVaapiDecoder *decoder, gboolean interlaced);

G_GNUC_INTERNAL
gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder     *decoder,
    GstVaapiContextInfo *cip
);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_push_frame(GstVaapiDecoder *decoder,
    GstVideoCodecFrame *frame);

G_GNUC_INTERNAL
GstVaapiDecoderStatus
gst_vaapi_decoder_check_status(GstVaapiDecoder *decoder);

G_GNUC_INTERNAL
GstVaapiDecoderStatus
gst_vaapi_decoder_decode_codec_data(GstVaapiDecoder *decoder);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_PRIV_H */
