/*
 * GStreamer
 *
 * unit test for h265parse
 *
 *  Copyright (C) 2020 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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
#include "av1parse.h"
#include <string.h>

static void
check_caps_event (GstHarness * h)
{
  GstEvent *event;
  GstCaps *caps = NULL;
  GstStructure *s;
  const gchar *profile;
  gint width, height;
  guint depth;

  while ((event = gst_harness_try_pull_event (h))) {
    GstCaps *event_caps;
    if (GST_EVENT_TYPE (event) != GST_EVENT_CAPS) {
      gst_event_unref (event);
      continue;
    }

    gst_event_parse_caps (event, &event_caps);
    gst_caps_replace (&caps, event_caps);
    gst_event_unref (event);
  }

  fail_unless (caps != NULL);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_get_int (s, "width", &width));
  fail_unless (gst_structure_get_int (s, "height", &height));
  fail_unless ((profile = gst_structure_get_string (s, "profile")));
  fail_unless (gst_structure_get_uint (s, "bit-depth-chroma", &depth));

  fail_unless_equals_int (width, 400);
  fail_unless_equals_int (height, 300);
  fail_unless_equals_int (depth, 8);
  fail_unless_equals_string (profile, "main");
  gst_caps_unref (caps);
}

GST_START_TEST (test_byte_to_frame)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gint i = 0;
  guint offset;
  guint len;
  guint output_buf_num;

  h = gst_harness_new_parse ("av1parse");
  fail_unless (h != NULL);

  gst_harness_set_sink_caps_str (h, "video/x-av1,parsed=(boolean)true,"
      "alignment=(string)frame,stream-format=(string)obu-stream");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  gst_harness_play (h);

  output_buf_num = 0;
  offset = 0;
  len = stream_no_annexb_av1_len / 5;
  for (i = 0; i < 5; i++) {
    if (i == 4)
      len = stream_no_annexb_av1_len - offset;

    in_buf = gst_buffer_new_and_alloc (len);
    gst_buffer_map (in_buf, &map, GST_MAP_WRITE);
    memcpy (map.data, stream_no_annexb_av1 + offset, len);
    gst_buffer_unmap (in_buf, &map);
    offset += len;

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    gst_clear_buffer (&out_buf);
    while ((out_buf = gst_harness_try_pull (h)) != NULL) {
      if (output_buf_num == 0)
        check_caps_event (h);

      fail_unless (gst_buffer_get_size (out_buf) ==
          stream_av1_frame_size[output_buf_num]);

      gst_clear_buffer (&out_buf);
      output_buf_num++;
    }
  }

  fail_unless (output_buf_num == 14);

  gst_harness_teardown (h);

}

GST_END_TEST;

GST_START_TEST (test_byte_to_annexb)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gint i = 0;
  guint offset;
  guint len;
  guint output_buf_num;

  h = gst_harness_new_parse ("av1parse");
  fail_unless (h != NULL);

  gst_harness_set_sink_caps_str (h, "video/x-av1,parsed=(boolean)true,"
      "alignment=(string)tu,stream-format=(string)annexb");
  gst_harness_set_src_caps_str (h, "video/x-av1,alignment=(string)byte");

  gst_harness_play (h);

  output_buf_num = 0;
  offset = 0;
  len = stream_no_annexb_av1_len / 5;
  for (i = 0; i < 5; i++) {
    if (i == 4)
      len = stream_no_annexb_av1_len - offset;

    in_buf = gst_buffer_new_and_alloc (len);
    gst_buffer_map (in_buf, &map, GST_MAP_WRITE);
    memcpy (map.data, stream_no_annexb_av1 + offset, len);
    gst_buffer_unmap (in_buf, &map);
    offset += len;

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    gst_clear_buffer (&out_buf);
    while ((out_buf = gst_harness_try_pull (h)) != NULL) {
      if (output_buf_num == 0)
        check_caps_event (h);

      fail_unless (gst_buffer_get_size (out_buf) ==
          stream_annexb_av1_tu_len[output_buf_num]);

      gst_clear_buffer (&out_buf);
      output_buf_num++;
    }
  }

  /* The last TU need EOS */
  fail_unless (gst_harness_push_event (h, gst_event_new_eos ()));
  out_buf = gst_harness_try_pull (h);
  fail_unless (out_buf);
  fail_unless (gst_buffer_get_size (out_buf) ==
      stream_annexb_av1_tu_len[output_buf_num]);
  output_buf_num++;
  gst_clear_buffer (&out_buf);

  fail_unless (output_buf_num == 10);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_annexb_to_frame)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gint i = 0;
  guint offset;
  guint output_buf_num;

  h = gst_harness_new_parse ("av1parse");
  fail_unless (h != NULL);

  gst_harness_set_sink_caps_str (h, "video/x-av1,parsed=(boolean)true,"
      "alignment=(string)frame,stream-format=(string)obu-stream");
  gst_harness_set_src_caps_str (h, "video/x-av1,alignment=(string)tu,"
      "stream-format=(string)annexb");

  gst_harness_play (h);

  output_buf_num = 0;
  offset = 0;
  for (i = 0; i < G_N_ELEMENTS (stream_annexb_av1_tu_len); i++) {
    in_buf = gst_buffer_new_and_alloc (stream_annexb_av1_tu_len[i]);
    gst_buffer_map (in_buf, &map, GST_MAP_WRITE);
    memcpy (map.data, stream_annexb_av1 + offset, stream_annexb_av1_tu_len[i]);
    gst_buffer_unmap (in_buf, &map);
    offset += stream_annexb_av1_tu_len[i];

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    gst_clear_buffer (&out_buf);
    while ((out_buf = gst_harness_try_pull (h)) != NULL) {
      if (output_buf_num == 0)
        check_caps_event (h);

      fail_unless (gst_buffer_get_size (out_buf) ==
          stream_av1_frame_size[output_buf_num]);

      gst_clear_buffer (&out_buf);
      output_buf_num++;
    }
  }

  fail_unless (output_buf_num == 14);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_annexb_to_obu)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gint i = 0;
  guint offset;
  guint output_buf_num;

  h = gst_harness_new_parse ("av1parse");
  fail_unless (h != NULL);

  gst_harness_set_sink_caps_str (h, "video/x-av1,parsed=(boolean)true,"
      "alignment=(string)obu");
  gst_harness_set_src_caps_str (h, "video/x-av1,alignment=(string)tu,"
      "stream-format=(string)annexb");

  gst_harness_play (h);

  output_buf_num = 0;
  offset = 0;
  for (i = 0; i < G_N_ELEMENTS (stream_annexb_av1_tu_len); i++) {
    in_buf = gst_buffer_new_and_alloc (stream_annexb_av1_tu_len[i]);
    gst_buffer_map (in_buf, &map, GST_MAP_WRITE);
    memcpy (map.data, stream_annexb_av1 + offset, stream_annexb_av1_tu_len[i]);
    gst_buffer_unmap (in_buf, &map);
    offset += stream_annexb_av1_tu_len[i];

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    gst_clear_buffer (&out_buf);
    while ((out_buf = gst_harness_try_pull (h)) != NULL) {
      if (output_buf_num == 0)
        check_caps_event (h);

      fail_unless (gst_buffer_get_size (out_buf) ==
          stream_av1_obu_size[output_buf_num]);

      gst_clear_buffer (&out_buf);
      output_buf_num++;
    }
  }

  fail_unless (output_buf_num == G_N_ELEMENTS (stream_av1_obu_size));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_byte_to_obu)
{
  GstHarness *h;
  GstBuffer *in_buf, *out_buf = NULL;
  GstMapInfo map;
  GstFlowReturn ret;
  gint i = 0;
  guint offset;
  guint len;
  guint output_buf_num;

  h = gst_harness_new_parse ("av1parse");
  fail_unless (h != NULL);

  gst_harness_set_sink_caps_str (h, "video/x-av1,parsed=(boolean)true,"
      "alignment=(string)obu,stream-format=(string)obu-stream");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  gst_harness_play (h);

  output_buf_num = 0;
  offset = 0;
  len = stream_no_annexb_av1_len / 5;
  for (i = 0; i < 5; i++) {
    if (i == 4)
      len = stream_no_annexb_av1_len - offset;

    in_buf = gst_buffer_new_and_alloc (len);
    gst_buffer_map (in_buf, &map, GST_MAP_WRITE);
    memcpy (map.data, stream_no_annexb_av1 + offset, len);
    gst_buffer_unmap (in_buf, &map);
    offset += len;

    ret = gst_harness_push (h, in_buf);
    fail_unless (ret == GST_FLOW_OK, "GstFlowReturn was %s",
        gst_flow_get_name (ret));

    gst_clear_buffer (&out_buf);
    while ((out_buf = gst_harness_try_pull (h)) != NULL) {
      if (output_buf_num == 0)
        check_caps_event (h);

      fail_unless (gst_buffer_get_size (out_buf) ==
          stream_av1_obu_size[output_buf_num]);

      gst_clear_buffer (&out_buf);
      output_buf_num++;
    }
  }

  fail_unless (output_buf_num == G_N_ELEMENTS (stream_av1_obu_size));

  gst_harness_teardown (h);

}

GST_END_TEST;

GST_START_TEST (test_timing_info_invalid_framerate)
{
  GstHarness *h;
  GstBuffer *buf;
  GstEvent *event;
  gboolean seen = FALSE;
  /* Sequence header OBU with timing_info time_scale = 0x80000000 and
   * num_units_in_display_tick = 0xFFFFFFFF. The values do not fit the
   * gint framerate and used to trigger undefined behaviour
   * (G_MININT % -1 inside gst_util_greatest_common_divisor) when
   * reducing the fraction. */
  const guint8 stream[] = {
    0x0a, 0x11, 0x07, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x05, 0x33, 0xff, 0x00, 0x68, 0x02
  };

  h = gst_harness_new_parse ("av1parse");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  buf = gst_buffer_new_and_alloc (sizeof (stream));
  gst_buffer_fill (buf, 0, stream, sizeof (stream));
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  gst_harness_push_event (h, gst_event_new_eos ());

  while ((event = gst_harness_pull_event (h)) != NULL) {
    GstCaps *caps;
    const GstStructure *s;
    gint n = 0, d = 1;

    if (GST_EVENT_TYPE (event) != GST_EVENT_CAPS) {
      gst_event_unref (event);
      continue;
    }

    seen = TRUE;
    gst_event_parse_caps (event, &caps);
    s = gst_caps_get_structure (caps, 0);
    if (gst_structure_get_fraction (s, "framerate", &n, &d))
      fail_unless (n >= 0 && d > 0);

    gst_event_unref (event);
    break;
  }
  fail_unless (seen);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_timing_info_framerate)
{
  /* Two sequence headers, both yielding a 30/1 framerate: the first
   * with time_scale = 30000 and num_units_in_display_tick = 1000, the
   * second with time_scale = 4294967280 and num_units_in_display_tick =
   * 143165576, whose unreduced numerator does not fit a gint but whose
   * fraction reduces to 30/1. */
  static const guint8 stream[] = {
    0x0a, 0x11, 0x04, 0x00, 0x00, 0x0f, 0xa0, 0x00, 0x01, 0xd4, 0xc3, 0x00,
    0x00, 0x05, 0x33, 0xff, 0x00, 0x68, 0x02
  };
  static const guint8 big_stream[] = {
    0x0a, 0x11, 0x04, 0x22, 0x22, 0x22, 0x23, 0xff, 0xff, 0xff, 0xc3, 0x00,
    0x00, 0x05, 0x33, 0xff, 0x00, 0x68, 0x02
  };
  const guint8 *streams[] = { stream, big_stream };
  gsize stream_sizes[] = { sizeof (stream), sizeof (big_stream) };
  GstHarness *h;
  GstBuffer *buf;
  GstEvent *event;
  gboolean seen;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (streams); i++) {
    seen = FALSE;

    h = gst_harness_new_parse ("av1parse");
    gst_harness_set_src_caps_str (h, "video/x-av1");

    buf = gst_buffer_new_and_alloc (stream_sizes[i]);
    gst_buffer_fill (buf, 0, streams[i], stream_sizes[i]);
    fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

    gst_harness_push_event (h, gst_event_new_eos ());

    while ((event = gst_harness_pull_event (h)) != NULL) {
      GstCaps *caps;
      const GstStructure *s;
      gint n = 0, d = 1;

      if (GST_EVENT_TYPE (event) != GST_EVENT_CAPS) {
        gst_event_unref (event);
        continue;
      }

      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      if (gst_structure_get_fraction (s, "framerate", &n, &d)) {
        fail_unless_equals_int (n, 30);
        fail_unless_equals_int (d, 1);
        seen = TRUE;
      }

      gst_event_unref (event);
      break;
    }

    fail_unless (seen);

    gst_harness_teardown (h);
  }
}

GST_END_TEST;

GST_START_TEST (test_uvlc_invalid)
{
  GstHarness *h;
  GstBuffer *buf;
  /* Sequence header OBU with timing information whose uvlc-encoded
   * num_ticks_per_picture_minus_1 used to trigger a 1 << 31 shift,
   * which is undefined behaviour for a signed literal. */
  const guint8 stream[] = {
    0x0a, 0x11, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x3d, 0x01, 0xa2, 0x00,
    0x00, 0x00, 0x02, 0x33, 0xff, 0x00, 0x68, 0x02
  };

  h = gst_harness_new_parse ("av1parse");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  buf = gst_buffer_new_and_alloc (sizeof (stream));
  gst_buffer_fill (buf, 0, stream, sizeof (stream));
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  gst_harness_push_event (h, gst_event_new_eos ());

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_sequence_header_truncated)
{
  GstHarness *h;
  GstBuffer *buf;
  /* Sequence header OBU declaring frame_width_bits_minus_1 = 15 and
   * frame_height_bits_minus_1 = 13 while the OBU data ends before both
   * frame sizes are encoded. This used to read past the end of the OBU
   * data. */
  const guint8 stream[] = {
    0x0a, 0x07, 0x00, 0x07, 0x00, 0xfd, 0xfd, 0xff, 0x02
  };

  h = gst_harness_new_parse ("av1parse");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  buf = gst_buffer_new_and_alloc (sizeof (stream));
  gst_buffer_fill (buf, 0, stream, sizeof (stream));
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  gst_harness_push_event (h, gst_event_new_eos ());

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_tile_group_extent)
{
  GstHarness *h;
  GstBuffer *buf;
  /* Frame OBU with a tile group whose tg_end (255) exceeds the tile
   * count (192). The tile rows derived from tg_end used to index past
   * the parser mi_row_starts table. */
  const guint8 bytestream[] = {
    0x0a, 0x0b, 0x00, 0x00, 0x00, 0x3c, 0x5c, 0xff, 0xff, 0xc0, 0x1a, 0x00,
    0x80, 0x32, 0x97, 0x04, 0x10, 0x77, 0xe0, 0x00, 0x00, 0x00, 0x80, 0x7f,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  h = gst_harness_new_parse ("av1parse");
  gst_harness_set_src_caps_str (h, "video/x-av1");

  buf = gst_buffer_new_and_alloc (sizeof (bytestream));
  gst_buffer_fill (buf, 0, bytestream, sizeof (bytestream));
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  gst_harness_push_event (h, gst_event_new_eos ());

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
av1parse_suite (void)
{
  Suite *s;
  TCase *tc_chain;

  s = suite_create ("av1parse");
  tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_byte_to_frame);
  tcase_add_test (tc_chain, test_byte_to_annexb);
  tcase_add_test (tc_chain, test_annexb_to_frame);
  tcase_add_test (tc_chain, test_annexb_to_obu);
  tcase_add_test (tc_chain, test_byte_to_obu);

  tcase_add_test (tc_chain, test_timing_info_invalid_framerate);
  tcase_add_test (tc_chain, test_timing_info_framerate);
  tcase_add_test (tc_chain, test_uvlc_invalid);
  tcase_add_test (tc_chain, test_sequence_header_truncated);
  tcase_add_test (tc_chain, test_tile_group_extent);

  return s;
}

GST_CHECK_MAIN (av1parse);
