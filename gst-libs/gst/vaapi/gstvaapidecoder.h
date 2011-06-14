/*
 *  gstvaapidecoder.h - VA decoder abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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
#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>

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

typedef enum _GstVaapiDecoderStatus             GstVaapiDecoderStatus;
typedef struct _GstVaapiDecoder                 GstVaapiDecoder;
typedef struct _GstVaapiDecoderPrivate          GstVaapiDecoderPrivate;
typedef struct _GstVaapiDecoderClass            GstVaapiDecoderClass;

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
 * @GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN: Unknown error.
 *
 * Decoder status for gst_vaapi_decoder_get_surface().
 */
enum _GstVaapiDecoderStatus {
    GST_VAAPI_DECODER_STATUS_SUCCESS = 0,
    GST_VAAPI_DECODER_STATUS_END_OF_STREAM,
    GST_VAAPI_DECODER_STATUS_ERROR_ALLOCATION_FAILED,
    GST_VAAPI_DECODER_STATUS_ERROR_INIT_FAILED,
    GST_VAAPI_DECODER_STATUS_ERROR_UNSUPPORTED_CODEC,
    GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA,
    GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE,
    GST_VAAPI_DECODER_STATUS_ERROR_INVALID_SURFACE,
    GST_VAAPI_DECODER_STATUS_ERROR_UNKNOWN = -1
};

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

    GstVaapiDecoderStatus (*decode)(GstVaapiDecoder *decoder, GstBuffer *buffer);
};

GType
gst_vaapi_decoder_get_type(void);

GstCaps *
gst_vaapi_decoder_get_caps(GstVaapiDecoder *decoder);

gboolean
gst_vaapi_decoder_put_buffer(GstVaapiDecoder *decoder, GstBuffer *buf);

GstVaapiSurfaceProxy *
gst_vaapi_decoder_get_surface(
    GstVaapiDecoder       *decoder,
    GstVaapiDecoderStatus *pstatus
);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_H */
