/*
 *  simple-decoder.c - Simple Decoder Application
 *
 *  Copyright (C) 2013-2014 Intel Corporation
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

/*
 * This is a really simple decoder application that only accepts raw
 * bitstreams. So, it may be needed to suggest what codec to use to
 * the application.
 */

#include "gst/vaapi/sysdeps.h"
#include <stdarg.h>
#include <gst/vaapi/gstvaapidecoder.h>
#include <gst/vaapi/gstvaapidecoder_h264.h>
#include <gst/vaapi/gstvaapidecoder_jpeg.h>
#include <gst/vaapi/gstvaapidecoder_mpeg2.h>
#include <gst/vaapi/gstvaapidecoder_mpeg4.h>
#include <gst/vaapi/gstvaapidecoder_vc1.h>
#include <gst/vaapi/gstvaapiwindow.h>
#include "codec.h"
#include "output.h"

static gchar *g_codec_str;
static gboolean g_benchmark;

static GOptionEntry g_options[] = {
  {"codec", 'c',
        0,
        G_OPTION_ARG_STRING, &g_codec_str,
      "suggested codec", NULL},
  {"benchmark", 0,
        0,
        G_OPTION_ARG_NONE, &g_benchmark,
      "benchmark mode", NULL},
  {NULL,}
};

typedef enum
{
  APP_RUNNING,
  APP_GOT_EOS,
  APP_GOT_ERROR,
} AppEvent;

typedef enum
{
  APP_ERROR_NONE,
  APP_ERROR_DECODER,
  APP_ERROR_RENDERER,
} AppError;

typedef struct
{
  GstVaapiSurfaceProxy *proxy;
  GstClockTime pts;
  GstClockTime duration;
} RenderFrame;

typedef struct
{
  GMutex mutex;
  GMappedFile *file;
  gchar *file_name;
  guint file_offset;
  guint file_size;
  guchar *file_data;
  GstVaapiDisplay *display;
  GstVaapiDecoder *decoder;
  GThread *decoder_thread;
  gboolean decoder_thread_cancel;
  GAsyncQueue *decoder_queue;
  GstVaapiCodec codec;
  guint fps_n;
  guint fps_d;
  guint32 frame_duration;
  guint surface_width;
  guint surface_height;
  GstVaapiWindow *window;
  guint window_width;
  guint window_height;
  GThread *render_thread;
  gboolean render_thread_cancel;
  GCond render_ready;
  RenderFrame *last_frame;
  GError *error;
  AppEvent event;
  GCond event_cond;
  GTimer *timer;
  guint32 num_frames;
} App;

static inline RenderFrame *
render_frame_new (void)
{
  return g_new (RenderFrame, 1);
}

static void
render_frame_free (RenderFrame * rfp)
{
  if (G_UNLIKELY (!rfp))
    return;
  gst_vaapi_surface_proxy_replace (&rfp->proxy, NULL);
  g_free (rfp);
}

static inline void
render_frame_replace (RenderFrame ** rfp_ptr, RenderFrame * new_rfp)
{
  if (*rfp_ptr)
    render_frame_free (*rfp_ptr);
  *rfp_ptr = new_rfp;
}

#define APP_ERROR app_error_quark()
static GQuark
app_error_quark (void)
{
  static gsize g_quark;

  if (g_once_init_enter (&g_quark)) {
    gsize quark = (gsize) g_quark_from_static_string ("AppError");
    g_once_init_leave (&g_quark, quark);
  }
  return g_quark;
}

static void
app_send_error (App * app, GError * error)
{
  g_mutex_lock (&app->mutex);
  app->error = error;
  app->event = APP_GOT_ERROR;
  g_cond_signal (&app->event_cond);
  g_mutex_unlock (&app->mutex);
}

static void
app_send_eos (App * app)
{
  g_mutex_lock (&app->mutex);
  app->event = APP_GOT_EOS;
  g_cond_signal (&app->event_cond);
  g_mutex_unlock (&app->mutex);
}

static const gchar *
get_decoder_status_string (GstVaapiDecoderStatus status)
{
  const gchar *str;

#define DEFINE_STATUS(status, status_string) \
    case GST_VAAPI_DECODER_STATUS_##status:  \
        str = status_string;                 \
        break

  switch (status) {
      DEFINE_STATUS (SUCCESS, "<success>");
      DEFINE_STATUS (END_OF_STREAM, "<EOS>");
      DEFINE_STATUS (ERROR_ALLOCATION_FAILED, "allocation failed");
      DEFINE_STATUS (ERROR_INIT_FAILED, "initialization failed");
      DEFINE_STATUS (ERROR_UNSUPPORTED_CODEC, "unsupported codec");
      DEFINE_STATUS (ERROR_NO_DATA, "not enough data");
      DEFINE_STATUS (ERROR_NO_SURFACE, "no surface vailable");
      DEFINE_STATUS (ERROR_INVALID_SURFACE, "invalid surface");
      DEFINE_STATUS (ERROR_BITSTREAM_PARSER, "bitstream parser error");
      DEFINE_STATUS (ERROR_UNSUPPORTED_PROFILE, "unsupported profile");
      DEFINE_STATUS (ERROR_UNSUPPORTED_CHROMA_FORMAT,
          "unsupported chroma-format");
      DEFINE_STATUS (ERROR_INVALID_PARAMETER, "invalid parameter");
    default:
      str = "<unknown>";
      break;
  }
#undef DEFINE_STATUS

  return str;
}

static const gchar *
get_error_string (AppError error)
{
  const gchar *str;

#define DEFINE_ERROR(error, error_string)       \
    case APP_ERROR_##error:                     \
        str = error_string;                     \
        break

  switch (error) {
      DEFINE_ERROR (NONE, "<none>");
      DEFINE_ERROR (DECODER, "decoder");
      DEFINE_ERROR (RENDERER, "renderer");
    default:
      str = "unknown";
      break;
  }
#undef DEFINE_ERROR

  return str;
}

static gpointer
decoder_thread (gpointer data)
{
  App *const app = data;
  GError *error = NULL;
  GstVaapiDecoderStatus status;
  GstVaapiSurfaceProxy *proxy;
  RenderFrame *rfp;
  GstBuffer *buffer;
  GstClockTime pts;
  gboolean got_eos = FALSE;
  guint ofs;

  g_print ("Decoder thread started\n");

#define SEND_ERROR(...)                                                 \
    do {                                                                \
        error = g_error_new(APP_ERROR, APP_ERROR_DECODER, __VA_ARGS__); \
        goto send_error;                                                \
    } while (0)

  pts = g_get_monotonic_time ();
  ofs = 0;
  while (!g_atomic_int_get (&app->decoder_thread_cancel)) {
    if (G_UNLIKELY (ofs == app->file_size))
      buffer = NULL;
    else {
      const gsize size = MIN (4096, app->file_size - ofs);
      buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
          app->file_data, app->file_size, ofs, size, NULL, NULL);
      if (!buffer)
        SEND_ERROR ("failed to allocate new buffer");
      ofs += size;
    }
    if (!gst_vaapi_decoder_put_buffer (app->decoder, buffer))
      SEND_ERROR ("failed to push buffer to decoder");
    gst_buffer_replace (&buffer, NULL);

    status = gst_vaapi_decoder_get_surface (app->decoder, &proxy);
    switch (status) {
      case GST_VAAPI_DECODER_STATUS_SUCCESS:
        rfp = render_frame_new ();
        if (!rfp)
          SEND_ERROR ("failed to allocate render frame");
        rfp->proxy = proxy;
        rfp->pts = pts;
        rfp->duration = app->frame_duration;
        pts += app->frame_duration;
        g_async_queue_push (app->decoder_queue, rfp);
        break;
      case GST_VAAPI_DECODER_STATUS_ERROR_NO_DATA:
        /* nothing to do, just continue to the next iteration */
        break;
      case GST_VAAPI_DECODER_STATUS_END_OF_STREAM:
        gst_vaapi_decoder_flush (app->decoder);
        if (got_eos)
          goto send_eos;
        got_eos = TRUE;
        break;
      default:
        SEND_ERROR ("%s", get_decoder_status_string (status));
        break;
    }
  }
  return NULL;

#undef SEND_ERROR

send_eos:
  app_send_eos (app);
  return NULL;

send_error:
  app_send_error (app, error);
  return NULL;
}

static void
app_set_framerate (App * app, guint fps_n, guint fps_d)
{
  if (!fps_n || !fps_d)
    return;

  g_mutex_lock (&app->mutex);
  if (fps_n != app->fps_n || fps_d != app->fps_d) {
    app->fps_n = fps_n;
    app->fps_d = fps_d;
    app->frame_duration =
        gst_util_uint64_scale (GST_TIME_AS_USECONDS (GST_SECOND), fps_d, fps_n);
  }
  g_mutex_unlock (&app->mutex);
}

static void
handle_decoder_state_changes (GstVaapiDecoder * decoder,
    const GstVideoCodecState * codec_state, gpointer user_data)
{
  App *const app = user_data;

  g_assert (app->decoder == decoder);
  app_set_framerate (app, codec_state->info.fps_n, codec_state->info.fps_d);
}

static gboolean
start_decoder (App * app)
{
  GstCaps *caps;

  app->file = g_mapped_file_new (app->file_name, FALSE, NULL);
  if (!app->file)
    return FALSE;

  app->file_size = g_mapped_file_get_length (app->file);
  app->file_data = (guint8 *) g_mapped_file_get_contents (app->file);
  if (!app->file_data)
    return FALSE;

  caps = caps_from_codec (app->codec);
  switch (app->codec) {
    case GST_VAAPI_CODEC_H264:
      app->decoder = gst_vaapi_decoder_h264_new (app->display, caps);
      break;
    case GST_VAAPI_CODEC_JPEG:
      app->decoder = gst_vaapi_decoder_jpeg_new (app->display, caps);
      break;
    case GST_VAAPI_CODEC_MPEG2:
      app->decoder = gst_vaapi_decoder_mpeg2_new (app->display, caps);
      break;
    case GST_VAAPI_CODEC_MPEG4:
      app->decoder = gst_vaapi_decoder_mpeg4_new (app->display, caps);
      break;
    case GST_VAAPI_CODEC_VC1:
      app->decoder = gst_vaapi_decoder_vc1_new (app->display, caps);
      break;
    default:
      app->decoder = NULL;
      break;
  }
  if (!app->decoder)
    return FALSE;

  gst_vaapi_decoder_set_codec_state_changed_func (app->decoder,
      handle_decoder_state_changes, app);

  g_timer_start (app->timer);

  app->decoder_thread = g_thread_try_new ("Decoder Thread", decoder_thread,
      app, NULL);
  if (!app->decoder_thread)
    return FALSE;
  return TRUE;
}

static gboolean
stop_decoder (App * app)
{
  g_timer_stop (app->timer);

  g_atomic_int_set (&app->decoder_thread_cancel, TRUE);
  g_thread_join (app->decoder_thread);
  g_print ("Decoder thread stopped\n");
  return TRUE;
}

static void
ensure_window_size (App * app, GstVaapiSurface * surface)
{
  guint width, height;

  if (gst_vaapi_window_get_fullscreen (app->window))
    return;

  gst_vaapi_surface_get_size (surface, &width, &height);
  if (app->surface_width == width && app->surface_height == height)
    return;
  app->surface_width = width;
  app->surface_height = height;

  gst_vaapi_window_set_size (app->window, width, height);
  gst_vaapi_window_get_size (app->window,
      &app->window_width, &app->window_height);
}

static inline void
renderer_wait_until (App * app, GstClockTime pts)
{
  g_mutex_lock (&app->mutex);
  do {
  } while (g_cond_wait_until (&app->render_ready, &app->mutex, pts));
  g_mutex_unlock (&app->mutex);
}

static gboolean
renderer_process (App * app, RenderFrame * rfp)
{
  GError *error = NULL;
  GstVaapiSurface *surface;
  const GstVaapiRectangle *crop_rect;

#define SEND_ERROR(...)                                                 \
    do {                                                                \
        error = g_error_new(APP_ERROR, APP_ERROR_RENDERER, __VA_ARGS__); \
        goto send_error;                                                \
    } while (0)

  surface = gst_vaapi_surface_proxy_get_surface (rfp->proxy);
  if (!surface)
    SEND_ERROR ("failed to get decoded surface from render frame");

  ensure_window_size (app, surface);

  crop_rect = gst_vaapi_surface_proxy_get_crop_rect (rfp->proxy);

  if (!gst_vaapi_surface_sync (surface))
    SEND_ERROR ("failed to sync decoded surface");

  if (G_LIKELY (!g_benchmark))
    renderer_wait_until (app, rfp->pts);

  if (!gst_vaapi_window_put_surface (app->window, surface,
          crop_rect, NULL, GST_VAAPI_PICTURE_STRUCTURE_FRAME))
    SEND_ERROR ("failed to render surface %" GST_VAAPI_ID_FORMAT,
        GST_VAAPI_ID_ARGS (gst_vaapi_surface_get_id (surface)));

  app->num_frames++;

  render_frame_replace (&app->last_frame, rfp);
  return TRUE;

#undef SEND_ERROR

send_error:
  app_send_error (app, error);
  return FALSE;
}

static gpointer
renderer_thread (gpointer data)
{
  App *const app = data;
  RenderFrame *rfp;

  g_print ("Render thread started\n");

  while (!g_atomic_int_get (&app->render_thread_cancel)) {
    rfp = g_async_queue_timeout_pop (app->decoder_queue, 1000000);
    if (rfp && !renderer_process (app, rfp))
      break;
  }
  return NULL;
}

static gboolean
flush_decoder_queue (App * app)
{
  RenderFrame *rfp;

  /* Flush pending surfaces */
  do {
    rfp = g_async_queue_try_pop (app->decoder_queue);
    if (!rfp)
      return TRUE;
  } while (renderer_process (app, rfp));
  return FALSE;
}

static gboolean
start_renderer (App * app)
{
  app->render_thread = g_thread_try_new ("Renderer Thread", renderer_thread,
      app, NULL);
  if (!app->render_thread)
    return FALSE;
  return TRUE;
}

static gboolean
stop_renderer (App * app)
{
  g_atomic_int_set (&app->render_thread_cancel, TRUE);
  g_thread_join (app->render_thread);

  g_print ("Render thread stopped\n");

  flush_decoder_queue (app);
  render_frame_replace (&app->last_frame, NULL);
  return TRUE;
}

static void
app_free (App * app)
{
  if (!app)
    return;

  if (app->file) {
    g_mapped_file_unref (app->file);
    app->file = NULL;
  }
  g_free (app->file_name);

  gst_vaapi_decoder_replace (&app->decoder, NULL);
  gst_vaapi_window_replace (&app->window, NULL);
  gst_vaapi_display_replace (&app->display, NULL);

  if (app->decoder_queue) {
    g_async_queue_unref (app->decoder_queue);
    app->decoder_queue = NULL;
  }

  if (app->timer) {
    g_timer_destroy (app->timer);
    app->timer = NULL;
  }

  g_cond_clear (&app->render_ready);
  g_cond_clear (&app->event_cond);
  g_mutex_clear (&app->mutex);
  g_free (app);
}

static App *
app_new (void)
{
  App *app;

  app = g_new0 (App, 1);
  if (!app)
    return NULL;

  g_mutex_init (&app->mutex);
  g_cond_init (&app->event_cond);
  g_cond_init (&app->render_ready);

  app_set_framerate (app, 60, 1);
  app->window_width = 640;
  app->window_height = 480;

  app->decoder_queue = g_async_queue_new_full (
      (GDestroyNotify) render_frame_free);
  if (!app->decoder_queue)
    goto error;

  app->timer = g_timer_new ();
  if (!app->timer)
    goto error;
  return app;

error:
  app_free (app);
  return NULL;
}

static gboolean
app_check_events (App * app)
{
  GError *error = NULL;
  gboolean stop = FALSE;

  do {
    g_mutex_lock (&app->mutex);
    while (app->event == APP_RUNNING)
      g_cond_wait (&app->event_cond, &app->mutex);

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
    g_mutex_unlock (&app->mutex);
  } while (!stop);

  if (!error)
    return TRUE;

  g_message ("%s error: %s", get_error_string (error->code), error->message);
  g_error_free (error);
  return FALSE;
}

static gboolean
app_run (App * app, int argc, char *argv[])
{
  if (argc < 2) {
    g_message ("no bitstream file specified");
    return FALSE;
  }
  app->file_name = g_strdup (argv[1]);

  if (!g_file_test (app->file_name, G_FILE_TEST_IS_REGULAR)) {
    g_message ("failed to find file '%s'", app->file_name);
    return FALSE;
  }

  app->codec = identify_codec (app->file_name);
  if (!app->codec) {
    app->codec = identify_codec_from_string (g_codec_str);
    if (!app->codec) {
      g_message ("failed to identify codec for '%s'", app->file_name);
      return FALSE;
    }
  }

  g_print ("Simple decoder (%s bitstream)\n", string_from_codec (app->codec));

  app->display = video_output_create_display (NULL);
  if (!app->display) {
    g_message ("failed to create VA display");
    return FALSE;
  }

  app->window = video_output_create_window (app->display,
      app->window_width, app->window_height);
  if (!app->window) {
    g_message ("failed to create window");
    return FALSE;
  }

  gst_vaapi_window_show (app->window);

  if (!start_decoder (app)) {
    g_message ("failed to start decoder thread");
    return FALSE;
  }

  if (!start_renderer (app)) {
    g_message ("failed to start renderer thread");
    return FALSE;
  }

  app_check_events (app);

  stop_renderer (app);
  stop_decoder (app);

  g_print ("Decoded %u frames", app->num_frames);
  if (g_benchmark) {
    const gdouble elapsed = g_timer_elapsed (app->timer, NULL);
    g_print (" in %.2f sec (%.1f fps)\n",
        elapsed, (gdouble) app->num_frames / elapsed);
  }
  g_print ("\n");
  return TRUE;
}

int
main (int argc, char *argv[])
{
  App *app;
  gint ret;

  if (!video_output_init (&argc, argv, g_options))
    g_error ("failed to initialize video output subsystem");

  app = app_new ();
  if (!app)
    g_error ("failed to create application context");

  ret = !app_run (app, argc, argv);

  app_free (app);
  g_free (g_codec_str);
  video_output_exit ();
  return ret;
}
