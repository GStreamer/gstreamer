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
static GstBus *bus;
static GMainLoop *loop = NULL;
static gboolean have_eos = FALSE;
static gboolean have_data = FALSE;
static gint buffer_count = 0;
static gboolean caps_set = FALSE;

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
  GstFlowReturn ret = GST_FLOW_OK;

  if (caps_set == FALSE) {
    GstCaps *caps;
    /* push caps */
    caps =
        gst_caps_new_simple ("video/x-test-custom", "width", G_TYPE_INT,
        TEST_VIDEO_WIDTH, "height", G_TYPE_INT, TEST_VIDEO_HEIGHT, "framerate",
        GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D, NULL);
    gst_pad_set_caps (GST_BASE_PARSE_SRC_PAD (parse), caps);
    gst_caps_unref (caps);
    caps_set = TRUE;
  }

  while (frame->buffer && gst_buffer_get_size (frame->buffer) >= 8) {
    GST_BUFFER_DURATION (frame->buffer) =
        gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
        TEST_VIDEO_FPS_N);
    ret = gst_base_parse_finish_frame (parse, frame, 8);
  }
  return ret;
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

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);

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
  bus = gst_bus_new ();
  gst_element_set_bus (parsetest, bus);
}

static void
cleanup_parsertest (void)
{
  /* release the bus first to get rid of all refcounts */
  gst_element_set_bus (parsetest, NULL);
  gst_object_unref (bus);

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
check_no_error_received (void)
{
  GstMessage *msg;

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_ERROR);
  fail_unless (msg == NULL);
  if (msg)
    gst_message_unref (msg);
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

  check_no_error_received ();

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

GST_START_TEST (parser_empty_stream)
{
  setup_parsertester ();

  run_parser_playback_test (NULL, 0, 1.0);
}

GST_END_TEST;

static GstFlowReturn
_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstMapInfo map;
  guint64 num;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  num = *(guint64 *) map.data;

  fail_unless (buffer_count == num);
  fail_unless (GST_BUFFER_PTS (buffer) ==
      gst_util_uint64_scale_round (buffer_count, GST_SECOND * TEST_VIDEO_FPS_D,
          TEST_VIDEO_FPS_N));
  fail_unless (GST_BUFFER_DURATION (buffer) ==
      gst_util_uint64_scale_round (GST_SECOND, TEST_VIDEO_FPS_D,
          TEST_VIDEO_FPS_N));
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);
  buffer_count++;

  have_data = TRUE;
  return GST_FLOW_OK;
}

static gboolean
_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_INFO_OBJECT (pad, "got %s event %p: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (loop) {
        while (!g_main_loop_is_running (loop)) {
          /* nothing */
        };
      }
      have_eos = TRUE;
      if (loop)
        g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  gst_event_unref (event);

  return TRUE;
}

static GstFlowReturn
_src_getrange (GstPad * pad, GstObject * parent, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  gboolean ret = FALSE;
  if (offset >= 80 && have_eos == FALSE) {
    ret = gst_element_seek (parsetest, -1.0, GST_FORMAT_TIME,
        GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, gst_util_uint64_scale_round (5,
            GST_SECOND * TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N));
    fail_unless (ret == TRUE);
    buffer_count = 0;
  }

  *buffer = create_test_buffer (offset / 8);

  return GST_FLOW_OK;
}

static gboolean
_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:{
      gst_query_set_scheduling (query, GST_SCHEDULING_FLAG_SEEKABLE, 1, -1, 0);
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PULL);
      res = TRUE;
      break;
    }
    default:
      GST_DEBUG_OBJECT (pad, "unhandled %s query", GST_QUERY_TYPE_NAME (query));
      break;
  }

  return res;
}

GST_START_TEST (parser_reverse_playback)
{
  have_eos = FALSE;
  have_data = FALSE;
  loop = g_main_loop_new (NULL, FALSE);

  setup_parsertester ();
  gst_pad_set_getrange_function (mysrcpad, _src_getrange);
  gst_pad_set_query_function (mysrcpad, _src_query);
  gst_pad_set_chain_function (mysinkpad, _sink_chain);
  gst_pad_set_event_function (mysinkpad, _sink_event);

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (parsetest, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  g_main_loop_run (loop);
  fail_unless (have_eos == TRUE);
  fail_unless (have_data == TRUE);

  gst_element_set_state (parsetest, GST_STATE_NULL);

  check_no_error_received ();
  cleanup_parsertest ();

  g_main_loop_unref (loop);
  loop = NULL;
}

GST_END_TEST;

static void
baseparse_setup (void)
{
  /* init/reset global state */
  mysrcpad = mysinkpad = NULL;
  parsetest = NULL;
  bus = NULL;
  loop = NULL;
  have_eos = have_data = caps_set = FALSE;
  buffer_count = 0;
}

static void
baseparse_teardown (void)
{
}

static Suite *
gst_baseparse_suite (void)
{
  Suite *s = suite_create ("GstBaseParse");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_checked_fixture (tc, baseparse_setup, baseparse_teardown);
  tcase_add_test (tc, parser_playback);
  tcase_add_test (tc, parser_empty_stream);
  tcase_add_test (tc, parser_reverse_playback_on_passthrough);
  tcase_add_test (tc, parser_reverse_playback);

  return s;
}

GST_CHECK_MAIN (gst_baseparse);
