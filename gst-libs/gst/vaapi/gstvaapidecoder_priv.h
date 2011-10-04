/*
 *  gstvaapidecoder_priv.h - VA decoder abstraction (private definitions)
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

#ifndef GST_VAAPI_DECODER_PRIV_H
#define GST_VAAPI_DECODER_PRIV_H

#include <glib.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapicontext.h>

G_BEGIN_DECLS

#define GST_VAAPI_DECODER_CAST(decoder) ((GstVaapiDecoder *)(decoder))

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
 * GST_VAAPI_DECODER_CODEC_DATA:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the #GstBuffer holding optional codec data
 * for @decoder.
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_CODEC_DATA
#define GST_VAAPI_DECODER_CODEC_DATA(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->codec_data

/**
 * GST_VAAPI_DECODER_WIDTH:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the coded width of the picture
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_WIDTH
#define GST_VAAPI_DECODER_WIDTH(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->width

/**
 * GST_VAAPI_DECODER_HEIGHT:
 * @decoder: a #GstVaapiDecoder
 *
 * Macro that evaluates to the coded height of the picture
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_DECODER_HEIGHT
#define GST_VAAPI_DECODER_HEIGHT(decoder) \
    GST_VAAPI_DECODER_CAST(decoder)->priv->height

/* End-of-Stream buffer */
#define GST_BUFFER_FLAG_EOS (GST_BUFFER_FLAG_LAST + 0)

#define GST_BUFFER_IS_EOS(buffer) \
    GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_EOS)

#define GST_VAAPI_DECODER_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_DECODER,        \
                                 GstVaapiDecoderPrivate))

typedef enum _GstVaapiPictureType       GstVaapiPictureType;
typedef struct _GstVaapiCodecInfo       GstVaapiCodecInfo;
typedef struct _GstVaapiPicture         GstVaapiPicture;
typedef struct _GstVaapiSlice           GstVaapiSlice;
typedef struct _GstVaapiIqMatrix        GstVaapiIqMatrix;
typedef struct _GstVaapiBitPlane        GstVaapiBitPlane;

enum _GstVaapiPictureType {
    GST_VAAPI_PICTURE_TYPE_NONE = 0,        // Undefined
    GST_VAAPI_PICTURE_TYPE_I,               // Intra
    GST_VAAPI_PICTURE_TYPE_P,               // Predicted
    GST_VAAPI_PICTURE_TYPE_B,               // Bi-directional predicted
    GST_VAAPI_PICTURE_TYPE_S,               // S(GMC)-VOP (MPEG-4)
    GST_VAAPI_PICTURE_TYPE_SI,              // Switching Intra
    GST_VAAPI_PICTURE_TYPE_SP,              // Switching Predicted
    GST_VAAPI_PICTURE_TYPE_BI,              // BI type (VC-1)
};

enum {
    GST_VAAPI_PICTURE_SKIPPED   = 1 << 0,   // Skipped frame
    GST_VAAPI_PICTURE_REFERENCE = 1 << 1,   // Reference frame
};

#define GST_VAAPI_PICTURE(picture) \
    ((GstVaapiPicture *)(picture))

#define GST_VAAPI_PICTURE_IS_REFERENCE(picture) \
    ((GST_VAAPI_PICTURE(picture)->flags & GST_VAAPI_PICTURE_REFERENCE) != 0)

struct _GstVaapiCodecInfo {
    guint               pic_size;           // GstVaapiPicture size
    guint               slice_size;         // GstVaapiSlice size
    guint               pic_param_size;     // VAPictureParameterBuffer size
    guint               slice_param_size;   // VASliceParameterBuffer size
    guint               iq_matrix_size;     // VAIQMatrixBuffer size
};

struct _GstVaapiPicture {
    GstVaapiPictureType type;
    guint               flags;
    VASurfaceID         surface_id;
    GstVaapiSurface    *surface;
    VABufferID          param_id;
    void               *param;
    GPtrArray          *slices;
    GstVaapiIqMatrix   *iq_matrix;
    GstVaapiBitPlane   *bitplane;
    GstClockTime        pts;
};

struct _GstVaapiSlice {
    VABufferID          param_id;
    void               *param;
    VABufferID          data_id;
};

struct _GstVaapiIqMatrix {
    VABufferID          param_id;
    void               *param;
};

struct _GstVaapiBitPlane {
    VABufferID          data_id;
    guint8             *data;
};

struct _GstVaapiDecoderPrivate {
    GstVaapiDisplay    *display;
    VADisplay           va_display;
    GstVaapiContext    *context;
    VAContextID         va_context;
    GstCaps            *caps;
    GstVaapiCodec       codec;
    GstBuffer          *codec_data;
    GstVaapiCodecInfo   codec_info;
    guint               width;
    guint               height;
    guint               fps_n;
    guint               fps_d;
    guint               par_n;
    guint               par_d;
    GQueue             *buffers;
    GQueue             *surfaces;
};

void
gst_vaapi_decoder_set_picture_size(
    GstVaapiDecoder    *decoder,
    guint               width,
    guint               height
) attribute_hidden;

void
gst_vaapi_decoder_set_framerate(
    GstVaapiDecoder    *decoder,
    guint               fps_n,
    guint               fps_d
) attribute_hidden;

void
gst_vaapi_decoder_set_pixel_aspect_ratio(
    GstVaapiDecoder    *decoder,
    guint               par_n,
    guint               par_d
) attribute_hidden;

gboolean
gst_vaapi_decoder_ensure_context(
    GstVaapiDecoder    *decoder,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
) attribute_hidden;

gboolean
gst_vaapi_decoder_push_buffer_sub(
    GstVaapiDecoder *decoder,
    GstBuffer       *buffer,
    guint            offset,
    guint            size
) attribute_hidden;

gboolean
gst_vaapi_decoder_push_surface(
    GstVaapiDecoder *decoder,
    GstVaapiSurface *surface,
    GstClockTime     timestamp
) attribute_hidden;

gboolean
gst_vaapi_decoder_push_surface_proxy(
    GstVaapiDecoder      *decoder,
    GstVaapiSurfaceProxy *proxy,
    GstClockTime          timestamp
) attribute_hidden;

GstVaapiPicture *
gst_vaapi_decoder_new_picture(GstVaapiDecoder *decoder)
    attribute_hidden;

void
gst_vaapi_decoder_free_picture(GstVaapiDecoder *decoder, GstVaapiPicture *picture)
    attribute_hidden;

GstVaapiIqMatrix *
gst_vaapi_decoder_new_iq_matrix(GstVaapiDecoder *decoder)
    attribute_hidden;

GstVaapiBitPlane *
gst_vaapi_decoder_new_bitplane(GstVaapiDecoder *decoder, guint size)
    attribute_hidden;

GstVaapiSlice *
gst_vaapi_decoder_new_slice(
    GstVaapiDecoder *decoder,
    GstVaapiPicture *picture,
    guchar          *buf,
    guint            buf_size
) attribute_hidden;

gboolean
gst_vaapi_decoder_decode_picture(
    GstVaapiDecoder *decoder,
    GstVaapiPicture *picture
) attribute_hidden;

G_END_DECLS

#endif /* GST_VAAPI_DECODER_PRIV_H */
