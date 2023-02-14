/*
 * simple-encoder.c - Test GstVaapiencoder
 *
 * Copyright (C) 2015 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapiencoder_mpeg2.h>
#include <gst/vaapi/gstvaapiencoder_h264.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>

#include "output.h"
#include "y4mreader.h"

static guint g_bitrate = 0;
static gchar *g_codec_str;
static gchar *g_output_file_name;
static char **g_input_files = NULL;

#define SURFACE_NUM 16

static GOptionEntry g_options[] = {
  {"codec", 'c', 0, G_OPTION_ARG_STRING, &g_codec_str,
      "codec to use for video encoding (h264/mpeg2)", NULL},
  {"bitrate", 'b', 0, G_OPTION_ARG_INT, &g_bitrate,
      "desired bitrate expressed in kbps", NULL},
  {"output", 'o', 0, G_OPTION_ARG_FILENAME, &g_output_file_name,
      "output file name", NULL},
  {G_OPTION_REMAINING, ' ', 0, G_OPTION_ARG_FILENAME_ARRAY, &g_input_files,
      "input file name", NULL},
  {NULL}
};

typedef struct
{
  GstVaapiDisplay *display;
  GstVaapiEncoder *encoder;
  guint read_frames;
  guint encoded_frames;
  guint saved_frames;
  Y4MReader *parser;
  FILE *output_file;
  guint input_stopped:1;
  guint encode_failed:1;
} App;

static inline gchar *
generate_output_filename (const gchar * ext)
{
  gchar *fn;
  int i = 0;

  while (1) {
    fn = g_strdup_printf ("temp%02d.%s", i, ext);
    if (g_file_test (fn, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
      i++;
      g_free (fn);
    } else {
      break;
    }
  }

  return fn;
}

static gboolean
parse_options (int *argc, char *argv[])
{
  GOptionContext *ctx;
  gboolean success;
  GError *error = NULL;

  ctx = g_option_context_new (" - encoder test options");
  if (!ctx)
    return FALSE;

  g_option_context_add_group (ctx, gst_init_get_option_group ());
  g_option_context_add_main_entries (ctx, g_options, NULL);
  g_option_context_set_help_enabled (ctx, TRUE);
  success = g_option_context_parse (ctx, argc, &argv, &error);
  if (!success) {
    g_printerr ("Option parsing failed: %s\n", error->message);
    g_error_free (error);
    goto bail;
  }

  if (!g_codec_str)
    g_codec_str = g_strdup ("h264");
  if (!g_output_file_name)
    g_output_file_name = generate_output_filename (g_codec_str);

bail:
  g_option_context_free (ctx);
  return success;
}

static void
print_yuv_info (App * app)
{
  g_print ("\n");
  g_print ("Encode      : %s\n", g_codec_str);
  g_print ("Resolution  : %dx%d\n", app->parser->width, app->parser->height);
  g_print ("Source YUV  : %s\n", g_input_files ? g_input_files[0] : "stdin");
  g_print ("Frame Rate  : %0.1f fps\n",
      1.0 * app->parser->fps_n / app->parser->fps_d);
  g_print ("Coded file  : %s\n", g_output_file_name);
  g_print ("\n");
}

static void
print_num_frame (App * app)
{
  g_print ("\n");
  g_print ("read frames    : %d\n", app->read_frames);
  g_print ("encoded frames : %d\n", app->encoded_frames);
  g_print ("saved frames   : %d\n", app->saved_frames);
  g_print ("\n");
}

static GstVaapiEncoder *
encoder_new (GstVaapiDisplay * display)
{
  GstVaapiEncoder *encoder = NULL;

  if (!g_strcmp0 (g_codec_str, "mpeg2"))
    encoder = gst_vaapi_encoder_mpeg2_new (display);
  else if (!g_strcmp0 (g_codec_str, "h264"))
    encoder = gst_vaapi_encoder_h264_new (display);
  else
    return NULL;

  gst_vaapi_encoder_set_bitrate (encoder, g_bitrate);

  return encoder;
}

static inline GstVideoCodecState *
new_codec_state (gint width, gint height, gint fps_n, gint fps_d)
{
  GstVideoCodecState *state;

  state = g_new0 (GstVideoCodecState, 1);
  state->ref_count = 1;
  gst_video_info_set_format (&state->info, GST_VIDEO_FORMAT_ENCODED, width,
      height);

  state->info.fps_n = fps_n;
  state->info.fps_d = fps_d;

  return state;
}

static gboolean
set_format (GstVaapiEncoder * encoder, gint width, gint height, gint fps_n,
    gint fps_d)
{
  GstVideoCodecState *in_state;
  GstVaapiEncoderStatus status;

  in_state = new_codec_state (width, height, fps_n, fps_d);
  status = gst_vaapi_encoder_set_codec_state (encoder, in_state);
  g_free (in_state);

  return (status == GST_VAAPI_ENCODER_STATUS_SUCCESS);
}

static GstBuffer *
allocate_buffer (GstVaapiCodedBuffer * vbuf)
{
  GstBuffer *buf;
  gssize size;

  size = gst_vaapi_coded_buffer_get_size (vbuf);
  if (size <= 0) {
    g_warning ("Invalid VA buffer size (%zd)", size);
    return NULL;
  }

  buf = gst_buffer_new_and_alloc (size);
  if (!buf) {
    g_warning ("Failed to create output buffer of size %zd", size);
    return NULL;
  }

  if (!gst_vaapi_coded_buffer_copy_into (buf, vbuf)) {
    g_warning ("Failed to copy VA buffer data");
    gst_buffer_unref (buf);
    return NULL;
  }

  return buf;
}

static GstVaapiEncoderStatus
get_encoder_buffer (GstVaapiEncoder * encoder, GstBuffer ** buffer)
{
  GstVaapiCodedBufferProxy *proxy = NULL;
  GstVaapiEncoderStatus status;

  status = gst_vaapi_encoder_get_buffer_with_timeout (encoder, &proxy, 50000);
  if (status < GST_VAAPI_ENCODER_STATUS_SUCCESS) {
    g_warning ("Failed to get a buffer from encoder: %d", status);
    return status;
  } else if (status > GST_VAAPI_ENCODER_STATUS_SUCCESS) {
    return status;
  }

  *buffer = allocate_buffer (GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (proxy));
  gst_vaapi_coded_buffer_proxy_unref (proxy);

  return status;
}

static gboolean
outputs_to_file (GstBuffer * buffer, FILE * file)
{
  GstMapInfo info;
  size_t written;
  gboolean ret = FALSE;

  if (!gst_buffer_map (buffer, &info, GST_MAP_READ))
    return FALSE;

  if (info.size <= 0 || !info.data)
    return FALSE;

  written = fwrite (info.data, 1, info.size, file);
  if (written < info.size) {
    g_warning ("write file error.");
    goto bail;
  }

  ret = TRUE;

bail:
  gst_buffer_unmap (buffer, &info);
  return ret;
}

static gpointer
get_buffer_thread (gpointer data)
{
  App *app = data;

  GstVaapiEncoderStatus ret;
  GstBuffer *obuf;

  while (1) {
    obuf = NULL;
    ret = get_encoder_buffer (app->encoder, &obuf);
    if (app->input_stopped && ret > GST_VAAPI_ENCODER_STATUS_SUCCESS) {
      break;                    /* finished */
    } else if (ret > GST_VAAPI_ENCODER_STATUS_SUCCESS) {        /* another chance */
      continue;
    }
    if (ret < GST_VAAPI_ENCODER_STATUS_SUCCESS) {       /* fatal error */
      app->encode_failed = TRUE;
      break;
    }

    app->encoded_frames++;
    g_debug ("encoded frame %d, buffer = %p", app->encoded_frames, obuf);

    if (app->output_file && outputs_to_file (obuf, app->output_file))
      app->saved_frames++;

    gst_buffer_unref (obuf);
  }

  if (obuf)
    gst_buffer_replace (&obuf, NULL);

  return NULL;
}

static void
app_free (App * app)
{
  g_return_if_fail (app);

  if (app->parser)
    y4m_reader_close (app->parser);

  if (app->encoder) {
    if ((gst_vaapi_encoder_flush (app->encoder)) !=
        GST_VAAPI_ENCODER_STATUS_SUCCESS)
      GST_WARNING_OBJECT (app->encoder,
          "Failed to flush pending encoded frames");

    gst_object_unref (app->encoder);
  }

  if (app->display)
    gst_object_unref (app->display);

  if (app->output_file)
    fclose (app->output_file);

  g_free (app);
}

static App *
app_new (const gchar * input_fn, const gchar * output_fn)
{
  App *app = g_new0 (App, 1);
  if (!app)
    return NULL;

  app->parser = y4m_reader_open (input_fn);
  if (!app->parser) {
    g_warning ("Could not parse input stream.");
    goto error;
  }

  app->output_file = fopen (output_fn, "w");
  if (app->output_file == NULL) {
    g_warning ("Could not open file \"%s\" for writing: %s.", output_fn,
        g_strerror (errno));
    goto error;
  }

  app->display = video_output_create_display (NULL);
  if (!app->display) {
    g_warning ("Could not create VA display.");
    goto error;
  }

  app->encoder = encoder_new (app->display);
  if (!app->encoder) {
    g_warning ("Could not create encoder.");
    goto error;
  }

  if (!set_format (app->encoder, app->parser->width, app->parser->height,
          app->parser->fps_n, app->parser->fps_d)) {
    g_warning ("Could not set format.");
    goto error;
  }

  return app;

error:
  app_free (app);
  return NULL;
}

static gboolean
upload_frame (GstVaapiEncoder * encoder, GstVaapiSurfaceProxy * proxy)
{
  GstVideoCodecFrame *frame;
  GstVaapiEncoderStatus ret;

  frame = g_new0 (GstVideoCodecFrame, 1);
  gst_video_codec_frame_set_user_data (frame,
      gst_vaapi_surface_proxy_ref (proxy),
      (GDestroyNotify) gst_vaapi_surface_proxy_unref);

  ret = gst_vaapi_encoder_put_frame (encoder, frame);
  return (ret == GST_VAAPI_ENCODER_STATUS_SUCCESS);
}

static gboolean
load_frame (App * app, GstVaapiImage * image)
{
  gboolean ret = FALSE;

  if (!gst_vaapi_image_map (image))
    return FALSE;

  ret = y4m_reader_load_image (app->parser, image);

  if (!gst_vaapi_image_unmap (image))
    return FALSE;

  return ret;
}

static int
app_run (App * app)
{
  GstVaapiImage *image;
  GstVaapiVideoPool *pool;
  GThread *buffer_thread;
  gsize id;
  int ret = EXIT_FAILURE;

  image = gst_vaapi_image_new (app->display, GST_VIDEO_FORMAT_I420,
      app->parser->width, app->parser->height);

  {
    GstVideoInfo vi;
    gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_ENCODED,
        app->parser->width, app->parser->height);
    pool = gst_vaapi_surface_pool_new_full (app->display, &vi, 0);
  }

  buffer_thread = g_thread_new ("get buffer thread", get_buffer_thread, app);

  while (1) {
    GstVaapiSurfaceProxy *proxy;
    GstVaapiSurface *surface;

    if (!load_frame (app, image))
      break;

    if (!gst_vaapi_image_unmap (image))
      break;

    proxy =
        gst_vaapi_surface_proxy_new_from_pool (GST_VAAPI_SURFACE_POOL (pool));
    if (!proxy) {
      g_warning ("Could not get surface proxy from pool.");
      break;
    }
    surface = gst_vaapi_surface_proxy_get_surface (proxy);
    if (!surface) {
      g_warning ("Could not get surface from proxy.");
      break;
    }

    if (!gst_vaapi_surface_put_image (surface, image)) {
      g_warning ("Could not update surface");
      break;
    }

    if (!upload_frame (app->encoder, proxy)) {
      g_warning ("put frame failed");
      break;
    }

    app->read_frames++;
    id = gst_vaapi_surface_get_id (surface);
    g_debug ("input frame %d, surface id = %" G_GSIZE_FORMAT, app->read_frames,
        id);

    gst_vaapi_surface_proxy_unref (proxy);
  }

  app->input_stopped = TRUE;

  g_thread_join (buffer_thread);

  if (!app->encode_failed && feof (app->parser->fp))
    ret = EXIT_SUCCESS;

  gst_vaapi_video_pool_replace (&pool, NULL);
  gst_vaapi_image_unref (image);
  return ret;
}

int
main (int argc, char *argv[])
{
  App *app;
  int ret = EXIT_FAILURE;
  gchar *input_fn;

  if (!parse_options (&argc, argv))
    return EXIT_FAILURE;

  /* @TODO: iterate all the input files */
  input_fn = g_input_files ? g_input_files[0] : NULL;
  if (input_fn && !g_file_test (input_fn,
          G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
    g_warning ("input file \"%s\" doesn't exist", input_fn);
    goto bail;
  }

  app = app_new (input_fn, g_output_file_name);
  if (!app)
    goto bail;

  print_yuv_info (app);
  ret = app_run (app);
  print_num_frame (app);

  app_free (app);

bail:
  g_free (g_codec_str);
  g_free (g_output_file_name);
  g_strfreev (g_input_files);

  gst_deinit ();

  return ret;
}
