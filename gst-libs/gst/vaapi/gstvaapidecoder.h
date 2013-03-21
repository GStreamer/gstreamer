/*
 *  gstvaapidecoder.h - VA decoder abstraction
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

#ifndef GST_VAAPI_DECODER_H
#define GST_VAAPI_DECODER_H

#include <gst/gstbuffer.h>
#include <gst/base/gstadapter.h>
#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>
#include <gst/video/gstvideoutils.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DECODER \
    (gst_vaapi_decoder_get_type())

#define GST_VAAPI_DECODER(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_DECODER, \
                                GstVaapiDecoder))

#define GST_VAAPI_DECODER_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_DECODER,    \
                             GstVaapiDecoderClass))

#define GST_VAAPI_IS_DECODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DECODER))

#define GST_VAAPI_IS_DECODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DECODER))

#define GST_VAAPI_DECODER_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_DECODER,  \
                               GstVaapiDecoderClass))

typedef struct _GstVaapiDecoder                 GstVaapiDecoder;
typedef struct _GstVaapiDecoderPrivate          GstVaapiDecoderPrivate;
typedef struct _GstVaapiDecoderClass            GstVaapiDecoderClass;
        struct _GstVaapiDecoderUnit;

/**
 * GstVaapiDecoderStatus:
 * @GST_VAAPI_DECODER_STATUS_SUCCESS: Success.
 * @GST_VAAPI_DECODER_STATUS_END_OF_STREAM: End-Of-Stream.
 * @GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED: No memory left.
 * @GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED: Decoder initialization failure.
 * @GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC: Unsupported codec.
 * @GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA: Not enough input data to decode.
 * @GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE: No surface left to hold the decoded picture.
 * @GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE: Invalid surface.
 * @GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER: Invalid or unsupported bitstream data.
 * @GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE: Unsupported codec profile.
 * @GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT: Unsupported chroma format.
 * @GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER: Unsupported parameter.
 * @GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN: Unknown error.
 *
 * Decoder status for gst_vaapi_decoder_get_surface().
 */
typedef enum {
    GST_VAAPI_DECODER_STATUS_SUCCESS = 0,
    GST_VAAPI_DECODER_STATUS_END_OF_STREAM,
    GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED,
    GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED,
    GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC,
    GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA,
    GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE,
    GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE,
    GST_VAAPI_DECODER_STATUS_ERROR_BITSTREAM_PARSER,
    GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_PROFILE,
    GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CHROMA_FORMAT,
    GST_VAAPI_DECODER_STATUS_ERROR_INVALID_PARAMETER,
    GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN = -1
} GstVaapiDecoderStatus;

/**
 * GstVaapiDecoder:
 *
 * A VA decoder base instance.
 */
struct _GstVaapiDecoder {
    /*< private >*/
    GObject parent_instance;

    GstVaapiDecoderPrivate *priv;
};

/**
 * GstVaapiDecoderClass:
 *
 * A VA decoder base class.
 */
struct _GstVaapiDecoderClass {
    /*< private >*/
    GObjectClass parent_class;

    GstVaapiDecoderStatus (*parse)(GstVaapiDecoder *decoder,
        GstAdapter *adapter, gboolean at_eos,
        struct _GstVaapiDecoderUnit *unit);
    GstVaapiDecoderStatus (*decode)(GstVaapiDecoder *decoder,
        struct _GstVaapiDecoderUnit *unit);
    GstVaapiDecoderStatus (*start_frame)(GstVaapiDecoder *decoder,
        struct _GstVaapiDecoderUnit *unit);
    GstVaapiDecoderStatus (*end_frame)(GstVaapiDecoder *decoder);
    GstVaapiDecoderStatus (*flush)(GstVaapiDecoder *decoder);
    GstVaapiDecoderStatus (*decode_codec_data)(GstVaapiDecoder *decoder,
        const guchar *buf, guint buf_size);
};

GType
gst_vaapi_decoder_get_type(void) G_GNUC_CONST;

GstVaapiCodec
gst_vaapi_decoder_get_codec(GstVaapiDecoder *decoder);

GstVideoCodecState *
gst_vaapi_decoder_get_codec_state(GstVaapiDecoder *decoder);

GstCaps *
gst_vaapi_decoder_get_caps(GstVaapiDecoder *decoder);

gboolean
gst_vaapi_decoder_put_buffer(GstVaapiDecoder *decoder, GstBuffer *buf);

GstVaapiDecoderStatus
gst_vaapi_decoder_get_surface(GstVaapiDecoder *decoder,
    GstVaapiSurfaceProxy **out_proxy_ptr);

GstVaapiDecoderStatus
gst_vaapi_decoder_get_frame(GstVaapiDecoder *decoder,
    GstVideoCodecFrame **out_frame_ptr);

GstVaapiDecoderStatus
gst_vaapi_decoder_parse(GstVaapiDecoder *decoder,
    GstVideoCodecFrame *frame, GstAdapter *adapter, gboolean at_eos,
    guint *got_unit_size_ptr, gboolean *got_frame_ptr);

GstVaapiDecoderStatus
gst_vaapi_decoder_decode(GstVaapiDecoder *decoder, GstVideoCodecFrame *frame);

GstVaapiDecoderStatus
gst_vaapi_decoder_flush(GstVaapiDecoder *decoder);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_H */
