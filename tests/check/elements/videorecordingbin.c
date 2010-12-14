/* GStreamer unit test for the videorecordingbin element
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

#define N_BUFFERS 100

typedef struct
{
  GstElement *pipe;
  GstElement *src;
  GstElement *vrbin;
} GstVideoRecordingBinTestContext;

static void
gstvideorecordingbin_init_test_context (GstVideoRecordingBinTestContext * ctx,
    gint num_buffers)
{
  fail_unless (ctx != NULL);

  ctx->pipe = gst_pipeline_new ("pipeline");
  fail_unless (ctx->pipe != NULL);
  ctx->src = gst_element_factory_make ("videotestsrc", "src");
  fail_unless (ctx->src != NULL, "Failed to create videotestsrc element");
  ctx->vrbin = gst_element_factory_make ("videorecordingbin", "icbin");
  fail_unless (ctx->vrbin != NULL,
      "Failed to create videorecordingbin element");

  if (num_buffers > 0)
    g_object_set (ctx->src, "num-buffers", num_buffers, NULL);

  fail_unless (gst_bin_add (GST_BIN (ctx->pipe), ctx->src));
  fail_unless (gst_bin_add (GST_BIN (ctx->pipe), ctx->vrbin));
  fail_unless (gst_element_link (ctx->src, ctx->vrbin));
}

static void
gstvideorecordingbin_unset_test_context (GstVideoRecordingBinTestContext * ctx)
{
  gst_element_set_state (ctx->pipe, GST_STATE_NULL);
  gst_object_unref (ctx->pipe);
  memset (ctx, 0, sizeof (GstVideoRecordingBinTestContext));
}

static gchar *
make_test_file_name (gint num)
{
  return g_strdup_printf ("%s" G_DIR_SEPARATOR_S
      "videorecordingbintest_%d.cap", g_get_tmp_dir (), num);
}

GST_START_TEST (test_simple_recording)
{
  GstVideoRecordingBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  gchar *test_file_name;
  FILE *f;

  gstvideorecordingbin_init_test_context (&ctx, N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name (0);
  g_object_set (ctx.vrbin, "location", test_file_name, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* check there is a recorded file */
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_EXISTS));
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_IS_REGULAR));
  fail_if (g_file_test (test_file_name, G_FILE_TEST_IS_SYMLINK));

  /* check the file isn't empty */
  f = fopen (test_file_name, "r");
  fseek (f, 0, SEEK_END);
  fail_unless (ftell (f) > 0);
  fclose (f);

  gstvideorecordingbin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

GST_START_TEST (test_setting_video_encoder)
{
  GstVideoRecordingBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  GstElement *encoder;
  gchar *test_file_name;
  FILE *f;

  gstvideorecordingbin_init_test_context (&ctx, N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name (0);
  g_object_set (ctx.vrbin, "location", test_file_name, NULL);

  encoder = gst_element_factory_make ("theoraenc", NULL);
  g_object_set (ctx.vrbin, "video-encoder", encoder, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* check there is a recorded file */
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_EXISTS));
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_IS_REGULAR));
  fail_if (g_file_test (test_file_name, G_FILE_TEST_IS_SYMLINK));

  /* check the file isn't empty */
  f = fopen (test_file_name, "r");
  fseek (f, 0, SEEK_END);
  fail_unless (ftell (f) > 0);
  fclose (f);

  gstvideorecordingbin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

GST_START_TEST (test_setting_video_muxer)
{
  GstVideoRecordingBinTestContext ctx;
  GstBus *bus;
  GstMessage *msg;
  GstElement *encoder;
  GstElement *muxer;
  gchar *test_file_name;
  FILE *f;

  gstvideorecordingbin_init_test_context (&ctx, N_BUFFERS);
  bus = gst_element_get_bus (ctx.pipe);

  test_file_name = make_test_file_name (0);
  g_object_set (ctx.vrbin, "location", test_file_name, NULL);

  encoder = gst_element_factory_make ("theoraenc", NULL);
  g_object_set (ctx.vrbin, "video-encoder", encoder, NULL);

  muxer = gst_element_factory_make ("oggmux", NULL);
  g_object_set (ctx.vrbin, "video-muxer", muxer, NULL);

  fail_if (gst_element_set_state (ctx.pipe, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 10,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);

  /* check there is a recorded file */
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_EXISTS));
  fail_unless (g_file_test (test_file_name, G_FILE_TEST_IS_REGULAR));
  fail_if (g_file_test (test_file_name, G_FILE_TEST_IS_SYMLINK));

  /* check the file isn't empty */
  f = fopen (test_file_name, "r");
  fseek (f, 0, SEEK_END);
  fail_unless (ftell (f) > 0);
  fclose (f);

  gstvideorecordingbin_unset_test_context (&ctx);
  gst_object_unref (bus);
  g_free (test_file_name);
}

GST_END_TEST;

static Suite *
videorecordingbin_suite (void)
{
  Suite *s = suite_create ("videorecordingbin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_recording);
  tcase_add_test (tc_chain, test_setting_video_encoder);
  tcase_add_test (tc_chain, test_setting_video_muxer);

  return s;
}

GST_CHECK_MAIN (videorecordingbin);
