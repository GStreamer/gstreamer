/*
 * GStreamer
 *
 * unit test for aacparse
 *
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
#include <gst/audio/audio.h>
#include "parser.h"

#define SRC_CAPS_TMPL   "audio/mpeg, parsed=(boolean)false, mpegversion=(int)1"
#define SINK_CAPS_TMPL  "audio/mpeg, parsed=(boolean)true, mpegversion=(int)1"

GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL)
    );

GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_TMPL)
    );

/* some data */
static guint8 mp3_frame[384] = {
  0xff, 0xfb, 0x94, 0xc4, 0xff, 0x83, 0xc0, 0x00,
  0x01, 0xa4, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
  0x34, 0x80, 0x00, 0x00, 0x04, 0x00,
};

static guint8 garbage_frame[] = {
  0xff, 0xff, 0xff, 0xff, 0xff
};


GST_START_TEST (test_parse_normal)
{
  gst_parser_test_normal (mp3_frame, sizeof (mp3_frame));
}

GST_END_TEST;


GST_START_TEST (test_parse_drain_single)
{
  gst_parser_test_drain_single (mp3_frame, sizeof (mp3_frame));
}

GST_END_TEST;


GST_START_TEST (test_parse_drain_garbage)
{
  gst_parser_test_drain_garbage (mp3_frame, sizeof (mp3_frame),
      garbage_frame, sizeof (garbage_frame));
}

GST_END_TEST;


GST_START_TEST (test_parse_split)
{
  gst_parser_test_split (mp3_frame, sizeof (mp3_frame));
}

GST_END_TEST;


GST_START_TEST (test_parse_skip_garbage)
{
  gst_parser_test_skip_garbage (mp3_frame, sizeof (mp3_frame),
      garbage_frame, sizeof (garbage_frame));
}

GST_END_TEST;


#define structure_get_int(s,f) \
    (g_value_get_int(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_int_equals(s,field,num) \
    fail_unless_equals_int (structure_get_int(s,field), num)

GST_START_TEST (test_parse_detect_stream)
{
  GstStructure *s;
  GstCaps *caps;

  caps = gst_parser_test_get_output_caps (mp3_frame, sizeof (mp3_frame), NULL);

  fail_unless (caps != NULL);

  GST_LOG ("mpegaudio output caps: %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "audio/mpeg"));
  fail_unless_structure_field_int_equals (s, "mpegversion", 1);
  fail_unless_structure_field_int_equals (s, "layer", 3);
  fail_unless_structure_field_int_equals (s, "channels", 1);
  fail_unless_structure_field_int_equals (s, "rate", 48000);

  gst_caps_unref (caps);
}

GST_END_TEST;


/* Gapless tests are performed using a test signal that contains 30 MPEG
 * frames, has padding samples at the beginning and at the end, a LAME
 * tag to inform about said padding samples, and a sample rate of 32 kHz
 * and 1 channel. The test signal is 1009ms long. setup_gapless_test_info()
 * fills the GaplessTestInfo struct with details about this test signal. */

typedef struct
{
  const gchar *filename;
  guint num_mpeg_frames;
  guint num_samples_per_frame;
  guint num_start_padding_samples;
  guint num_end_padding_samples;
  guint sample_rate;

  guint first_padded_end_frame;
  guint64 num_samples_with_padding;
  guint64 num_samples_without_padding;

  GstClockTime first_frame_duration;
  GstClockTime regular_frame_duration;
  GstClockTime total_duration_without_padding;

  GstElement *appsink;
  GstElement *parser;
} GaplessTestInfo;

static void
setup_gapless_test_info (GaplessTestInfo * info)
{
  info->filename = "sine-1009ms-1ch-32000hz-gapless-with-lame-tag.mp3";
  info->num_mpeg_frames = 31;
  info->num_samples_per_frame = 1152;   /* standard for MP3s */
  info->sample_rate = 32000;

  /* Note that these start and end padding figures are not exactly like
   * those that we get from the LAME tag. That's because that tag only
   * contains the _encoder_ delay & padding. In the figures below, the
   * _decoder_ delay is also factored in (529 samples). mpegaudioparse
   * does the same, so we have to apply it here. */
  info->num_start_padding_samples = 1105;
  info->num_end_padding_samples = 1167;

  /* In MP3s with LAME tags, the first frame is a frame made of Xing/LAME
   * metadata and dummy nullsamples (this is for backwards compatibility).
   * num_start_padding_samples defines how many padding samples are there
   * (this does not include the nullsamples from the first dummy frame).
   * Likewise, num_end_padding_samples defines how many padding samples
   * are there at the end of the MP3 stream.
   * There may be more padding samples than the size of one frame, meaning
   * that there may be frames that are made entirely of padding samples.
   * Such frames are output by mpegaudioparse, but their duration is set
   * to 0, and their PTS corresponds to the last valid PTS in the stream
   * (= the last PTS that is within the actual media data).
   * For this reason, we cannot just assume that the last frame is the
   * one containing padding - there may be more. So, calculate the number
   * of the first frame that contains padding sames from the _end_ of
   * the stream. We'll need that later for buffer PTS and duration checks. */
  info->first_padded_end_frame = (info->num_mpeg_frames - 1 -
      info->num_end_padding_samples / info->num_samples_per_frame);
  info->num_samples_with_padding = (info->num_mpeg_frames - 1) *
      info->num_samples_per_frame;
  info->num_samples_without_padding = info->num_samples_with_padding -
      info->num_start_padding_samples - info->num_end_padding_samples;

  /* The first frame (excluding the dummy frame at the beginning) will be
   * clipped due to the padding samples at the start of the stream, so we
   * have to calculate this separately. */
  info->first_frame_duration =
      gst_util_uint64_scale_int (info->num_samples_per_frame -
      info->num_start_padding_samples, GST_SECOND, info->sample_rate);
  /* Regular, unclipped MPEG frame duration. */
  info->regular_frame_duration =
      gst_util_uint64_scale_int (info->num_samples_per_frame, GST_SECOND,
      info->sample_rate);
  /* The total actual playtime duration. */
  info->total_duration_without_padding =
      gst_util_uint64_scale_int (info->num_samples_without_padding, GST_SECOND,
      info->sample_rate);
}

static void
check_parsed_mpeg_frame (GaplessTestInfo * info, guint frame_num)
{
  GstClockTime expected_pts = GST_CLOCK_TIME_NONE;
  GstClockTime expected_duration = GST_CLOCK_TIME_NONE;
  gboolean expect_audioclipmeta = FALSE;
  guint64 expected_audioclipmeta_start = 0;
  guint64 expected_audioclipmeta_end = 0;
  GstSample *sample;
  GstBuffer *buffer;
  GstAudioClippingMeta *audioclip_meta;

  GST_DEBUG ("checking frame %u", frame_num);

  /* This is called after the frame with the given number has been output by
   * mpegaudioparse. We can then pull that frame from appsink, and check its
   * PTS, duration, and audioclipmeta (if we expect it to be there). */

  if (frame_num == 0) {
    expected_pts = 0;
    expected_duration = 0;
    expect_audioclipmeta = FALSE;
  } else if (frame_num == 1) {
    /* First frame (excluding the dummy metadata frame at the beginning of
     * the MPEG stream that mpegaudioparse internally drops). This one will be
     * clipped due to the padding samples at the beginning, so we expect a
     * clipping meta to be there. Also, its duration will be smaller than that
     * of regular, unclipped frames. */

    expected_pts = 0;
    expected_duration = info->first_frame_duration;

    expect_audioclipmeta = TRUE;
    expected_audioclipmeta_start = info->num_start_padding_samples;
    expected_audioclipmeta_end = 0;
  } else if (frame_num > 1 && frame_num < info->first_padded_end_frame) {
    /* Regular, unclipped frame. */

    expected_pts = info->first_frame_duration + (frame_num - 2) *
        info->regular_frame_duration;
    expected_duration = info->regular_frame_duration;
  } else if (frame_num == info->first_padded_end_frame) {
    /* The first frame at the end with padding samples. This one will have
     * the last few valid samples, followed by the first padding samples. */

    guint64 num_valid_samples = (info->num_samples_with_padding -
        info->num_end_padding_samples) - (frame_num - 1) *
        info->num_samples_per_frame;
    guint64 num_padding_samples = info->num_samples_per_frame -
        num_valid_samples;

    expected_pts = info->first_frame_duration + (frame_num - 2) *
        info->regular_frame_duration;
    expected_duration = gst_util_uint64_scale_int (num_valid_samples,
        GST_SECOND, info->sample_rate);

    expect_audioclipmeta = TRUE;
    expected_audioclipmeta_start = 0;
    expected_audioclipmeta_end = num_padding_samples;
  } else {
    /* A fully clipped frame at the end of the stream. */

    expected_pts = info->total_duration_without_padding;
    expected_duration = 0;

    expect_audioclipmeta = TRUE;
    expected_audioclipmeta_start = 0;
    expected_audioclipmeta_end = info->num_samples_per_frame;
  }

  /* Pull the frame from appsink so we can check it. */

  sample = gst_app_sink_pull_sample (GST_APP_SINK (info->appsink));
  fail_if (sample == NULL);
  fail_unless (GST_IS_SAMPLE (sample));

  buffer = gst_sample_get_buffer (sample);
  fail_if (buffer == NULL);

  /* Verify the sample's PTS and duration. */
  fail_unless_equals_uint64 (GST_BUFFER_PTS (buffer), expected_pts);
  fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer), expected_duration);
  /* Check if there's audio clip metadata, and verify it if it exists. */
  if (expect_audioclipmeta) {
    audioclip_meta = gst_buffer_get_audio_clipping_meta (buffer);
    fail_if (audioclip_meta == NULL);
    fail_unless_equals_uint64 (audioclip_meta->start,
        expected_audioclipmeta_start);
    fail_unless_equals_uint64 (audioclip_meta->end, expected_audioclipmeta_end);
  }

  gst_sample_unref (sample);
}

GST_START_TEST (test_parse_gapless_and_skip_padding_samples)
{
  GstElement *source, *parser, *appsink, *pipeline;
  GstStateChangeReturn state_ret;
  guint frame_num;
  GaplessTestInfo info;

  setup_gapless_test_info (&info);

  pipeline = gst_pipeline_new (NULL);
  source = gst_element_factory_make ("filesrc", NULL);
  parser = gst_element_factory_make ("mpegaudioparse", NULL);
  appsink = gst_element_factory_make ("appsink", NULL);

  info.appsink = appsink;
  info.parser = parser;

  gst_bin_add_many (GST_BIN (pipeline), source, parser, appsink, NULL);
  gst_element_link_many (source, parser, appsink, NULL);

  {
    char *full_filename =
        g_build_filename (GST_TEST_FILES_PATH, info.filename, NULL);
    g_object_set (G_OBJECT (source), "location", full_filename, NULL);
    g_free (full_filename);
  }

  g_object_set (G_OBJECT (appsink), "async", FALSE, "sync", FALSE,
      "max-buffers", 1, "enable-last-sample", FALSE, "processing-deadline",
      G_MAXUINT64, NULL);

  state_ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);

  fail_unless (state_ret != GST_STATE_CHANGE_FAILURE);

  if (state_ret == GST_STATE_CHANGE_ASYNC) {
    GST_LOG ("waiting for pipeline to reach PAUSED state");
    state_ret = gst_element_get_state (pipeline, NULL, NULL, -1);
    fail_unless_equals_int (state_ret, GST_STATE_CHANGE_SUCCESS);
  }

  /* Verify all frames from the test signal. */
  for (frame_num = 0; frame_num < info.num_mpeg_frames; ++frame_num)
    check_parsed_mpeg_frame (&info, frame_num);

  /* Check what duration is returned by a query. This duration must exclude
   * the padding samples. */
  {
    GstQuery *query;
    gint64 duration;
    GstFormat format;

    query = gst_query_new_duration (GST_FORMAT_TIME);
    fail_unless (gst_element_query (pipeline, query));

    gst_query_parse_duration (query, &format, &duration);
    fail_unless_equals_int (format, GST_FORMAT_TIME);
    fail_unless_equals_uint64 ((guint64) duration,
        info.total_duration_without_padding);

    gst_query_unref (query);
  }

  /* Seek tests: Here we seek to a certain position that corresponds to a
   * certain frame. Then we check if we indeed got that frame. */

  /* Seek back to the first frame. */
  {
    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
        GST_STATE_CHANGE_SUCCESS);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT, 0);
    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
        GST_STATE_CHANGE_SUCCESS);

    check_parsed_mpeg_frame (&info, 1);
  }

  /* Seek to the second frame. */
  {
    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
        GST_STATE_CHANGE_SUCCESS);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT, info.first_frame_duration);
    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
        GST_STATE_CHANGE_SUCCESS);

    check_parsed_mpeg_frame (&info, 2);
  }

  /* Seek to the last frame with valid samples (= the first frame with padding
   * samples at the end of the stream). */
  {
    GstClockTime pts = info.first_frame_duration +
        (info.first_padded_end_frame - 2) * info.regular_frame_duration;

    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
        GST_STATE_CHANGE_SUCCESS);
    gst_element_seek_simple (pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT, pts);
    fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
        GST_STATE_CHANGE_SUCCESS);

    check_parsed_mpeg_frame (&info, info.first_padded_end_frame);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;


static Suite *
mpegaudioparse_suite (void)
{
  Suite *s = suite_create ("mpegaudioparse");
  TCase *tc_chain = tcase_create ("general");


  /* init test context */
  ctx_factory = "mpegaudioparse";
  ctx_sink_template = &sinktemplate;
  ctx_src_template = &srctemplate;

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_normal);
  tcase_add_test (tc_chain, test_parse_drain_single);
  tcase_add_test (tc_chain, test_parse_drain_garbage);
  tcase_add_test (tc_chain, test_parse_split);
  tcase_add_test (tc_chain, test_parse_skip_garbage);
  tcase_add_test (tc_chain, test_parse_detect_stream);
  tcase_add_test (tc_chain, test_parse_gapless_and_skip_padding_samples);

  return s;
}


/*
 * TODO:
 *   - Both push- and pull-modes need to be tested
 *      * Pull-mode & EOS
 */
GST_CHECK_MAIN (mpegaudioparse);
