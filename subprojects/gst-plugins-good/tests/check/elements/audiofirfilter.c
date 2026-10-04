/* GStreamer
 *
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/* FIXME 0.11: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <math.h>
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/audio/audio.h>

static gboolean have_eos = FALSE;

static gboolean
on_message (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    case GST_MESSAGE_WARNING:
      g_assert_not_reached ();
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_EOS:
      have_eos = TRUE;
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
on_rate_changed (GstElement * element, gint rate, gpointer user_data)
{
  GValueArray *va;
  GValue v = { 0, };

  fail_unless (rate > 0);

  va = g_value_array_new (6);

  g_value_init (&v, G_TYPE_DOUBLE);
  g_value_set_double (&v, 0.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);
  g_value_set_double (&v, 0.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);
  g_value_set_double (&v, 0.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);
  g_value_set_double (&v, 0.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);
  g_value_set_double (&v, 0.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);
  g_value_set_double (&v, 1.0);
  g_value_array_append (va, &v);
  g_value_reset (&v);

  g_object_set (G_OBJECT (element), "kernel", va, NULL);

  g_value_array_free (va);
}

static gboolean have_data = FALSE;

static void
on_handoff (GstElement * object, GstBuffer * buffer, GstPad * pad,
    gpointer user_data)
{
  if (!have_data) {
    GstMapInfo map;
    gdouble *data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = (gdouble *) map.data;

    fail_unless (map.size > 5 * sizeof (gdouble));
    fail_unless (data[0] == 0.0);
    fail_unless (data[1] == 0.0);
    fail_unless (data[2] == 0.0);
    fail_unless (data[3] == 0.0);
    fail_unless (data[4] == 0.0);
    fail_unless (data[5] != 0.0);

    gst_buffer_unmap (buffer, &map);
    have_data = TRUE;
  }
}

GST_START_TEST (test_pipeline)
{
  GstElement *pipeline, *src, *cfilter, *filter, *sink;
  GstCaps *caps;
  GstBus *bus;
  GMainLoop *loop;

  have_data = FALSE;
  have_eos = FALSE;

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fail_unless (pipeline != NULL);

  src = gst_element_factory_make ("audiotestsrc", NULL);
  fail_unless (src != NULL);
  g_object_set (G_OBJECT (src), "num-buffers", 1000, NULL);

  cfilter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (cfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, GST_AUDIO_NE (F64), NULL);
  g_object_set (G_OBJECT (cfilter), "caps", caps, NULL);
  gst_caps_unref (caps);

  filter = gst_element_factory_make ("audiofirfilter", NULL);
  fail_unless (filter != NULL);
  g_signal_connect (G_OBJECT (filter), "rate-changed",
      G_CALLBACK (on_rate_changed), NULL);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);
  g_object_set (G_OBJECT (sink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (sink), "handoff", G_CALLBACK (on_handoff), NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, cfilter, filter, sink, NULL);
  fail_unless (gst_element_link_many (src, cfilter, filter, sink, NULL));

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (on_message), loop);

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (loop);

  fail_unless (have_data);
  fail_unless (have_eos);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_bus_remove_signal_watch (bus);
  gst_object_unref (GST_OBJECT (bus));
  g_main_loop_unref (loop);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/*
 * Number of taps and channels used by test_fft_stereo_eos_flush().
 * 64 taps put the element into FFT mode (threshold is 32 taps), 2
 * channels are required to exercise interleaved data in the EOS
 * residue flush.
 */
#define FIR_TEST_TAPS 64
#define FIR_TEST_CHANNELS 2

/*
 * Latency must be > 0 for the test to be meaningful. In FFT mode EOS
 * flush produces (N mod blocklen) + latency samples and is skipped
 * entirely if that is 0. With latency = 0, the flush would only run
 * if the input length happens to be a non-multiple of the FFT output
 * block length.
 */
#define FIR_TEST_LATENCY (FIR_TEST_TAPS - 1)

/*
 * In FFT mode the residue flushed at EOS is (N mod blocklen) + latency
 * frames, where N is the input length in frames and blocklen is the
 * number of output frames per FFT pass.
 *
 * block_length - kernel_length + 1 = 193
 * block_length = gst_fft_next_fast_length(4 * taps) = 256.
 *
 * N is chosen such that this residue is larger than one block. Flush
 * loop in push_residue then runs twice and its memcpy of the second
 * pass runs with gensamples != 0. That covers the channel-dependent
 * destination offset, not only the copy length.
 *
 * N = 13 * 1024 = 13312 frames gives a residue of
 * (13312 mod 193) + 63 = 251 frames.
 */
#define FIR_TEST_SAMPLES_PER_BUFFER 1024
#define FIR_TEST_NUM_BUFFERS 13

static GArray *collected_in = NULL;
static GArray *collected_out = NULL;

static GstPadProbeReturn
collect_samples_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GArray *array = user_data;
  GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);
  GstMapInfo map;

  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    g_array_append_vals (array, map.data, map.size / sizeof (gdouble));
    gst_buffer_unmap (buffer, &map);
  }

  return GST_PAD_PROBE_OK;
}

/*
 * Verify that the output of the filter matches the reference convolution
 * of the captured input, shifted by the latency property. In particular
 * this covers the tail pushed by push_residue() at EOS.
 */
GST_START_TEST (test_fft_stereo_eos_flush)
{
  GstElement *pipeline, *src, *cfilter, *filter, *sink;
  GstPad *pad;
  GstCaps *caps;
  GstBus *bus;
  GMainLoop *loop;
  GValueArray *va;
  GValue v = { 0, };
  gdouble kernel[FIR_TEST_TAPS];
  gdouble max_err = 0.0;
  guint i, t, c;
  guint in_frames, out_frames;

  have_eos = FALSE;

  collected_in = g_array_new (FALSE, FALSE, sizeof (gdouble));
  collected_out = g_array_new (FALSE, FALSE, sizeof (gdouble));

  for (i = 0; i < FIR_TEST_TAPS; i++)
    kernel[i] = ((i * 7919) % 101) / 100.0 - 0.5;

  pipeline = gst_element_factory_make ("pipeline", NULL);
  fail_unless (pipeline != NULL);

  src = gst_element_factory_make ("audiotestsrc", NULL);
  fail_unless (src != NULL);
  g_object_set (src, "wave", 5, "samplesperbuffer", FIR_TEST_SAMPLES_PER_BUFFER,
      "num-buffers", FIR_TEST_NUM_BUFFERS, NULL);

  cfilter = gst_element_factory_make ("capsfilter", NULL);
  fail_unless (cfilter != NULL);
  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, GST_AUDIO_NE (F64),
      "channels", G_TYPE_INT, FIR_TEST_CHANNELS,
      "layout", G_TYPE_STRING, "interleaved", NULL);
  g_object_set (cfilter, "caps", caps, NULL);
  gst_caps_unref (caps);

  filter = gst_element_factory_make ("audiofirfilter", NULL);
  fail_unless (filter != NULL);

  va = g_value_array_new (FIR_TEST_TAPS);
  g_value_init (&v, G_TYPE_DOUBLE);
  for (i = 0; i < FIR_TEST_TAPS; i++) {
    g_value_set_double (&v, kernel[i]);
    g_value_array_append (va, &v);
    g_value_reset (&v);
  }
  g_object_set (filter, "kernel", va, NULL);
  g_object_set (filter, "latency", (guint64) FIR_TEST_LATENCY, NULL);
  g_value_array_free (va);
  g_value_unset (&v);

  sink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (sink != NULL);
  g_object_set (sink, "sync", FALSE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, cfilter, filter, sink, NULL);
  fail_unless (gst_element_link_many (src, cfilter, filter, sink, NULL));

  pad = gst_element_get_static_pad (filter, "sink");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, collect_samples_cb,
      collected_in, NULL);
  gst_object_unref (pad);
  pad = gst_element_get_static_pad (filter, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, collect_samples_cb,
      collected_out, NULL);
  gst_object_unref (pad);

  loop = g_main_loop_new (NULL, FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (on_message), loop);

  fail_if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (loop);

  fail_unless (have_eos);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_bus_remove_signal_watch (bus);
  gst_object_unref (G_OBJECT (bus));
  g_main_loop_unref (loop);
  gst_object_unref (pipeline);

  in_frames = collected_in->len / FIR_TEST_CHANNELS;
  out_frames = collected_out->len / FIR_TEST_CHANNELS;
  fail_unless (in_frames > 0 && out_frames == in_frames,
      "expected %u output frames, got %u",
      (guint) in_frames, (guint) out_frames);

  for (t = 0; t < out_frames; t++) {
    for (c = 0; c < FIR_TEST_CHANNELS; c++) {
      gdouble expected = 0.0, err;

      /* out[i] corresponds to y[i + latency] */
      for (i = 0; i < FIR_TEST_TAPS; i++) {
        gint idx = (gint) (t + FIR_TEST_LATENCY) - (gint) i;

        if (idx >= 0 && (guint) idx < in_frames)
          expected +=
              ((gdouble *) collected_in->data)[idx * FIR_TEST_CHANNELS + c]
              * kernel[i];
      }
      err = fabs (expected -
          ((gdouble *) collected_out->data)[t * FIR_TEST_CHANNELS + c]);
      if (err > max_err)
        max_err = err;
    }
  }

  fail_unless (max_err < 1e-9,
      "output does not match reference convolution, max error %g", max_err);

  g_array_free (collected_in, TRUE);
  g_array_free (collected_out, TRUE);
  collected_in = NULL;
  collected_out = NULL;
}

GST_END_TEST;

static Suite *
audiofirfilter_suite (void)
{
  Suite *s = suite_create ("audiofirfilter");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pipeline);
  tcase_add_test (tc_chain, test_fft_stereo_eos_flush);

  return s;
}

GST_CHECK_MAIN (audiofirfilter);
