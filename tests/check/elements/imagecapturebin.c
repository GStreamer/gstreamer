/* GStreamer unit test for the imagecapturebin element
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <gst/check/gstcheck.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#define N_BUFFERS 3

typedef struct
{
  GstElement *pipe;
  GstElement *src;
  GstElement *icbin;
} GstImageCaptureBinTestContext;

static void
gstimagecapturebin_init_test_context (GstImageCaptureBinTestContext * ctx,
    const gchar * src, gint num_buffers)
{
  fail_unless (ctx != NULL);

  ctx->pipe = gst_pipeline_new ("pipeline");
  fail_unless (ctx->pipe != NULL);
  ctx->src = gst_element_factory_make (src, "src");
  fail_unless (ctx->src != NULL, "Failed to create src element");
  ctx->icbin = gst_element_factory_make ("imagecapturebin", "icbin");
  fail_unless (ctx->icbin != NULL, "Failed to create imagecapturebin element");

  if (num_buffers > 0)
    g_object_set (ctx->src, "num-buffers", num_buffers, NULL);

  fail_unless (gst_bin_add (GST_BIN (ctx->pipe), ctx->src));
  fail_unless (gst_bin_add (GST_BIN (ctx->pipe), ctx->icbin));
  fail_unless (gst_element_link_many (ctx->src, ctx->icbin, NULL));
}

static void
gstimagecapturebin_unset_test_context (GstImageCaptureBinTestContext * ctx)
{
  gst_element_set_state (ctx->pipe, GST_STATE_NULL);
  gst_object_unref (ctx->pipe);
  memset (ctx, 0, sizeof (GstImageCaptureBinTestContext));
}

static gchar *
make_test_file_name (void)
{
  return g_strdup_printf ("%s" G_DIR_SEPARATOR_S
      "imagecapturebintest_%%d.cap", g_get_tmp_dir ());
}

static gboolean
get_file_info (const gchar * filename, gint * width, gint * height)
{
  GstElement *playbin = gst_element_factory_make ("playbin2", NULL);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstState state = GST_STATE_NULL;
  GstPad *pad;
  GstCaps *caps;
  gchar *uri = g_strdup_printf ("file://%s", filename);

  g_object_set (playbin, "video-sink", fakesink, NULL);
  g_object_set (playbin, "uri", uri, NULL);
  g_free (uri);

  gst_element_set_state (playbin, GST_STATE_PAUSED);

  gst_element_get_state (playbin, &state, NULL, GST_SECOND * 5);

  fail_unless (state == GST_STATE_PAUSED);

  g_signal_emit_by_name (playbin, "get-video-pad", 0, &pad, NULL);
  caps = gst_pad_get_negotiated_caps (pad);
  fail_unless (gst_structure_get_int (gst_caps_get_structure (caps, 0), "width",
          width));
  fail_unless (gst_structure_get_int (gst_caps_get_structure (caps, 0),
          "height", height));

  gst_object_unref (pad);
  gst_element_set_state (playbin, GST_STATE_NULL);
  gst_object_unref (playbin);
  return TRUE;
}

static GstBuffer *
create_video_buffer (GstCaps * caps)
{
  GstElement *pipeline;
  GstElement *cf;
  GstElement *sink;
  GstBuffer *buffer;

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=1 ! capsfilter name=cf ! appsink name=sink",
      NULL);
  g_assert (pipeline != NULL);

  cf = gst_bin_get_by_name (GST_BIN (pipeline), "cf");
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  g_object_set (G_OBJECT (cf), "caps", caps, NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  buffer = gst_app_sink_pull_buffer (GST_APP_SINK (sink));

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (sink);
  gst_object_unref (cf);
  return buffer;
}


GST_START_TEST (test_simple_capture)
{
  GstImageCaptureBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  gchar *test_file_name;
  gint i;

  gstimagecapturebin_init_test_context (&ctx, "videotestsrc", N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name ();
  g_object_set (ctx.icbin, "location", test_file_name, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* check there are N_BUFFERS files */
  for (i = 0; i < N_BUFFERS; i++) {
    gchar *filename;
    FILE *f;

    filename = g_strdup_printf (test_file_name, i);

    fail_unless (g_file_test (filename, G_FILE_TEST_EXISTS));
    fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
    fail_if (g_file_test (filename, G_FILE_TEST_IS_SYMLINK));

    /* check the file isn't empty */
    f = fopen (filename, "r");
    fseek (f, 0, SEEK_END);
    fail_unless (ftell (f) > 0);
    fclose (f);

    g_free (filename);
  }

  gstimagecapturebin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;


GST_START_TEST (test_multiple_captures_different_caps)
{
  GstImageCaptureBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  gchar *test_file_name;
  gint i;
  gint widths[] = { 100, 300, 200 };
  gint heights[] = { 300, 200, 100 };
  GstPad *pad;

  gstimagecapturebin_init_test_context (&ctx, "appsrc", N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name ();
  g_object_set (ctx.icbin, "location", test_file_name, NULL);
  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  /* push data directly because set_caps and buffer pushes on appsrc
   * are not serialized into the flow, so we can't guarantee the buffers
   * have the caps we want on them when pushed */
  pad = gst_element_get_static_pad (ctx.src, "src");

  /* push the buffers */
  for (i = 0; i < N_BUFFERS; i++) {
    GstCaps *caps;
    GstBuffer *buf;

    caps = gst_caps_new_simple ("video/x-raw-yuv", "width", G_TYPE_INT,
        widths[i], "height", G_TYPE_INT, heights[i], "framerate",
        GST_TYPE_FRACTION, 1, 1, "format", GST_TYPE_FOURCC,
        GST_MAKE_FOURCC ('I', '4', '2', '0'), NULL);

    buf = create_video_buffer (caps);
    fail_if (buf == NULL);

    fail_unless (gst_pad_push (pad, buf) == GST_FLOW_OK);
    gst_caps_unref (caps);
  }
  gst_app_src_end_of_stream (GST_APP_SRC (ctx.src));
  gst_object_unref (pad);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  /* check there are N_BUFFERS files */
  for (i = 0; i < N_BUFFERS; i++) {
    gchar *filename;
    FILE *f;
    gint width = 0, height = 0;

    filename = g_strdup_printf (test_file_name, i);

    fail_unless (g_file_test (filename, G_FILE_TEST_EXISTS));
    fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
    fail_if (g_file_test (filename, G_FILE_TEST_IS_SYMLINK));

    /* check the file isn't empty */
    f = fopen (filename, "r");
    fseek (f, 0, SEEK_END);
    fail_unless (ftell (f) > 0);
    fclose (f);

    /* get the file info */
    fail_unless (get_file_info (filename, &width, &height));
    fail_unless (width == widths[i]);
    fail_unless (height == heights[i]);

    g_free (filename);
  }

  gstimagecapturebin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

GST_START_TEST (test_setting_encoder)
{
  GstImageCaptureBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  GstElement *encoder;
  gchar *test_file_name;
  gint i;

  gstimagecapturebin_init_test_context (&ctx, "videotestsrc", N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name ();
  g_object_set (ctx.icbin, "location", test_file_name, NULL);

  encoder = gst_element_factory_make ("jpegenc", NULL);
  g_object_set (ctx.icbin, "image-encoder", encoder, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* check there are N_BUFFERS files */
  for (i = 0; i < N_BUFFERS; i++) {
    gchar *filename;
    FILE *f;

    filename = g_strdup_printf (test_file_name, i);

    fail_unless (g_file_test (filename, G_FILE_TEST_EXISTS));
    fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
    fail_if (g_file_test (filename, G_FILE_TEST_IS_SYMLINK));

    /* check the file isn't empty */
    f = fopen (filename, "r");
    fseek (f, 0, SEEK_END);
    fail_unless (ftell (f) > 0);
    fclose (f);

    g_free (filename);
  }

  gstimagecapturebin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

GST_START_TEST (test_setting_muxer)
{
  GstImageCaptureBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  GstElement *encoder;
  gchar *test_file_name;
  gint i;

  gstimagecapturebin_init_test_context (&ctx, "videotestsrc", N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name ();
  g_object_set (ctx.icbin, "location", test_file_name, NULL);

  encoder = gst_element_factory_make ("pngenc", NULL);
  g_object_set (ctx.icbin, "image-encoder", encoder, NULL);

  encoder = gst_element_factory_make ("identity", NULL);
  g_object_set (ctx.icbin, "image-muxer", encoder, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* check there are N_BUFFERS files */
  for (i = 0; i < N_BUFFERS; i++) {
    gchar *filename;
    FILE *f;

    filename = g_strdup_printf (test_file_name, i);

    fail_unless (g_file_test (filename, G_FILE_TEST_EXISTS));
    fail_unless (g_file_test (filename, G_FILE_TEST_IS_REGULAR));
    fail_if (g_file_test (filename, G_FILE_TEST_IS_SYMLINK));

    /* check the file isn't empty */
    f = fopen (filename, "r");
    fseek (f, 0, SEEK_END);
    fail_unless (ftell (f) > 0);
    fclose (f);

    g_free (filename);
  }

  gstimagecapturebin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

static Suite *
imagecapturebin_suite (void)
{
  Suite *s = suite_create ("imagecapturebin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_capture);
  tcase_add_test (tc_chain, test_multiple_captures_different_caps);
  tcase_add_test (tc_chain, test_setting_encoder);
  tcase_add_test (tc_chain, test_setting_muxer);

  return s;
}

GST_CHECK_MAIN (imagecapturebin);
