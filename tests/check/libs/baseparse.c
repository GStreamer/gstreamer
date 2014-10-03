/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <ts.santos@sisa.samsung.com>
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
#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/base/gstbaseparse.h>

static GstPad *mysrcpad, *mysinkpad;
static GstElement *parsetest;

#define TEST_VIDEO_WIDTH 640
#define TEST_VIDEO_HEIGHT 480
#define TEST_VIDEO_FPS_N 30
#define TEST_VIDEO_FPS_D 1

#define GST_PARSER_TESTER_TYPE gst_parser_tester_get_type()
static GType gst_parser_tester_get_type (void);

typedef struct _GstParserTester GstParserTester;
typedef struct _GstParserTesterClass GstParserTesterClass;

struct _GstParserTester
{
  GstBaseParse parent;
};

struct _GstParserTesterClass
{
  GstBaseParseClass parent_class;
};

G_DEFINE_TYPE (GstParserTester, gst_parser_tester, GST_TYPE_BASE_PARSE);

static gboolean
gst_parser_tester_start (GstBaseParse * parse)
{
  return TRUE;
}

static gboolean
gst_parser_tester_stop (GstBaseParse * parse)
{
  return TRUE;
}

static gboolean
gst_parser_tester_set_sink_caps (GstBaseParse * parse, GstCaps * caps)
{
  gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
  return TRUE;
}

static GstFlowReturn
gst_parser_tester_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  while (frame->buffer && gst_buffer_get_size (frame->buffer) >= 8) {
    GST_BUFFER_DURATION (frame->buffer) =
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);
    gst_base_parse_finish_frame (parse, frame, 8);
  }
  return GST_FLOW_OK;
}

static void
gst_parser_tester_class_init (GstParserTesterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *baseparse_class = GST_BASE_PARSE_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom"));

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom"));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  gst_element_class_set_metadata (element_class,
      "ParserTester", "Parser/Video", "yep", "me");

  baseparse_class->start = gst_parser_tester_start;
  baseparse_class->stop = gst_parser_tester_stop;
  baseparse_class->handle_frame = gst_parser_tester_handle_frame;
  baseparse_class->set_sink_caps = gst_parser_tester_set_sink_caps;
}

static void
gst_parser_tester_init (GstParserTester * tester)
{
}

static void
setup_parsertester (void)
{
  static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom")
      );
  static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom")
      );

  parsetest = g_object_new (GST_PARSER_TESTER_TYPE, NULL);
  mysrcpad = gst_check_setup_src_pad (parsetest, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (parsetest, &sinktemplate);
}

static void
cleanup_parsertest (void)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (parsetest);
  gst_check_teardown_sink_pad (parsetest);
  gst_check_teardown_element (parsetest);
}

static GstBuffer *
create_test_buffer (guint64 num)
{
  GstBuffer *buffer;
  guint64 *data = g_malloc (sizeof (guint64));

  *data = num;

  buffer = gst_buffer_new_wrapped (data, sizeof (guint64));

  GST_BUFFER_PTS (buffer) =
      gst_util_uint64_scale_round (num, GST_SECOND * TEST_VIDEO_FPS_D,
      TEST_VIDEO_FPS_N);
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
      TEST_VIDEO_FPS_N);

  return buffer;
}

static void
send_startup_events (void)
{
  GstCaps *caps;

  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("randomvalue")));

  /* push caps */
  caps =
      gst_caps_new_simple ("video/x-test-custom", "width", G_TYPE_INT,
      TEST_VIDEO_WIDTH, "height", G_TYPE_INT, TEST_VIDEO_HEIGHT, "framerate",
      GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D, NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
}

static void
run_parser_playback_test (GList * input, gint expected_output, gdouble rate)
{
  GstBuffer *buffer;
  guint64 i;
  GList *iter;
  GstSegment segment;

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (parsetest, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = rate;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  for (iter = input; iter; iter = g_list_next (iter)) {
    buffer = iter->data;

    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }
  g_list_free (input);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that all buffers were received by our source pad */
  fail_unless (g_list_length (buffers) == expected_output);
  i = 0;
  for (iter = buffers; iter; iter = g_list_next (iter)) {
    GstMapInfo map;
    guint64 num;

    buffer = iter->data;

    gst_buffer_map (buffer, &map, GST_MAP_READ);

    num = *(guint64 *) map.data;
    fail_unless (i == num);
    fail_unless (GST_BUFFER_PTS (buffer) == gst_util_uint64_scale_round (i,
            GST_SECOND * TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N));
    fail_unless (GST_BUFFER_DURATION (buffer) ==
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
            TEST_VIDEO_FPS_N));

    gst_buffer_unmap (buffer, &map);
    i++;
  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_parsertest ();
}


GST_START_TEST (parser_playback)
{
  GList *input = NULL;
  gint i;
  GstBuffer *buffer;

  setup_parsertester ();

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < 3; i++) {
    buffer = create_test_buffer (i);
    input = g_list_append (input, buffer);
  }

  run_parser_playback_test (input, 3, 1.0);
}

GST_END_TEST;


/* Check https://bugzilla.gnome.org/show_bug.cgi?id=721941 */
GST_START_TEST (parser_reverse_playback_on_passthrough)
{
  GList *input = NULL;
  gint i;
  GstBuffer *buffer;

  setup_parsertester ();

  gst_base_parse_set_passthrough (GST_BASE_PARSE (parsetest), TRUE);

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < 6; i++) {
    buffer = create_test_buffer (i);
    if (i > 0)
      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
    input = g_list_append (input, buffer);
  }
  GST_BUFFER_FLAG_SET (g_list_nth (input, 3)->data, GST_BUFFER_FLAG_DISCONT);

  run_parser_playback_test (input, 6, -1.0);
}

GST_END_TEST;


static Suite *
gst_baseparse_suite (void)
{
  Suite *s = suite_create ("GstBaseParse");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, parser_playback);
  tcase_add_test (tc, parser_reverse_playback_on_passthrough);

  return s;
}

GST_CHECK_MAIN (gst_baseparse);
