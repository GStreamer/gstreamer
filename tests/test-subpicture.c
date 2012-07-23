/*
 *  test-subpicture.c - Test GstVaapiSubpicture
 *
 *  Copyright (C) <2011> Intel Corporation
 *  Copyright (C) <2011> Collabora Ltd.
 *  Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
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

#include "config.h"
#include <string.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapisurface.h>
#include "output.h"
#include "test-mpeg2.h"
#include "test-subpicture-data.h"

typedef void (*GetVideoInfoFunc)(VideoDecodeInfo *info);

typedef struct _CodecDefs CodecDefs;
struct _CodecDefs {
    const gchar        *codec_str;
    GetVideoInfoFunc    get_video_info;
};

static const CodecDefs g_codec_defs[] = {
#define INIT_FUNCS(CODEC) { #CODEC, CODEC##_get_video_info }
    INIT_FUNCS(mpeg2),
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

static void
upload_image (guint8 *dst, const guint32 *src, guint size)
{
    guint i;

    for (i = 0; i < size; i += 4) {
        dst[i    ] = *src >> 24;
        dst[i + 1] = *src >> 16;
        dst[i + 2] = *src >> 8;
        dst[i + 3] = *src++;
    }
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay      *display;
    GstVaapiWindow       *window;
    GstVaapiDecoder      *decoder = NULL;
    GstCaps              *decoder_caps;
    GstStructure         *structure;
    GstVaapiDecoderStatus status;
    const CodecDefs      *codec;
    GstBuffer            *buffer;
    GstVaapiSurfaceProxy *proxy;
    GstVaapiSurface      *surface;
    VideoDecodeInfo       info;
    VideoSubpictureInfo   subinfo;
    GstVaapiImage        *subtitle_image;
    GstVaapiSubpicture   *subpicture;
    GstCaps              *argbcaps;
    GstVaapiRectangle     sub_rect;
    guint                 surf_width, surf_height;

    static const guint win_width  = 640;
    static const guint win_height = 480;

    if (!video_output_init(&argc, argv, g_options))
        g_error("failed to initialize video output subsystem");

    if (!g_codec_str)
        g_codec_str = g_strdup("mpeg2");

    g_print("Test %s decode\n", g_codec_str);
    codec = get_codec_defs(g_codec_str);
    if (!codec)
        g_error("no %s codec data found", g_codec_str);

    display = video_output_create_display(NULL);
    if (!display)
        g_error("could not create VA display");

    window = video_output_create_window(display, win_width, win_height);
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

    decoder = gst_vaapi_decoder_mpeg2_new(display, decoder_caps);
    if (!decoder)
        g_error("could not create video decoder");
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

    surface = gst_vaapi_surface_proxy_get_surface(proxy);
    if (!surface)
        g_error("could not get underlying surface");

    gst_vaapi_surface_get_size(surface, &surf_width, &surf_height);
    printf("surface size %dx%d\n", surf_width, surf_height);

    subpicture_get_info (&subinfo);

    /* Adding subpicture */
    argbcaps = gst_caps_new_simple ("video/x-raw-rgb",
              "endianness", G_TYPE_INT, G_BIG_ENDIAN,
              "bpp", G_TYPE_INT, 32,
              "red_mask", G_TYPE_INT, 0xff000000,
              "green_mask", G_TYPE_INT, 0x00ff0000,
              "blue_mask", G_TYPE_INT, 0x0000ff00,
              "alpha_mask", G_TYPE_INT, 0x000000ff,
              "width", G_TYPE_INT,  subinfo.width,
              "height", G_TYPE_INT, subinfo.height,
               NULL);

    buffer = gst_buffer_new_and_alloc (subinfo.data_size);
    upload_image (GST_BUFFER_DATA (buffer), subinfo.data, subinfo.data_size);
    gst_buffer_set_caps (buffer, argbcaps);

    subtitle_image = gst_vaapi_image_new (display,
      GST_VAAPI_IMAGE_RGBA, subinfo.width, subinfo.height);

    if (!gst_vaapi_image_update_from_buffer (subtitle_image, buffer, NULL))
        g_error ("could not update VA image with subtitle data");

    subpicture = gst_vaapi_subpicture_new (subtitle_image);

    /* We position it as a subtitle, centered at the bottom. */
    sub_rect.x = (surf_width - subinfo.width) / 2;
    sub_rect.y = surf_height - subinfo.height - 10;
    sub_rect.height = subinfo.height;
    sub_rect.width = subinfo.width;

    if (!gst_vaapi_surface_associate_subpicture (
         surface,
         subpicture,
         NULL,
         &sub_rect))
        g_error("could not associate subpicture");

    gst_vaapi_window_show(window);

    if (!gst_vaapi_window_put_surface(window,
                                      GST_VAAPI_SURFACE_PROXY_SURFACE(proxy),
                                      NULL,
                                      NULL,
                                      GST_VAAPI_PICTURE_STRUCTURE_FRAME))
        g_error("could not render surface");

    pause();

    gst_buffer_unref(buffer);
    g_object_unref(proxy);
    g_object_unref(decoder);
    g_object_unref(window);
    g_object_unref(display);
    g_free(g_codec_str);
    video_output_exit();
    return 0;
}
