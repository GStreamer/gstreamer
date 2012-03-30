/* GStreamer
 *
 * unit tests for audio support library
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2011 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <string.h>

GST_START_TEST (test_buffer_clipping_time)
{
  GstSegment s;
  GstBuffer *buf;
  GstBuffer *ret;
  GstMapInfo map;
  guint8 *data;

  /* Clip start and end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 4 * GST_SECOND;
  s.stop = 8 * GST_SECOND;
  s.time = 4 * GST_SECOND;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 400);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Clip only start */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 4 * GST_SECOND;
  s.stop = 12 * GST_SECOND;
  s.time = 4 * GST_SECOND;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 800);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Clip only stop */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 2 * GST_SECOND;
  s.stop = 10 * GST_SECOND;
  s.time = 2 * GST_SECOND;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data);
  fail_unless (map.size == 800);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Buffer outside segment */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 12 * GST_SECOND;
  s.stop = 20 * GST_SECOND;
  s.time = 12 * GST_SECOND;

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret == NULL);

  /* Clip start and end but don't touch duration and offset_end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 4 * GST_SECOND;
  s.stop = 8 * GST_SECOND;
  s.time = 4 * GST_SECOND;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 400);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* If the buffer has no timestamp it should assert()
   * FIXME: check if return value is the same as the input buffer.
   *        probably can't be done because the assert() does a SIGABRT.
   */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_TIME);
  s.start = 0 * GST_SECOND;
  s.stop = 10 * GST_SECOND;
  s.time = 0 * GST_SECOND;

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
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_PERCENT);
  s.start = 0;
  s.stop = 10;
  s.time = 0;

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
  GstMapInfo map;
  guint8 *data;

  /* Clip start and end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 400;
  s.stop = 800;
  s.time = 400;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 400);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Clip only start */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 400;
  s.stop = 1200;
  s.time = 400;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 800);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Clip only stop */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 200;
  s.stop = 1000;
  s.time = 200;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data);
  fail_unless (map.size == 800);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* Buffer outside segment */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 1200;
  s.stop = 2000;
  s.time = 1200;

  GST_BUFFER_TIMESTAMP (buf) = 2 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = 10 * GST_SECOND;
  GST_BUFFER_OFFSET (buf) = 200;
  GST_BUFFER_OFFSET_END (buf) = 1200;

  ret = gst_audio_buffer_clip (buf, &s, 100, 1);
  fail_unless (ret == NULL);

  /* Clip start and end but don't touch duration and offset_end */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 400;
  s.stop = 800;
  s.time = 400;

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
  gst_buffer_map (ret, &map, GST_MAP_READ);
  fail_unless (map.data == data + 200);
  fail_unless (map.size == 400);
  gst_buffer_unmap (ret, &map);

  gst_buffer_unref (ret);

  /* If the buffer has no offset it should assert()
   * FIXME: check if return value is the same as the input buffer.
   *        probably can't be done because the assert() does a SIGABRT.
   */
  buf = gst_buffer_new ();
  data = (guint8 *) g_malloc (1000);
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, 1000, 0, 1000, data, g_free));

  gst_segment_init (&s, GST_FORMAT_DEFAULT);
  s.start = 0;
  s.stop = 10;
  s.time = 0;

  GST_BUFFER_TIMESTAMP (buf) = 0 * GST_SECOND;
  GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_OFFSET (buf) = GST_BUFFER_OFFSET_NONE;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET_NONE;

  ASSERT_CRITICAL (ret = gst_audio_buffer_clip (buf, &s, 100, 1));

  gst_buffer_unref (buf);
}

GST_END_TEST;

GST_START_TEST (test_multichannel_checks)
{
  GstAudioChannelPosition pos_2_mixed[2] = {
    GST_AUDIO_CHANNEL_POSITION_MONO,
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
  GstAudioChannelPosition pos_2_frl[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT
  };
  GstAudioChannelPosition pos_2_frr[2] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
  };
  GstAudioChannelPosition pos_3_flrc[3] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER
  };
  GstAudioChannelPosition pos_3_frcl[3] = {
    GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
    GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT
  };
  GstAudioInfo info, info2;
  GstCaps *caps;

  gst_audio_info_init (&info);
  gst_audio_info_init (&info2);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000, 2, NULL);
  fail_unless (memcmp (&info.position, pos_2_flr, sizeof (pos_2_flr)) == 0);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000, 2, pos_2_flr);
  fail_unless (memcmp (&info.position, pos_2_flr, sizeof (pos_2_flr)) == 0);
  caps = gst_audio_info_to_caps (&info);
  fail_unless (gst_audio_info_from_caps (&info2, caps));
  fail_unless (memcmp (&info, &info2, sizeof (info)) == 0);
  gst_caps_unref (caps);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000, 2, pos_2_none);
  fail_unless (memcmp (&info.position, pos_2_none, sizeof (pos_2_none)) == 0);
  caps = gst_audio_info_to_caps (&info);
  fail_unless (gst_audio_info_from_caps (&info2, caps));
  fail_unless (memcmp (&info, &info2, sizeof (info)) == 0);
  gst_caps_unref (caps);

  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000, 3, pos_3_flrc);
  fail_unless (memcmp (&info.position, pos_3_flrc, sizeof (pos_3_flrc)) == 0);
  caps = gst_audio_info_to_caps (&info);
  fail_unless (gst_audio_info_from_caps (&info2, caps));
  fail_unless (memcmp (&info, &info2, sizeof (info)) == 0);
  gst_caps_unref (caps);

  ASSERT_WARNING (gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000,
          2, pos_2_frl));
  ASSERT_WARNING (gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000,
          2, pos_2_mixed));
  ASSERT_WARNING (gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000,
          2, pos_2_frr));
  ASSERT_WARNING (gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_F32, 48000,
          3, pos_3_frcl));
}

GST_END_TEST;

typedef struct
{
  gint channels;
  GstAudioChannelPosition from[32], to[32];
  gint32 in[32], out[32];
  gboolean fail;
} MultichannelReorderData;

GST_START_TEST (test_multichannel_reorder)
{
  MultichannelReorderData tests[] = {
    {1,
          {GST_AUDIO_CHANNEL_POSITION_MONO},
          {GST_AUDIO_CHANNEL_POSITION_MONO},
          {0, 1, 2, 3},
          {0, 1, 2, 3},
        FALSE},
    {1,
          {GST_AUDIO_CHANNEL_POSITION_MONO},
          {GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER},
          {0, 1, 2, 3},
          {0, 1, 2, 3},
        TRUE},
    {2,
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
          {0, 1, 2, 3},
          {0, 1, 2, 3},
        FALSE},
    {2,
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
          {GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT},
          {0, 1, 2, 3},
          {1, 0, 3, 2},
        FALSE},
    {4,
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
          {GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
                GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
          {0, 1, 2, 3},
          {1, 2, 0, 3},
        FALSE},
    {4,
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
          {GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
                GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER},
          {0, 1, 2, 3},
          {3, 0, 1, 2},
        FALSE},
    {4,
          {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
                GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
          {GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
                GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
                GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT},
          {0, 1, 2, 3},
          {3, 2, 1, 0},
        FALSE},
  };
  gint i;
  GstBuffer *buf;
  GstMapInfo map;

  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    buf =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, tests[i].in,
        sizeof (tests[i].in), 0, sizeof (tests[i].in), NULL, NULL);

    if (tests[i].fail) {
      fail_if (gst_audio_buffer_reorder_channels (buf, GST_AUDIO_FORMAT_S32,
              tests[i].channels, tests[i].from, tests[i].to));
    } else {
      fail_unless (gst_audio_buffer_reorder_channels (buf, GST_AUDIO_FORMAT_S32,
              tests[i].channels, tests[i].from, tests[i].to));

      gst_buffer_map (buf, &map, GST_MAP_READ);
      fail_unless_equals_int (map.size, sizeof (tests[i].in));
      fail_unless (memcmp (tests[i].out, map.data, map.size) == 0);
      gst_buffer_unmap (buf, &map);
    }
    gst_buffer_unref (buf);
  }
}

GST_END_TEST;

GST_START_TEST (test_audio_info)
{
  GstAudioFormat fmt;

  fmt = gst_audio_format_build_integer (TRUE, G_BYTE_ORDER, 8, 8);
  fail_unless (fmt == GST_AUDIO_FORMAT_S8);

  fmt = gst_audio_format_build_integer (FALSE, G_BYTE_ORDER, 8, 8);
  fail_unless (fmt == GST_AUDIO_FORMAT_U8);
}

GST_END_TEST;

static Suite *
audio_suite (void)
{
  Suite *s = suite_create ("audio support library");

  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_audio_info);
  tcase_add_test (tc_chain, test_buffer_clipping_time);
  tcase_add_test (tc_chain, test_buffer_clipping_samples);
  tcase_add_test (tc_chain, test_multichannel_checks);
  tcase_add_test (tc_chain, test_multichannel_reorder);

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
