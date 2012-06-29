/*
 *  gstvaapidecoder_mpeg4.h - MPEG-4 decoder
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

#ifndef GST_VAAPI_DECODER_MPEG4_H
#define GST_VAAPI_DECODER_MPEG4_H

#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DECODER_MPEG4 \
    (gst_vaapi_decoder_mpeg4_get_type())

#define GST_VAAPI_DECODER_MPEG4(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DECODER_MPEG4,   \
                                GstVaapiDecoderMpeg4))

#define GST_VAAPI_DECODER_MPEG4_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DECODER_MPEG4,      \
                             GstVaapiDecoderMpeg4Class))

#define GST_VAAPI_IS_DECODER_MPEG4(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DECODER_MPEG4))

#define GST_VAAPI_IS_DECODER_MPEG4_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DECODER_MPEG4))

#define GST_VAAPI_DECODER_MPEG4_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DECODER_MPEG4,    \
                               GstVaapiDecoderMpeg4Class))

typedef struct _GstVaapiDecoderMpeg4            GstVaapiDecoderMpeg4;
typedef struct _GstVaapiDecoderMpeg4Private     GstVaapiDecoderMpeg4Private;
typedef struct _GstVaapiDecoderMpeg4Class       GstVaapiDecoderMpeg4Class;

/**
 * GstVaapiDecoderMpeg4:
 *
 * A decoder based on Mpeg4.
 */
struct _GstVaapiDecoderMpeg4 {
    /*< private >*/
    GstVaapiDecoder parent_instance;

    GstVaapiDecoderMpeg4Private *priv;
};

/**
 * GstVaapiDecoderMpeg4Class:
 *
 * A decoder class based on Mpeg4.
 */
struct _GstVaapiDecoderMpeg4Class {
    /*< private >*/
    GstVaapiDecoderClass parent_class;
};

GType
gst_vaapi_decoder_mpeg4_get_type(void) G_GNUC_CONST;

GstVaapiDecoder *
gst_vaapi_decoder_mpeg4_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_MPEG4_H */
