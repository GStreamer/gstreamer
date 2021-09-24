/*
 *  gstvaapidecoder_priv.h - VA decoder abstraction (private definitions)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_PRIV_H
#define GST_VAAPI_DECODER_PRIV_H

#include "sysdeps.h"
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_unit.h>
#include <gst/vaapi/gstvaapicontext.h>

G_BEGIN_DECLS

#define GST_VAAPI_DECODER_CAST(decoder) \
    ((GstVaapiDecoder *)(decoder))

#define GST_VAAPI_DECODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VAAPI_DECODER, GstVaapiDecoderClass))

#define GST_VAAPI_IS_DECODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_DECODER))

#define GST_VAAPI_DECODER_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_DECODER, GstVaapiDecoderClass))

typedef struct _GstVaapiDecoderClass GstVaapiDecoderClass;

/**
 * GST_VAAPI_PARSER_STATE:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiParserState of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_PARSER_STATE
#define GST_VAAPI_PARSER_STATE(decoder) \
    (&GST_VAAPI_DECODER_CAST(decoder)->parser_state)

/**
 * GST_VAAPI_DECODER_DISPLAY:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiDisplay of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_DISPLAY
#define GST_VAAPI_DECODER_DISPLAY(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->display

/**
 * GST_VAAPI_DECODER_CONTEXT:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiContext of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CONTEXT
#define GST_VAAPI_DECODER_CONTEXT(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->context

/**
 * GST_VAAPI_DECODER_CODEC:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstVaapiCodec of @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC
#define GST_VAAPI_DECODER_CODEC(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->codec

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
    GST_VAAPI_DECODER_CAST(decoder)->codec_state

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
struct _GstVaapiParserState
{
  GstVideoCodecFrame *current_frame;
  guint32 current_frame_number;
  GstAdapter *current_adapter;
  GstAdapter *input_adapter;
  gint input_offset1;
  gint input_offset2;
  GstAdapter *output_adapter;
  GstVaapiDecoderUnit next_unit;
  guint next_unit_pending:1;
  guint at_eos:1;
};

/**
 * GstVaapiDecoder:
 *
 * A VA decoder base instance.
 */
struct _GstVaapiDecoder
{
  /*< private >*/
  GstObject parent_instance;

  gpointer user_data;
  GstVaapiDisplay *display;
  VADisplay va_display;
  GstVaapiContext *context;
  VAContextID va_context;
  GstVaapiCodec codec;
  GstVideoCodecState *codec_state;
  GAsyncQueue *buffers;
  GAsyncQueue *frames;
  GstVaapiParserState parser_state;
  GstVaapiDecoderStateChangedFunc codec_state_changed_func;
  gpointer codec_state_changed_data;
};

/**
 * GstVaapiDecoderClass:
 *
 * A VA decoder base class.
 */
struct _GstVaapiDecoderClass
{
  /*< private >*/
  GstObjectClass parent_class;

  GstVaapiDecoderStatus (*parse) (GstVaapiDecoder * decoder,
      GstAdapter * adapter, gboolean at_eos,
      struct _GstVaapiDecoderUnit * unit);
  GstVaapiDecoderStatus (*decode) (GstVaapiDecoder * decoder,
      struct _GstVaapiDecoderUnit * unit);
  GstVaapiDecoderStatus (*start_frame) (GstVaapiDecoder * decoder,
      struct _GstVaapiDecoderUnit * unit);
  GstVaapiDecoderStatus (*end_frame) (GstVaapiDecoder * decoder);
  GstVaapiDecoderStatus (*flush) (GstVaapiDecoder * decoder);
  GstVaapiDecoderStatus (*reset) (GstVaapiDecoder * decoder);
  GstVaapiDecoderStatus (*decode_codec_data) (GstVaapiDecoder * decoder,
      const guchar * buf, guint buf_size);
};

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_picture_size (GstVaapiDecoder * decoder,
    guint width, guint height);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_framerate (GstVaapiDecoder * decoder,
    guint fps_n, guint fps_d);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_pixel_aspect_ratio (GstVaapiDecoder * decoder,
    guint par_n, guint par_d);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_interlace_mode (GstVaapiDecoder * decoder,
    GstVideoInterlaceMode mode);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_interlaced (GstVaapiDecoder * decoder,
    gboolean interlaced);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_set_multiview_mode (GstVaapiDecoder * decoder,
    gint views, GstVideoMultiviewMode mv_mode, GstVideoMultiviewFlags mv_flags);

G_GNUC_INTERNAL
gboolean
gst_vaapi_decoder_ensure_context (GstVaapiDecoder * decoder,
    GstVaapiContextInfo * cip);

G_GNUC_INTERNAL
void
gst_vaapi_decoder_push_frame (GstVaapiDecoder * decoder,
    GstVideoCodecFrame * frame);

G_GNUC_INTERNAL
GstVaapiDecoderStatus
gst_vaapi_decoder_decode_codec_data (GstVaapiDecoder * decoder);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_PRIV_H */
