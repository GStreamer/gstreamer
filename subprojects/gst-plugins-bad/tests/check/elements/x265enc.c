/* GStreamer
 *
 * Copyright (c) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2010 David Schleef <ds@schleef.org>
 * Copyright (c) 2014 Thijs Vermeir <thijs.vermeir@barco.com>
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
#include <gst/codecparsers/gsth265parser.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "width = (int) [16, MAX], "
        "height = (int) [16, MAX], " "framerate = (fraction) [0, MAX]"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) I420, "
        "width = (int) [16, MAX], "
        "height = (int) [16, MAX], " "framerate = (fraction) [0, MAX]"));

static GstPad *sinkpad, *srcpad;

static GstElement *
setup_x265enc (const gchar * src_caps_str, const gchar * first_property_name,
    ...)
{
  GstElement *x265enc;
  GstCaps *srccaps = NULL;
  GstBus *bus;

  if (src_caps_str) {
    srccaps = gst_caps_from_string (src_caps_str);
    fail_unless (srccaps != NULL);
  }

  x265enc = gst_check_setup_element ("x265enc");
  fail_unless (x265enc != NULL);
  if (first_property_name) {
    va_list var_args;

    va_start (var_args, first_property_name);
    g_object_set_valist (G_OBJECT (x265enc), first_property_name, var_args);
    va_end (var_args);
  }
  srcpad = gst_check_setup_src_pad (x265enc, &srctemplate);
  sinkpad = gst_check_setup_sink_pad (x265enc, &sinktemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_pad_set_active (sinkpad, TRUE);

  gst_check_setup_events (srcpad, x265enc, srccaps, GST_FORMAT_TIME);

  bus = gst_bus_new ();
  gst_element_set_bus (x265enc, bus);

  fail_unless (gst_element_set_state (x265enc,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE,
      "could not set to playing");

  if (srccaps)
    gst_caps_unref (srccaps);

  buffers = NULL;
  return x265enc;
}

static void
cleanup_x265enc (GstElement * x265enc)
{
  GstBus *bus;

  /* Free parsed buffers */
  gst_check_drop_buffers ();

  bus = GST_ELEMENT_BUS (x265enc);
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);
  gst_check_teardown_src_pad (x265enc);
  gst_check_teardown_sink_pad (x265enc);
  gst_check_teardown_element (x265enc);
}

GST_START_TEST (test_encode_simple)
{
  GstElement *x265enc;
  GstBuffer *buffer;
  gint i;
  GList *l;
  GstCaps *outcaps, *sinkcaps;
  GstSegment seg;

  x265enc =
      setup_x265enc
      ("video/x-raw,format=(string)I420,width=(int)320,height=(int)240,framerate=(fraction)25/1",
      NULL);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_allocate (NULL, 320 * 240 + 2 * 160 * 120, NULL);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (i = 0; i < 10; i++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

  /* All buffers must be there now */
  fail_unless_equals_int (g_list_length (buffers), 10);

  outcaps =
      gst_caps_from_string
      ("video/x-h265,width=(int)320,height=(int)240,framerate=(fraction)25/1");

  for (l = buffers, i = 0; l; l = l->next, i++) {
    buffer = l->data;

    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    sinkcaps = gst_pad_get_current_caps (sinkpad);
    fail_unless (gst_caps_can_intersect (sinkcaps, outcaps));
    gst_caps_unref (sinkcaps);
  }

  gst_caps_unref (outcaps);

  cleanup_x265enc (x265enc);
}

GST_END_TEST;

GST_START_TEST (test_tiny_picture)
{
  GstElement *x265enc;
  GstBuffer *buffer;
  gint i;
  GList *l;
  GstCaps *outcaps, *sinkcaps;
  GstSegment seg;

  x265enc =
      setup_x265enc
      ("video/x-raw,format=(string)I420,width=(int)16,height=(int)16,framerate=(fraction)25/1",
      NULL);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_allocate (NULL, 16 * 16 + 2 * 8 * 8, NULL);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (i = 0; i < 10; i++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (i, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

  /* All buffers must be there now */
  fail_unless_equals_int (g_list_length (buffers), 10);

  outcaps =
      gst_caps_from_string
      ("video/x-h265,width=(int)16,height=(int)16,framerate=(fraction)25/1");

  for (l = buffers, i = 0; l; l = l->next, i++) {
    buffer = l->data;

    fail_unless_equals_uint64 (GST_BUFFER_DURATION (buffer),
        gst_util_uint64_scale (1, GST_SECOND, 25));

    sinkcaps = gst_pad_get_current_caps (sinkpad);
    fail_unless (gst_caps_can_intersect (sinkcaps, outcaps));
    gst_caps_unref (sinkcaps);
  }

  gst_caps_unref (outcaps);

  cleanup_x265enc (x265enc);
}

GST_END_TEST;

typedef struct
{
  guint num_nals;
  guint num_vps;
  guint num_sps;
  guint num_pps;
  GstH265NalUnitType first_nal_type;
} NalStats;

static NalStats
parse_nal_stats (GstBuffer * buffer)
{
  GstH265Parser *parser;
  GstH265ParserResult res;
  GstH265NalUnit nalu;
  GstMapInfo map;
  guint offset = 0;
  NalStats stats = { 0, };

  parser = gst_h265_parser_new ();
  fail_unless (gst_buffer_map (buffer, &map, GST_MAP_READ));

  do {
    res = gst_h265_parser_identify_nalu (parser, map.data, offset, map.size,
        &nalu);
    fail_unless (res == GST_H265_PARSER_OK
        || res == GST_H265_PARSER_NO_NAL_END);

    if (stats.num_nals == 0)
      stats.first_nal_type = nalu.type;
    stats.num_nals++;

    if (nalu.type == GST_H265_NAL_VPS)
      stats.num_vps++;
    else if (nalu.type == GST_H265_NAL_SPS)
      stats.num_sps++;
    else if (nalu.type == GST_H265_NAL_PPS)
      stats.num_pps++;

    offset = nalu.offset + nalu.size;
  } while (res == GST_H265_PARSER_OK);

  gst_buffer_unmap (buffer, &map);
  gst_h265_parser_free (parser);

  return stats;
}

GST_START_TEST (test_aud_and_repeat_headers)
{
  GstElement *x265enc;
  GstBuffer *buffer;
  NalStats stats;
  gint j;
  GList *l;
  GstSegment seg;

  x265enc =
      setup_x265enc
      ("video/x-raw,format=(string)I420,width=(int)320,height=(int)240,framerate=(fraction)25/1",
      "option-string", "aud=1:repeat-headers=1", NULL);

  gst_segment_init (&seg, GST_FORMAT_TIME);
  seg.stop = gst_util_uint64_scale (10, GST_SECOND, 25);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&seg)));

  buffer = gst_buffer_new_allocate (NULL, 320 * 240 + 2 * 160 * 120, NULL);
  gst_buffer_memset (buffer, 0, 0, -1);

  for (j = 0; j < 10; j++) {
    GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (j, GST_SECOND, 25);
    GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (1, GST_SECOND, 25);
    fail_unless (gst_pad_push (srcpad, gst_buffer_ref (buffer)) == GST_FLOW_OK);
  }

  gst_buffer_unref (buffer);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));

  fail_unless_equals_int (g_list_length (buffers), 10);

  /* When present, the AUD must be the first NAL of each access unit and
   * with repeat-headers there must be exactly one set of parameter sets
   * in the first access unit */
  stats = parse_nal_stats (GST_BUFFER (buffers->data));
  fail_unless (stats.num_nals > 0);
  fail_unless_equals_int (stats.first_nal_type, GST_H265_NAL_AUD);
  fail_unless_equals_int (stats.num_vps, 1);
  fail_unless_equals_int (stats.num_sps, 1);
  fail_unless_equals_int (stats.num_pps, 1);

  for (l = g_list_next (buffers); l; l = l->next) {
    stats = parse_nal_stats (GST_BUFFER (l->data));
    fail_unless (stats.num_nals > 0);
    fail_unless_equals_int (stats.first_nal_type, GST_H265_NAL_AUD);
  }

  cleanup_x265enc (x265enc);
}

GST_END_TEST;

static Suite *
x265enc_suite (void)
{
  Suite *s = suite_create ("x265enc");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_encode_simple);
  tcase_add_test (tc_chain, test_tiny_picture);
  tcase_add_test (tc_chain, test_aud_and_repeat_headers);

  return s;
}

GST_CHECK_MAIN (x265enc);
