/* GStreamer
 *
 * unit tests for audio support library
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>
#include <string.h>

static gboolean
structure_contains_channel_positions (const GstStructure * s)
{
  return (gst_structure_get_value (s, "channel-positions") != NULL);
}

#if 0
static gboolean
fixed_caps_have_channel_positions (const GstCaps * caps)
{
  GstStructure *s;

  fail_unless (caps != NULL);

  s = gst_caps_get_structure (caps, 0);
  fail_unless (s != NULL);

  return structure_contains_channel_positions (s);
}
#endif

GST_START_TEST (test_multichannel_checks)
{
  GstAudioChannelPosition pos_2_mixed[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
    GST_AUDIO_CHANNEL_POSITION_NONE
  };
  GstAudioChannelPosition pos_2_none[2] = {
    GST_AUDIO_CHANNEL_POSITION_NONE,
    GST_AUDIO_CHANNEL_POSITION_NONE
  };
  GstAudioChannelPosition pos_2_flr[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
  };
  GstAudioChannelPosition pos_2_frr[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
  };
  GstStructure *s;

  s = gst_structure_new ("audio/x-raw-int", "channels", G_TYPE_INT, 2, NULL);

  /* this should not work and issue a warning: FRONT_MONO + NONE */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_channel_positions (s, pos_2_mixed);
  _gst_check_expecting_log = FALSE;
  fail_if (structure_contains_channel_positions (s));

  /* this should work: NONE + NONE */
  gst_audio_set_channel_positions (s, pos_2_none);
  fail_unless (structure_contains_channel_positions (s));
  gst_structure_remove_field (s, "channel-positions");

  /* this should also work: FRONT_LEFT + FRONT_RIGHT */
  gst_audio_set_channel_positions (s, pos_2_flr);
  fail_unless (structure_contains_channel_positions (s));
  gst_structure_remove_field (s, "channel-positions");

  /* this should not work and issue a warning: FRONT_RIGHT twice */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_channel_positions (s, pos_2_frr);
  _gst_check_expecting_log = FALSE;

/* FIXME: did I misunderstand _set_structure_channel_positions_list? */
#if  0
  /* this should not work and issue a warning: FRONT_RIGHT twice */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_frr, 2);
  _gst_check_expecting_log = FALSE;

  /* this should not work and issue a warning: FRONT_MONO + NONE */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_mixed, 2);
  _gst_check_expecting_log = FALSE;

  /* this should not work either (channel count mismatch) */
  _gst_check_expecting_log = TRUE;
  gst_audio_set_structure_channel_positions_list (s, pos_2_none, 44);
  _gst_check_expecting_log = FALSE;
  fail_if (structure_contains_channel_positions (s));
#endif

  gst_structure_free (s);
}

GST_END_TEST;

GST_START_TEST (test_buffer_clipping_time)
{
  GstSegment s;
  GstBuffer *buf;
  GstBuffer *ret;
  guint8 *data;

  /* Clip start and end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 4 * GST_SECOND,
      8 * GST_SECOND, 4 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 800);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 400);

  gst_buffer_unref (ret);

  /* Clip only start */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 4 * GST_SECOND,
      12 * GST_SECOND, 4 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 8 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 1200);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 800);

  gst_buffer_unref (ret);

  /* Clip only stop */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 2 * GST_SECOND,
      10 * GST_SECOND, 2 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 2 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 8 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 200);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 1000);
  fail_unless (GST_BUFFER_DATA (ret) == data);
  fail_unless (GST_BUFFER_SIZE (ret) == 800);

  gst_buffer_unref (ret);

  /* Buffer outside segment */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 12 * GST_SECOND,
      20 * GST_SECOND, 12 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret == NULL);

  /* Clip start and end but don't touch duration and offset_end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 4 * GST_SECOND,
      8 * GST_SECOND, 4 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == GST_CLOCK_TIME_NONE);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == GST_BUFFER_OFFSET_NONE);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 400);

  gst_buffer_unref (ret);

  /* If the buffer has no timestamp it should assert()
   * FIXME: check if return value is the same as the input buffer.
   *        probably can't be done because the assert() does a SIGABRT.
   */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_TIME);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_TIME, 0 * GST_SECOND,
      10 * GST_SECOND, 0 * GST_SECOND);

  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret == buf);

  gst_buffer_unref (buf);

  /* If the format is not TIME or DEFAULT it should assert()
   * FIXME: check if return value is the same as the input buffer.
   *        probably can't be done because the assert() does a SIGABRT.
   */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_PERCENT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_PERCENT, 0, 10, 0);

  GST_BUFFER_TIMESTAMP (buf) = 0 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 0;
  GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ASSERT_CRITICAL (ret = gst_audio_buffer_clip (buf, &s, 100, 1));

  gst_buffer_unref (buf);

}

GST_END_TEST;

GST_START_TEST (test_buffer_clipping_samples)
{
  GstSegment s;
  GstBuffer *buf;
  GstBuffer *ret;
  guint8 *data;

  /* Clip start and end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 400,
      800, 400);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 800);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 400);

  gst_buffer_unref (ret);

  /* Clip only start */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 400,
      1200, 400);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 8 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 1200);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 800);

  gst_buffer_unref (ret);

  /* Clip only stop */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 200,
      1000, 200);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 2 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == 8 * GST_SECOND);
  fail_unless (GST_BUFFER_OFFSET (ret) == 200);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == 1000);
  fail_unless (GST_BUFFER_DATA (ret) == data);
  fail_unless (GST_BUFFER_SIZE (ret) == 800);

  gst_buffer_unref (ret);

  /* Buffer outside segment */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 1200,
      2000, 1200);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret == NULL);

  /* Clip start and end but don't touch duration and offset_end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 400,
      800, 400);

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret != NULL);

  fail_unless (GST_BUFFER_TIMESTAMP (ret) == 4 * GST_SECOND);
  fail_unless (GST_BUFFER_DURATION (ret) == GST_CLOCK_TIME_NONE);
  fail_unless (GST_BUFFER_OFFSET (ret) == 400);
  fail_unless (GST_BUFFER_OFFSET_END (ret) == GST_BUFFER_OFFSET_NONE);
  fail_unless (GST_BUFFER_DATA (ret) == data + 200);
  fail_unless (GST_BUFFER_SIZE (ret) == 400);

  gst_buffer_unref (ret);

  /* If the buffer has no offset it should assert()
   * FIXME: check if return value is the same as the input buffer.
   *        probably can't be done because the assert() does a SIGABRT.
   */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  GST_BUFFER_SIZE (buf) = 1000;
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  gst_segment_set_newsegment (&s, FALSE, 1.0, GST_FORMAT_DEFAULT, 0, 10, 0);

  GST_BUFFER_TIMESTAMP (buf) = 0 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ASSERT_CRITICAL (ret = gst_audio_buffer_clip (buf, &s, 100, 1));

  gst_buffer_unref (buf);
}

GST_END_TEST;

static Suite *
audio_suite (void)
{
  Suite *s = suite_create ("audio support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_multichannel_checks);
  tcase_add_test (tc_chain, test_buffer_clipping_time);
  tcase_add_test (tc_chain, test_buffer_clipping_samples);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audio_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
