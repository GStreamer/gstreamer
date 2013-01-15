/*
 *  simple-decoder.c - Simple Decoder Application
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

/*
 * This is a really simple decoder application that only accepts raw
 * bitstreams. So, it may be needed to suggest what codec to use to
 * the application.
 */

#include "config.h"
#include <stdarg.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>
#include <gst/vaapi/gstvaapivideometa.h>
#include <gst/vaapi/gstvaapiwindow.h>
#include "codec.h"
#include "output.h"

static gchar *g_codec_str;

static GOptionEntry g_options[] = {
    { "codec", 'c',
      0,
      G_OPTION_ARG_STRING, &g_codec_str,
      "suggested codec", NULL },
    { NULL, }
};

typedef enum {
    APP_RUNNING,
    APP_GOT_EOS,
    APP_GOT_ERROR,
} AppEvent;

typedef enum {
    APP_ERROR_NONE,
    APP_ERROR_DECODER,
    APP_ERROR_RENDERER,
} AppError;

typedef struct {
    GMutex              mutex;
    GMappedFile        *file;
    gchar              *file_name;
    guint               file_offset;
    guint               file_size;
    guchar             *file_data;
    GstVaapiDisplay    *display;
    GstVaapiDecoder    *decoder;
    GThread            *decoder_thread;
    volatile gboolean   decoder_thread_cancel;
    GCond               decoder_ready;
    GAsyncQueue        *decoder_queue;
    GstVaapiCodec       codec;
    GstVideoCodecState  codec_state;
    GstVaapiWindow     *window;
    GThread            *render_thread;
    volatile gboolean   render_thread_cancel;
    GstBuffer          *last_buffer;
    GError             *error;
    AppEvent            event;
    GCond               event_cond;
} App;

#define APP_ERROR app_error_quark()
static GQuark
app_error_quark(void)
{
    static gsize g_quark;

    if (g_once_init_enter(&g_quark)) {
        gsize quark = (gsize)g_quark_from_static_string("AppError");
        g_once_init_leave(&g_quark, quark);
    }
    return g_quark;
}

static void
app_send_error(App *app, GError *error)
{
    g_mutex_lock(&app->mutex);
    app->error = error;
    app->event = APP_GOT_ERROR;
    g_cond_signal(&app->event_cond);
    g_mutex_unlock(&app->mutex);
}

static void
app_send_eos(App *app)
{
    g_mutex_lock(&app->mutex);
    app->event = APP_GOT_EOS;
    g_cond_signal(&app->event_cond);
    g_mutex_unlock(&app->mutex);
}

static const gchar *
get_decoder_status_string(GstVaapiDecoderStatus status)
{
    const gchar *str;

#define DEFINE_STATUS(status, status_string) \
    case GST_VAAPI_DECODER_STATUS_##status:  \
        str = status_string;                 \
        break

    switch (status) {
        DEFINE_STATUS(SUCCESS,                  "<success>");
        DEFINE_STATUS(END_OF_STREAM,            "<EOS>");
        DEFINE_STATUS(ERROR_ALLOCATION_FAILED,  "allocation failed");
        DEFINE_STATUS(ERROR_INIT_FAILED,        "initialization failed");
        DEFINE_STATUS(ERROR_UNSUPPORTED_CODEC,  "unsupported codec");
        DEFINE_STATUS(ERROR_NO_DATA,            "not enough data");
        DEFINE_STATUS(ERROR_NO_SURFACE,         "no surface vailable");
        DEFINE_STATUS(ERROR_INVALID_SURFACE,    "invalid surface");
        DEFINE_STATUS(ERROR_BITSTREAM_PARSER,   "bitstream parser error");
        DEFINE_STATUS(ERROR_UNSUPPORTED_PROFILE,
                      "unsupported profile");
        DEFINE_STATUS(ERROR_UNSUPPORTED_CHROMA_FORMAT,
                      "unsupported chroma-format");
        DEFINE_STATUS(ERROR_INVALID_PARAMETER,  "invalid parameter");
    default:
        str = "<unknown>";
        break;
    }
#undef DEFINE_STATUS

    return str;
}

static const gchar *
get_error_string(AppError error)
{
    const gchar *str;

#define DEFINE_ERROR(error, error_string)       \
    case APP_ERROR_##error:                     \
        str = error_string;                     \
        break

    switch (error) {
        DEFINE_ERROR(NONE,      "<none>");
        DEFINE_ERROR(DECODER,   "decoder");
        DEFINE_ERROR(RENDERER,  "renderer");
    default:
        str = "unknown";
        break;
    }
#undef DEFINE_ERROR

    return str;
}

static void
decoder_release(App *app)
{
    g_mutex_lock(&app->mutex);
    g_cond_signal(&app->decoder_ready);
    g_mutex_unlock(&app->mutex);
}

static gpointer
decoder_thread(gpointer data)
{
    App * const app = data;
    GError *error = NULL;
    GstVaapiDecoderStatus status;
    GstVaapiSurfaceProxy *proxy;
    GstVaapiVideoMeta *meta;
    GstBuffer *buffer;
    gboolean got_surface;
    gint64 end_time;
    guint ofs;

    g_print("Decoder thread started\n");

#define SEND_ERROR(...)                                                 \
    do {                                                                \
        error = g_error_new(APP_ERROR, APP_ERROR_DECODER, __VA_ARGS__); \
        goto send_error;                                                \
    } while (0)

    ofs = 0;
    while (!app->decoder_thread_cancel) {
        if (G_UNLIKELY(ofs == app->file_size))
            buffer = NULL;
        else {
            buffer = gst_buffer_new();
            if (!buffer)
                SEND_ERROR("failed to allocate new buffer");

            GST_BUFFER_DATA(buffer) = app->file_data + ofs;
            GST_BUFFER_SIZE(buffer) = MIN(4096, app->file_size - ofs);
            ofs += GST_BUFFER_SIZE(buffer);
        }
        if (!gst_vaapi_decoder_put_buffer(app->decoder, buffer))
            SEND_ERROR("failed to push buffer to decoder");
        gst_buffer_replace(&buffer, NULL);

    get_surface:
        status = gst_vaapi_decoder_get_surface(app->decoder, &proxy);
        switch (status) {
        case GST_VAAPI_DECODER_STATUS_SUCCESS:
            gst_vaapi_surface_proxy_set_user_data(proxy,
                app, (GDestroyNotify)decoder_release);
            meta = gst_vaapi_video_meta_new_with_surface_proxy(proxy);
            gst_vaapi_surface_proxy_unref(proxy);
            if (!meta)
                SEND_ERROR("failed to allocate video meta");
            buffer = gst_buffer_new();
            if (!buffer)
                SEND_ERROR("failed to allocate output buffer");
            gst_buffer_set_vaapi_video_meta(buffer, meta);
            gst_vaapi_video_meta_unref(meta);
            g_async_queue_push(app->decoder_queue, buffer);
            break;
        case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
            /* nothing to do, just continue to the next iteration */
            break;
        case GST_VAAPI_DECODER_STATUS_END_OF_STREAM:
            goto send_eos;
        case GST_VAAPI_DECODER_STATUS_ERROR_NO_SURFACE:
            end_time = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
            g_mutex_lock(&app->mutex);
            got_surface = g_cond_wait_until(&app->decoder_ready, &app->mutex,
                end_time);
            g_mutex_unlock(&app->mutex);
            if (got_surface)
                goto get_surface;
            SEND_ERROR("failed to acquire a surface within one second");
            break;
        default:
            SEND_ERROR("%s", get_decoder_status_string(status));
            break;
        }
    }
    return NULL;

#undef SEND_ERROR

send_eos:
    app_send_eos(app);
    return NULL;

send_error:
    app_send_error(app, error);
    return NULL;
}

static gboolean
start_decoder(App *app)
{
    GstCaps *caps;

    app->file = g_mapped_file_new(app->file_name, FALSE, NULL);
    if (!app->file)
        return FALSE;

    app->file_size = g_mapped_file_get_length(app->file);
    app->file_data = (guint8 *)g_mapped_file_get_contents(app->file);
    if (!app->file_data)
        return FALSE;

    caps = caps_from_codec(app->codec);
    switch (app->codec) {
    case GST_VAAPI_CODEC_H264:
        app->decoder = gst_vaapi_decoder_h264_new(app->display, caps);
        break;
#if USE_JPEG_DECODER
    case GST_VAAPI_CODEC_JPEG:
        app->decoder = gst_vaapi_decoder_jpeg_new(app->display, caps);
        break;
#endif
    case GST_VAAPI_CODEC_MPEG2:
        app->decoder = gst_vaapi_decoder_mpeg2_new(app->display, caps);
        break;
    case GST_VAAPI_CODEC_MPEG4:
        app->decoder = gst_vaapi_decoder_mpeg4_new(app->display, caps);
        break;
    case GST_VAAPI_CODEC_VC1:
        app->decoder = gst_vaapi_decoder_vc1_new(app->display, caps);
        break;
    default:
        app->decoder = NULL;
        break;
    }
    if (!app->decoder)
        return FALSE;

    app->decoder_thread = g_thread_create(decoder_thread, app, TRUE, NULL);
    if (!app->decoder_thread)
        return FALSE;
    return TRUE;
}

static gboolean
stop_decoder(App *app)
{
    app->decoder_thread_cancel = TRUE;
    g_thread_join(app->decoder_thread);
    g_print("Decoder thread stopped\n");
    return TRUE;
}

static gboolean
renderer_process(App *app, GstBuffer *buffer)
{
    GError *error = NULL;
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;

#define SEND_ERROR(...)                                                 \
    do {                                                                \
        error = g_error_new(APP_ERROR, APP_ERROR_RENDERER, __VA_ARGS__); \
        goto send_error;                                                \
    } while (0)

    meta = gst_buffer_get_vaapi_video_meta(buffer);
    if (!meta)
        SEND_ERROR("failed to get video meta");

    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface)
        SEND_ERROR("failed to get decoded surface from video meta");

    if (!gst_vaapi_window_put_surface(app->window, surface, NULL, NULL,
            GST_VAAPI_PICTURE_STRUCTURE_FRAME))
        SEND_ERROR("failed to render surface %" GST_VAAPI_ID_FORMAT,
                   GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));

    gst_buffer_replace(&app->last_buffer, buffer);
    gst_buffer_unref(buffer);
    return TRUE;

#undef SEND_ERROR

send_error:
    app_send_error(app, error);
    return FALSE;
}

static gpointer
renderer_thread(gpointer data)
{
    App * const app = data;
    GstBuffer *buffer;

    g_print("Render thread started\n");

    while (!app->render_thread_cancel) {
        buffer = g_async_queue_timeout_pop(app->decoder_queue, 1000000);
        if (buffer && !renderer_process(app, buffer))
            break;
    }
    return NULL;
}

static gboolean
flush_decoder_queue(App *app)
{
    GstBuffer *buffer;

    /* Flush pending surfaces */
    do {
        buffer = g_async_queue_try_pop(app->decoder_queue);
        if (!buffer)
            return TRUE;
    } while (renderer_process(app, buffer));
    return FALSE;
}

static gboolean
start_renderer(App *app)
{
    app->render_thread = g_thread_create(renderer_thread, app, TRUE, NULL);
    if (!app->render_thread)
        return FALSE;
    return TRUE;
}

static gboolean
stop_renderer(App *app)
{
    app->render_thread_cancel = TRUE;
    g_thread_join(app->render_thread);

    g_print("Render thread stopped\n");

    flush_decoder_queue(app);
    gst_buffer_replace(&app->last_buffer, NULL);
    return TRUE;
}

static void
app_free(App *app)
{
    if (!app)
        return;

    if (app->file) {
        g_mapped_file_unref(app->file);
        app->file = NULL;
    }
    g_free(app->file_name);

    g_clear_object(&app->decoder);
    g_clear_object(&app->window);
    g_clear_object(&app->display);

    if (app->decoder_queue) {
        g_async_queue_unref(app->decoder_queue);
        app->decoder_queue = NULL;
    }
    g_cond_clear(&app->decoder_ready);

    g_cond_clear(&app->event_cond);
    g_mutex_clear(&app->mutex);
    g_slice_free(App, app);
}

static App *
app_new(void)
{
    App *app;

    app = g_slice_new0(App);
    if (!app)
        return NULL;

    g_mutex_init(&app->mutex);
    g_cond_init(&app->event_cond);
    g_cond_init(&app->decoder_ready);

    app->decoder_queue = g_async_queue_new_full(
        (GDestroyNotify)gst_buffer_unref);
    if (!app->decoder_queue)
        goto error;
    return app;

error:
    app_free(app);
    return NULL;
}

static gboolean
app_check_events(App *app)
{
    GError *error = NULL;
    gboolean stop = FALSE;

    do {
        g_mutex_lock(&app->mutex);
        while (app->event == APP_RUNNING)
            g_cond_wait(&app->event_cond, &app->mutex);

        switch (app->event) {
        case APP_GOT_ERROR:
            error = app->error;
            app->error = NULL;
            /* fall-through */
        case APP_GOT_EOS:
            stop = TRUE;
            break;
        default:
            break;
        }
        g_mutex_unlock(&app->mutex);
    } while (!stop);

    if (!error)
        return TRUE;

    g_message("%s error: %s", get_error_string(error->code), error->message);
    g_error_free(error);
    return FALSE;
}

static gboolean
app_run(App *app, int argc, char *argv[])
{
    if (!video_output_init(&argc, argv, g_options)) {
        g_message("failed to initialize video output subsystem");
        return FALSE;
    }

    if (argc < 2) {
        g_message("no bitstream file specified");
        return FALSE;
    }
    app->file_name = g_strdup(argv[1]);

    if (!g_file_test(app->file_name, G_FILE_TEST_IS_REGULAR)) {
        g_message("failed to find file '%s'", app->file_name);
        return FALSE;
    }

    app->codec = identify_codec(app->file_name);
    if (!app->codec) {
        app->codec = identify_codec_from_string(g_codec_str);
        if (!app->codec) {
            g_message("failed to identify codec for '%s'", app->file_name);
            return FALSE;
        }
    }

    g_print("Simple decoder (%s bitstream)\n", string_from_codec(app->codec));

    app->display = video_output_create_display(NULL);
    if (!app->display) {
        g_message("failed to create VA display");
        return FALSE;
    }

    app->window = video_output_create_window(app->display, 640, 480);
    if (!app->window) {
        g_message("failed to create window");
        return FALSE;
    }

    gst_vaapi_window_show(app->window);

    if (!start_decoder(app)) {
        g_message("failed to start decoder thread");
        return FALSE;
    }

    if (!start_renderer(app)) {
        g_message("failed to start renderer thread");
        return FALSE;
    }

    app_check_events(app);

    stop_renderer(app);
    stop_decoder(app);
    video_output_exit();
    return TRUE;
}

int
main(int argc, char *argv[])
{
    App *app;
    gint ret;

    app = app_new();
    if (!app)
        g_error("failed to create application context");

    ret = !app_run(app, argc, argv);

    app_free(app);
    g_free(g_codec_str);
    return ret;
}
