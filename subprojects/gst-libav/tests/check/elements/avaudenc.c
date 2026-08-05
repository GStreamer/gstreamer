/* GStreamer
 *
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/audio/audio.h>

GST_START_TEST (test_audioenc_drain)
{
  GstHarness *h;
  GstAudioInfo info;
  GstBuffer *in_buf;
  gint i = 0;
  gint num_output = 0;
  GstFlowReturn ret;
  GstSegment segment;
  GstCaps *caps;
  gint samples_per_buffer = 1024;
  gint rate = 44100;
  gint size;
  GstClockTime duration;

  h = gst_harness_new ("avenc_aac");
  fail_unless (h != NULL);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, rate, 1, NULL);

  caps = gst_audio_info_to_caps (&info);
  gst_harness_set_src_caps (h, gst_caps_copy (caps));

  duration = gst_util_uint64_scale_int (samples_per_buffer, GST_SECOND, rate);
  size = samples_per_buffer * GST_AUDIO_INFO_BPF (&info);

  for (i = 0; i < 2; i++) {
    in_buf = gst_buffer_new_and_alloc (size);

    gst_buffer_memset (in_buf, 0, 0, size);

    /* small rounding error would be expected, but should be fine */
    GST_BUFFER_PTS (in_buf) = i * duration;
    GST_BUFFER_DURATION (in_buf) = duration;

    ret = gst_harness_push (h, in_buf);

    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));
  }

  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_segment_set_running_time (&segment, GST_FORMAT_TIME,
          2 * duration));

  /* Push new eos event to drain encoder */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));

  /* And start new stream */
  fail_unless (gst_harness_push_event (h,
          gst_event_new_stream_start ("new-stream-id")));
  gst_harness_set_src_caps (h, caps);
  fail_unless (gst_harness_push_event (h, gst_event_new_segment (&segment)));

  in_buf = gst_buffer_new_and_alloc (size);

  GST_BUFFER_PTS (in_buf) = 2 * duration;
  GST_BUFFER_DURATION (in_buf) = duration;

  ret = gst_harness_push (h, in_buf);
  fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
      gst_flow_get_name (ret));

  /* Finish encoding and drain again */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));
  do {
    GstBuffer *out_buf = NULL;

    out_buf = gst_harness_try_pull (h);
    if (out_buf) {
      num_output++;
      gst_buffer_unref (out_buf);
      continue;
    }

    break;
  } while (1);

  fail_unless (num_output >= 3);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_audioenc_16_channels)
{
  /* avaudenc used to have a bug for >8ch where a double-free attempt would occur,
   * crashing the whole process. Since >8ch encoding is quite rarely used, this test
   * is meant to detect any crashes that would indicate somebody broke that again */
  GstHarness *h;
  GstAudioInfo info;
  GstBuffer *in_buf;
  GstCaps *caps;
  gint size;
  GstAudioChannelPosition position[16];
  /* 16ch hexadecagonal layout */
  guint64 channel_mask = 0x3137D37;

  h = gst_harness_new ("avenc_aac");
  fail_unless (h != NULL);

  gst_audio_channel_positions_from_mask (16, channel_mask, position);
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 44100, 16, position);

  caps = gst_audio_info_to_caps (&info);
  gst_harness_set_src_caps (h, caps);

  size = 1024 * GST_AUDIO_INFO_BPF (&info);
  in_buf = gst_buffer_new_and_alloc (size);
  gst_buffer_memset (in_buf, 0, 0, size);
  GST_BUFFER_PTS (in_buf) = 0;

  GstFlowReturn ret = gst_harness_push (h, in_buf);
  fail_if (ret != GST_FLOW_OK);

  gst_harness_teardown (h);
}

GST_END_TEST;

// Make sure we fix up any too-small memory alignment before feeding data
// to FFmpeg. By default we use the malloc alignment, which might be 16,
// but FFmpeg might be using SIMD operations that require a bigger alignment.
GST_START_TEST (test_audioenc_alignment_fixup)
{
  GstHarness *h;
  GstAudioInfo info;
  GstCaps *caps;

  h = gst_harness_new ("avenc_ac3");
  fail_unless (h != NULL);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 44100, 1, NULL);

  caps = gst_audio_info_to_caps (&info);
  gst_harness_set_src_caps (h, caps);

  fail_unless_equals_int (GST_AUDIO_INFO_BPF (&info), sizeof (float));

  // AC-3 has 1536 samples per frame. Need to supply that many per buffer,
  // otherwise the audio encoder baseclass will realloc things via GstAdapter
  // and mess up our carefully curated audio buffer (mis)alignment.
# define N_SAMPLES 1536
# define N_ALIGNMENTS 16

  const gsize size = N_SAMPLES * sizeof (float);

  float *samples = g_new0 (float, (N_SAMPLES + N_ALIGNMENTS));

  guint64 offset = 0;

  for (int i = 0; i < 100; ++i) {
    GstMemory *mem = gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
        samples + (i % N_ALIGNMENTS), size, 0, size,
        NULL, NULL);

    GstBuffer *in_buf = gst_buffer_new ();
    gst_buffer_insert_memory (in_buf, 0, mem);

    GST_BUFFER_PTS (in_buf) = gst_util_uint64_scale (offset, GST_SECOND, 44100);

    GstFlowReturn ret = gst_harness_push (h, g_steal_pointer (&in_buf));
    fail_unless_equals_int (ret, GST_FLOW_OK);
    offset += N_SAMPLES;
  }

  g_free (samples);

  gst_harness_teardown (h);
}

GST_END_TEST;

/* A codec that declares AV_CODEC_CAP_ENCODER_FLUSH stays usable after
 * avcodec_flush_buffers(), so the drain of such an encoder used to start over
 * on every pass and hand the base class more packets forever. The AudioToolbox
 * encoders are the only ones that declare the capability, which limits this
 * test to macOS. A regression makes the encoder run until the timeout of the
 * test case, because the drain never gives control back. */
GST_START_TEST (test_audioenc_drain_encoder_flush)
{
  GstHarness *h;
  GstAudioInfo info;
  GstCaps *caps;
  gint i;
  gint num_output = 0;
  gint rate = 48000;
  gint samples_per_buffer = 1024;
  gint size;
  GstClockTime duration;

  if (!gst_element_factory_find ("avenc_aac_at")) {
    GST_INFO ("avenc_aac_at is not available, skipping test");
    return;
  }

  h = gst_harness_new ("avenc_aac_at");
  fail_unless (h != NULL);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, rate, 2, NULL);
  caps = gst_audio_info_to_caps (&info);
  gst_harness_set_src_caps (h, caps);

  duration = gst_util_uint64_scale_int (samples_per_buffer, GST_SECOND, rate);
  size = samples_per_buffer * GST_AUDIO_INFO_BPF (&info);

  for (i = 0; i < 10; i++) {
    GstBuffer *in_buf = gst_buffer_new_and_alloc (size);
    GstFlowReturn ret;

    gst_buffer_memset (in_buf, 0, 0, size);
    GST_BUFFER_PTS (in_buf) = i * duration;
    GST_BUFFER_DURATION (in_buf) = duration;

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));
  }

  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));

  while (TRUE) {
    GstBuffer *out_buf = gst_harness_try_pull (h);

    if (!out_buf)
      break;

    num_output++;
    gst_buffer_unref (out_buf);
  }

  /* Ten frames in, and AAC adds a frame of priming. A drain that repeats
   * passes this by orders of magnitude. */
  fail_unless (num_output > 0);
  fail_unless (num_output < 100, "encoder produced %d buffers for 10 frames",
      num_output);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
avaudenc_suite (void)
{
  Suite *s = suite_create ("avaudenc");
  TCase *tc_chain = tcase_create ("general");
  TCase *tc_encoder_flush = tcase_create ("encoder-flush");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_audioenc_drain);
  tcase_add_test (tc_chain, test_audioenc_16_channels);
  tcase_add_test (tc_chain, test_audioenc_alignment_fixup);

  /* The timeout is the only bound on a drain that repeats, so this test needs
   * a case of its own. */
  suite_add_tcase (s, tc_encoder_flush);
  tcase_set_timeout (tc_encoder_flush, 30);
  tcase_add_test (tc_encoder_flush, test_audioenc_drain_encoder_flush);

  return s;
}

GST_CHECK_MAIN (avaudenc)
