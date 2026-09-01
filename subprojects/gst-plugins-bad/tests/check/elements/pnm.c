/* GStreamer
 *
 * unit test for PNM encoder / decoder
 *
 * Copyright (C) <2016> Jan Schmidt <jan@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#include <gst/check/gstcheck.h>
#include <gst/app/gstappsink.h>

/* Create a pnmenc ! pnmdec and push in 
 * frames, checking that what comes out is what
 * went in */
GST_START_TEST (test_pnm_enc_dec)
{
  GstElement *pipeline;
  GstElement *incf, *outcf, *enc;
  GstElement *sink;
  GstSample *sample;
  GstBuffer *buffer;
  gint i, n;

  struct
  {
    const gchar *in_fmt;
    const gchar *out_fmt;
  } test_formats[] = {
    {
        "RGB", "RGB"}, {
        "GRAY8", "GRAY8"}, {
        "GRAY16_BE", "GRAY16_BE"}, {
        "GRAY16_BE", "GRAY16_LE"}, {
        "GRAY16_LE", "GRAY16_BE"}, {
        "GRAY16_LE", "GRAY16_LE"}
  };

  pipeline =
      gst_parse_launch
      ("videotestsrc num-buffers=1 ! capsfilter name=incf ! pnmenc name=enc ! pnmdec ! capsfilter name=outcf ! appsink name=sink",
      NULL);
  g_assert_nonnull (pipeline);

  incf = gst_bin_get_by_name (GST_BIN (pipeline), "incf");
  enc = gst_bin_get_by_name (GST_BIN (pipeline), "enc");
  outcf = gst_bin_get_by_name (GST_BIN (pipeline), "outcf");
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  for (n = 0; n < 2; n++) {
    for (i = 0; i < G_N_ELEMENTS (test_formats); i++) {
      GstCaps *incaps = gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
          320, "height", G_TYPE_INT, 240, "framerate",
          GST_TYPE_FRACTION, 1, 1, "format", G_TYPE_STRING,
          test_formats[i].in_fmt, NULL);
      GstCaps *outcaps =
          gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
          320, "height", G_TYPE_INT, 240, "framerate",
          GST_TYPE_FRACTION, 1, 1, "format", G_TYPE_STRING,
          test_formats[i].out_fmt, NULL);

      GST_DEBUG ("Setting in caps %" GST_PTR_FORMAT, incaps);
      g_object_set (G_OBJECT (incf), "caps", incaps, NULL);
      GST_DEBUG ("Setting out caps %" GST_PTR_FORMAT, outcaps);
      g_object_set (G_OBJECT (outcf), "caps", outcaps, NULL);

      gst_caps_unref (incaps);
      gst_caps_unref (outcaps);

      gst_element_set_state (pipeline, GST_STATE_PLAYING);

      sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));

      fail_unless (sample != NULL);
      buffer = gst_sample_get_buffer (sample);
      fail_unless (buffer != NULL);
      gst_sample_unref (sample);

      gst_element_set_state (pipeline, GST_STATE_NULL);
    }

    g_object_set (enc, "ascii", TRUE, NULL);
  }

  gst_object_unref (pipeline);
  gst_object_unref (sink);
  gst_object_unref (outcf);
  gst_object_unref (enc);
  gst_object_unref (incf);

}

GST_END_TEST;

/* Push PNM data whose header declares zero dimensions and make sure the
 * decoder fails cleanly with a stream error instead of negotiating a
 * 0x0 output state (which trips a critical in the video decoder base
 * class). P6 fails at the max field, P4 (bitmap) at the height field. */
GST_START_TEST (test_pnmdec_zero_dimensions)
{
  const gchar *headers[] = {
    "P6\n0 0\n255\n0123456789",
    "P4\n0 0\nxx"
  };
  gint i;

  for (i = 0; i < G_N_ELEMENTS (headers); i++) {
    GstElement *pipeline;
    GstElement *src;
    GstBus *bus;
    GstMessage *msg;
    GstFlowReturn ret;
    GstBuffer *buf;

    pipeline = gst_parse_launch ("appsrc name=src ! pnmdec ! fakesink", NULL);
    fail_unless (pipeline != NULL);

    src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
    bus = gst_element_get_bus (pipeline);

    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    buf = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
        (gpointer) headers[i], strlen (headers[i]), 0, strlen (headers[i]),
        NULL, NULL);
    g_signal_emit_by_name (src, "push-buffer", buf, &ret);
    gst_buffer_unref (buf);

    /* No critical so far; the check log handler would have failed the
     * test already otherwise */
    msg = gst_bus_timed_pop_filtered (bus, 5 * GST_SECOND,
        GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
    fail_unless (msg != NULL, "no error message for header '%s'", headers[i]);
    fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR,
        "expected error, got %s for header '%s'",
        GST_MESSAGE_TYPE_NAME (msg), headers[i]);
    gst_message_unref (msg);

    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (bus);
    gst_object_unref (src);
    gst_object_unref (pipeline);
  }
}

GST_END_TEST;

static Suite *
pnm_suite (void)
{
  Suite *s = suite_create ("pnm");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pnm_enc_dec);
  tcase_add_test (tc_chain, test_pnmdec_zero_dimensions);

  return s;
}

GST_CHECK_MAIN (pnm);
