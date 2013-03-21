/*
 *  decoder.h - Decoder utilities for the tests
 *
 *  Copyright (C) 2013 Intel Corporation
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

#include "gst/vaapi/sysdeps.h"
#include <string.h>
#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>
#include "decoder.h"
#include "test-jpeg.h"
#include "test-mpeg2.h"
#include "test-mpeg4.h"
#include "test-h264.h"
#include "test-vc1.h"

typedef void (*GetVideoInfoFunc)(VideoDecodeInfo *info);

typedef struct _CodecDefs CodecDefs;
struct _CodecDefs {
    const gchar        *codec_str;
    GetVideoInfoFunc    get_video_info;
};

static const CodecDefs g_codec_defs[] = {
#define INIT_FUNCS(CODEC) { #CODEC, CODEC##_get_video_info }
    INIT_FUNCS(jpeg),
    INIT_FUNCS(mpeg2),
    INIT_FUNCS(mpeg4),
    INIT_FUNCS(h264),
    INIT_FUNCS(vc1),
#undef INIT_FUNCS
    { NULL, }
};

static const CodecDefs *
find_codec_defs(const gchar *codec_str)
{
    const CodecDefs *c;
    for (c = g_codec_defs; c->codec_str; c++)
        if (strcmp(codec_str, c->codec_str) == 0)
            return c;
    return NULL;
}

#define CODEC_DEFS_KEY "codec-defs"

static inline const CodecDefs *
get_codec_defs(GstVaapiDecoder *decoder)
{
    return g_object_get_data(G_OBJECT(decoder), CODEC_DEFS_KEY);
}

static inline void
set_codec_defs(GstVaapiDecoder *decoder, const CodecDefs *c)
{
    g_object_set_data(G_OBJECT(decoder), CODEC_DEFS_KEY, (gpointer)c);
}

GstVaapiDecoder *
decoder_new(GstVaapiDisplay *display, const gchar *codec_name)
{
    GstVaapiDecoder *decoder;
    const CodecDefs *codec;
    GstCaps *caps;
    VideoDecodeInfo info;

    if (!codec_name)
        codec_name = "h264";

    codec = find_codec_defs(codec_name);
    if (!codec) {
        GST_ERROR("failed to find %s codec data", codec_name);
        return NULL;
    }

    codec->get_video_info(&info);
    caps = gst_vaapi_profile_get_caps(info.profile);
    if (!caps) {
        GST_ERROR("failed to create decoder caps");
        return NULL;
    }

    if (info.width > 0 && info.height > 0)
        gst_caps_set_simple(caps,
            "width",  G_TYPE_INT, info.width,
            "height", G_TYPE_INT, info.height,
            NULL);

    switch (gst_vaapi_profile_get_codec(info.profile)) {
    case GST_VAAPI_CODEC_H264:
        decoder = gst_vaapi_decoder_h264_new(display, caps);
        break;
#if USE_JPEG_DECODER
    case GST_VAAPI_CODEC_JPEG:
        decoder = gst_vaapi_decoder_jpeg_new(display, caps);
        break;
#endif
    case GST_VAAPI_CODEC_MPEG2:
        decoder = gst_vaapi_decoder_mpeg2_new(display, caps);
        break;
    case GST_VAAPI_CODEC_MPEG4:
        decoder = gst_vaapi_decoder_mpeg4_new(display, caps);
        break;
    case GST_VAAPI_CODEC_VC1:
        decoder = gst_vaapi_decoder_vc1_new(display, caps);
        break;
    default:
        decoder = NULL;
        break;
    }
    gst_caps_unref(caps);
    if (!decoder) {
        GST_ERROR("failed to create %s decoder", codec->codec_str);
        return NULL;
    }

    set_codec_defs(decoder, codec);
    return decoder;
}

gboolean
decoder_put_buffers(GstVaapiDecoder *decoder)
{
    const CodecDefs *codec;
    VideoDecodeInfo info;
    GstBuffer *buffer;
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), FALSE);

    codec = get_codec_defs(decoder);
    g_return_val_if_fail(codec != NULL, FALSE);

    codec->get_video_info(&info);
    buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_READONLY,
        (guchar *)info.data, info.data_size, 0, info.data_size, NULL, NULL);
    if (!buffer) {
        GST_ERROR("failed to create encoded data buffer");
        return FALSE;
    }

    success = gst_vaapi_decoder_put_buffer(decoder, buffer);
    gst_buffer_unref(buffer);
    if (!success) {
        GST_ERROR("failed to send video data to the decoder");
        return FALSE;
    }

    if (!gst_vaapi_decoder_put_buffer(decoder, NULL)) {
        GST_ERROR("failed to submit <end-of-stream> to the decoder");
        return FALSE;
    }
    return TRUE;
}

GstVaapiSurface *
decoder_get_surface(GstVaapiDecoder *decoder)
{
    GstVaapiSurface *surface;
    GstVaapiSurfaceProxy *proxy;
    GstVaapiDecoderStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    status = gst_vaapi_decoder_get_surface(decoder, &proxy);
    if (status != GST_VAAPI_DECODER_STATUS_SUCCESS) {
        GST_ERROR("failed to get decoded surface (decoder status %d)", status);
        return NULL;
    }

    /* Note: we only have a single I-frame to decode, so this is fine
       to just release the surface proxy right away */
    surface = gst_vaapi_surface_proxy_get_surface(proxy);
    gst_vaapi_surface_proxy_unref(proxy);
    return surface;
}

const gchar *
decoder_get_codec_name(GstVaapiDecoder *decoder)
{
    const CodecDefs *codec;

    g_return_val_if_fail(GST_VAAPI_IS_DECODER(decoder), NULL);

    codec = get_codec_defs(decoder);
    g_return_val_if_fail(codec != NULL, FALSE);

    return codec->codec_str;
}
