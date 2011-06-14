/*
 *  gstvaapidecoder_ffmpeg.h - FFmpeg-based decoder
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

#ifndef GST_VAAPI_DECODER_FFMPEG_H
#define GST_VAAPI_DECODER_FFMPEG_H

#include <gst/vaapi/gstvaapidecoder.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_DECODER_FFMPEG \
    (gst_vaapi_decoder_ffmpeg_get_type())

#define GST_VAAPI_DECODER_FFMPEG(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DECODER_FFMPEG,  \
                                GstVaapiDecoderFfmpeg))

#define GST_VAAPI_DECODER_FFMPEG_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DECODER_FFMPEG,     \
                             GstVaapiDecoderFfmpegClass))

#define GST_VAAPI_IS_DECODER_FFMPEG(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DECODER_FFMPEG))

#define GST_VAAPI_IS_DECODER_FFMPEG_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DECODER_FFMPEG))

#define GST_VAAPI_DECODER_FFMPEG_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DECODER_FFMPEG,   \
                               GstVaapiDecoderFfmpegClass))

typedef struct _GstVaapiDecoderFfmpeg           GstVaapiDecoderFfmpeg;
typedef struct _GstVaapiDecoderFfmpegPrivate    GstVaapiDecoderFfmpegPrivate;
typedef struct _GstVaapiDecoderFfmpegClass      GstVaapiDecoderFfmpegClass;

/**
 * GstVaapiDecoderFfmpeg:
 *
 * A decoder based on FFmpeg.
 */
struct _GstVaapiDecoderFfmpeg {
    /*< private >*/
    GstVaapiDecoder parent_instance;

    GstVaapiDecoderFfmpegPrivate *priv;
};

/**
 * GstVaapiDecoderFfmpegClass:
 *
 * A decoder class based on FFmpeg.
 */
struct _GstVaapiDecoderFfmpegClass {
    /*< private >*/
    GstVaapiDecoderClass parent_class;
};

GType
gst_vaapi_decoder_ffmpeg_get_type(void);

GstVaapiDecoder *
gst_vaapi_decoder_ffmpeg_new(GstVaapiDisplay *display, GstCaps *caps);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_FFMPEG_H */
