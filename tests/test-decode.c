/*
 *  test-decode.c - Test GstVaapiDecoder
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <string.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_ffmpeg.h>
#include <gst/vaapi/gstvaapisurface.h>
#include "test-mpeg2.h"
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
    INIT_FUNCS(mpeg2),
    INIT_FUNCS(h264),
    INIT_FUNCS(vc1),
#undef INIT_FUNCS
    { NULL, }
};

static const CodecDefs *
get_codec_defs(const gchar *codec_str)
{
    const CodecDefs *c;
    for (c = g_codec_defs; c->codec_str; c++)
        if (strcmp(codec_str, c->codec_str) == 0)
            return c;
    return NULL;
}

static inline void pause(void)
{
    g_print("Press any key to continue...\n");
    getchar();
}

static gchar *g_codec_str;

static GOptionEntry g_options[] = {
    { "codec", 'c',
      0,
      G_OPTION_ARG_STRING, &g_codec_str,
      "codec to test", NULL },
    { NULL, }
};

int
main(int argc, char *argv[])
{
    GOptionContext       *options;
    GstVaapiDisplay      *display;
    GstVaapiWindow       *window;
    GstVaapiDecoder      *decoder;
    GstCaps              *decoder_caps;
    GstStructure         *structure;
    GstVaapiDecoderStatus status;
    const CodecDefs      *codec;
    GstBuffer            *buffer;
    GstVaapiSurfaceProxy *proxy;
    VideoDecodeInfo       info;

    static const guint win_width  = 640;
    static const guint win_height = 480;

    gst_init(&argc, &argv);

    options = g_option_context_new(" - test-decode options");
    g_option_context_add_main_entries(options, g_options, NULL);
    g_option_context_parse(options, &argc, &argv, NULL);
    g_option_context_free(options);

    if (!g_codec_str)
        g_codec_str = g_strdup("h264");

    g_print("Test %s decode\n", g_codec_str);
    codec = get_codec_defs(g_codec_str);
    if (!codec)
        g_error("no %s codec data found", g_codec_str);

    display = gst_vaapi_display_x11_new(NULL);
    if (!display)
        g_error("could not create VA display");

    window = gst_vaapi_window_x11_new(display, win_width, win_height);
    if (!window)
        g_error("could not create window");

    codec->get_video_info(&info);
    decoder_caps = gst_vaapi_profile_get_caps(info.profile);
    if (!decoder_caps)
        g_error("could not create decoder caps");

    structure = gst_caps_get_structure(decoder_caps, 0);
    if (info.width > 0 && info.height > 0)
        gst_structure_set(
            structure,
            "width",  G_TYPE_INT, info.width,
            "height", G_TYPE_INT, info.height,
            NULL
        );

    decoder = gst_vaapi_decoder_ffmpeg_new(display, decoder_caps);
    if (!decoder)
        g_error("could not create FFmpeg decoder");
    gst_caps_unref(decoder_caps);

    buffer = gst_buffer_new();
    if (!buffer)
        g_error("could not create encoded data buffer");
    gst_buffer_set_data(buffer, (guchar *)info.data, info.data_size);

    if (!gst_vaapi_decoder_put_buffer(decoder, buffer))
        g_error("could not send video data to the decoder");
    gst_buffer_unref(buffer);

    if (!gst_vaapi_decoder_put_buffer(decoder, NULL))
        g_error("could not send EOS to the decoder");

    proxy = gst_vaapi_decoder_get_surface(decoder, &status);
    if (!proxy)
        g_error("could not get decoded surface (decoder status %d)", status);

    gst_vaapi_window_show(window);

    if (!gst_vaapi_window_put_surface(window,
                                      GST_VAAPI_SURFACE_PROXY_SURFACE(proxy),
                                      NULL,
                                      NULL,
                                      GST_VAAPI_PICTURE_STRUCTURE_FRAME))
        g_error("could not render surface");

    pause();

    g_object_unref(proxy);
    g_object_unref(decoder);
    g_object_unref(window);
    g_object_unref(display);
    g_free(g_codec_str);
    gst_deinit();
    return 0;
}
