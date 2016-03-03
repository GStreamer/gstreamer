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
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gst/app/app.h>

static GstPad *mysrcpad, *mysinkpad;
static GstElement *dec;
static GList *events = NULL;

#define TEST_VIDEO_WIDTH 640
#define TEST_VIDEO_HEIGHT 480
#define TEST_VIDEO_FPS_N 30
#define TEST_VIDEO_FPS_D 1

#define GST_VIDEO_ENCODER_TESTER_TYPE gst_video_encoder_tester_get_type()
static GType gst_video_encoder_tester_get_type (void);

typedef struct _GstVideoEncoderTester GstVideoEncoderTester;
typedef struct _GstVideoEncoderTesterClass GstVideoEncoderTesterClass;

struct _GstVideoEncoderTester
{
  GstVideoEncoder parent;

  GstFlowReturn pre_push_result;
};

struct _GstVideoEncoderTesterClass
{
  GstVideoEncoderClass parent_class;
};

G_DEFINE_TYPE (GstVideoEncoderTester, gst_video_encoder_tester,
    GST_TYPE_VIDEO_ENCODER);

static gboolean
gst_video_encoder_tester_start (GstVideoEncoder * dec)
{
  return TRUE;
}

static gboolean
gst_video_encoder_tester_stop (GstVideoEncoder * dec)
{
  return TRUE;
}

static gboolean
gst_video_encoder_tester_set_format (GstVideoEncoder * dec,
    GstVideoCodecState * state)
{
  GstVideoCodecState *res = gst_video_encoder_set_output_state (dec,
      gst_caps_new_simple ("video/x-test-custom", "width", G_TYPE_INT,
          480, "height", G_TYPE_INT, 360, NULL),
      NULL);

  gst_video_codec_state_unref (res);
  return TRUE;
}

static GstFlowReturn
gst_video_encoder_tester_handle_frame (GstVideoEncoder * dec,
    GstVideoCodecFrame * frame)
{
  guint8 *data;
  GstMapInfo map;
  guint64 input_num;

  gst_buffer_map (frame->input_buffer, &map, GST_MAP_READ);
  input_num = *((guint64 *) map.data);
  gst_buffer_unmap (frame->input_buffer, &map);

  data = g_malloc (sizeof (guint64));
  *(guint64 *) data = input_num;

  frame->output_buffer = gst_buffer_new_wrapped (data, sizeof (guint64));
  frame->pts = GST_BUFFER_PTS (frame->input_buffer);
  frame->duration = GST_BUFFER_DURATION (frame->input_buffer);

  return gst_video_encoder_finish_frame (dec, frame);
}

static GstFlowReturn
gst_video_encoder_tester_pre_push (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame)
{
  GstVideoEncoderTester *tester = (GstVideoEncoderTester *) enc;

  return tester->pre_push_result;
}

static void
gst_video_encoder_tester_class_init (GstVideoEncoderTesterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoencoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom"));

  gst_element_class_add_static_pad_template (element_class, &sink_templ);
  gst_element_class_add_static_pad_template (element_class, &src_templ);

  gst_element_class_set_metadata (element_class,
      "VideoEncoderTester", "Encoder/Video", "yep", "me");

  videoencoder_class->start = gst_video_encoder_tester_start;
  videoencoder_class->stop = gst_video_encoder_tester_stop;
  videoencoder_class->handle_frame = gst_video_encoder_tester_handle_frame;
  videoencoder_class->pre_push = gst_video_encoder_tester_pre_push;
  videoencoder_class->set_format = gst_video_encoder_tester_set_format;
}

static void
gst_video_encoder_tester_init (GstVideoEncoderTester * tester)
{
  tester->pre_push_result = GST_FLOW_OK;
}

static gboolean
_mysinkpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  events = g_list_append (events, event);
  return TRUE;
}

static void
setup_videoencodertester (void)
{
  static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom")
      );
  static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw")
      );

  dec = g_object_new (GST_VIDEO_ENCODER_TESTER_TYPE, NULL);
  mysrcpad = gst_check_setup_src_pad (dec, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (dec, &sinktemplate);

  gst_pad_set_event_function (mysinkpad, _mysinkpad_event);
}

static void
cleanup_videoencodertest (void)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);

  gst_element_set_state (dec, GST_STATE_NULL);

  gst_check_teardown_src_pad (dec);
  gst_check_teardown_sink_pad (dec);
  gst_check_teardown_element (dec);

  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;
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

static GstCaps *
create_test_caps (void)
{
  return gst_caps_new_simple ("video/x-raw", "width", G_TYPE_INT,
      TEST_VIDEO_WIDTH, "height", G_TYPE_INT, TEST_VIDEO_HEIGHT, "framerate",
      GST_TYPE_FRACTION, TEST_VIDEO_FPS_N, TEST_VIDEO_FPS_D,
      "format", G_TYPE_STRING, "GRAY8", NULL);
}

static void
send_startup_events (void)
{
  GstCaps *caps;

  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_stream_start ("randomvalue")));

  /* push caps */
  caps = create_test_caps ();
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
}

#define NUM_BUFFERS 100
GST_START_TEST (videoencoder_playback)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GList *iter;

  setup_videoencodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < NUM_BUFFERS; i++) {
    buffer = create_test_buffer (i);

    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that all buffers were received by our source pad */
  fail_unless (g_list_length (buffers) == NUM_BUFFERS);
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

  cleanup_videoencodertest ();
}

GST_END_TEST;

/* make sure tags sent right before eos are pushed */
GST_START_TEST (videoencoder_tags_before_eos)
{
  GstSegment segment;
  GstBuffer *buffer;
  GstTagList *tags;

  setup_videoencodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffer */
  buffer = create_test_buffer (0);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  /* clean received events list */
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a tag event */
  tags = gst_tag_list_new (GST_TAG_COMMENT, "test-comment", NULL);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_tag (tags)));

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that the tag was received */
  {
    GstEvent *tag_event = events->data;
    gchar *str;

    fail_unless (GST_EVENT_TYPE (tag_event) == GST_EVENT_TAG);
    gst_event_parse_tag (tag_event, &tags);
    fail_unless (gst_tag_list_get_string (tags, GST_TAG_COMMENT, &str));
    fail_unless (strcmp (str, "test-comment") == 0);
    g_free (str);
  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  cleanup_videoencodertest ();
}

GST_END_TEST;

/* make sure events sent right before eos are pushed */
GST_START_TEST (videoencoder_events_before_eos)
{
  GstSegment segment;
  GstBuffer *buffer;
  GstMessage *msg;

  setup_videoencodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffer */
  buffer = create_test_buffer (0);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  /* clean received events list */
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  /* push a serialized event */
  msg =
      gst_message_new_element (GST_OBJECT (mysrcpad),
      gst_structure_new_empty ("test"));
  fail_unless (gst_pad_push_event (mysrcpad,
          gst_event_new_sink_message ("sink-test", msg)));
  gst_message_unref (msg);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that the tag was received */
  {
    GstEvent *msg_event = events->data;
    const GstStructure *structure;

    fail_unless (GST_EVENT_TYPE (msg_event) == GST_EVENT_SINK_MESSAGE);
    fail_unless (gst_event_has_name (msg_event, "sink-test"));
    gst_event_parse_sink_message (msg_event, &msg);
    structure = gst_message_get_structure (msg);
    fail_unless (gst_structure_has_name (structure, "test"));
    gst_message_unref (msg);
  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;
  g_list_free_full (events, (GDestroyNotify) gst_event_unref);
  events = NULL;

  cleanup_videoencodertest ();
}

GST_END_TEST;

GST_START_TEST (videoencoder_flush_events)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint i;
  GList *events_iter;

  setup_videoencodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  for (i = 0; i < NUM_BUFFERS; i++) {
    if (i % 10 == 0) {
      GstTagList *tags;

      tags = gst_tag_list_new (GST_TAG_TRACK_NUMBER, i, NULL);
      fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_tag (tags)));
    } else {
      buffer = create_test_buffer (i);

      fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
    }
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  events_iter = events;
  /* make sure the usual events have been received */
  {
    GstEvent *sstart = events_iter->data;
    fail_unless (GST_EVENT_TYPE (sstart) == GST_EVENT_STREAM_START);
    events_iter = g_list_next (events_iter);
  }
  {
    GstEvent *caps_event = events_iter->data;
    fail_unless (GST_EVENT_TYPE (caps_event) == GST_EVENT_CAPS);
    events_iter = g_list_next (events_iter);
  }
  {
    GstEvent *segment_event = events_iter->data;
    fail_unless (GST_EVENT_TYPE (segment_event) == GST_EVENT_SEGMENT);
    events_iter = g_list_next (events_iter);
  }

  /* check that EOS was received */
  fail_unless (GST_PAD_IS_EOS (mysrcpad));
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_flush_start ()));
  fail_unless (GST_PAD_IS_EOS (mysrcpad));

  /* Check that we have tags */
  {
    GstEvent *tags = gst_pad_get_sticky_event (mysrcpad, GST_EVENT_TAG, 0);

    fail_unless (tags != NULL);
    gst_event_unref (tags);
  }

  /* Check that we still have a segment set */
  {
    GstEvent *segment =
        gst_pad_get_sticky_event (mysrcpad, GST_EVENT_SEGMENT, 0);

    fail_unless (segment != NULL);
    gst_event_unref (segment);
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_flush_stop (TRUE)));
  fail_if (GST_PAD_IS_EOS (mysrcpad));

  /* Check that the segment was flushed on FLUSH_STOP */
  {
    GstEvent *segment =
        gst_pad_get_sticky_event (mysrcpad, GST_EVENT_SEGMENT, 0);

    fail_unless (segment == NULL);
  }

  /* Check the tags were not lost on FLUSH_STOP */
  {
    GstEvent *tags = gst_pad_get_sticky_event (mysrcpad, GST_EVENT_TAG, 0);

    fail_unless (tags != NULL);
    gst_event_unref (tags);

  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_videoencodertest ();
}

GST_END_TEST;

/* When pre_push fails the correct GstFlowReturn should be returned and there
 * should be no leaks */
GST_START_TEST (videoencoder_pre_push_fails)
{
  GstVideoEncoderTester *tester;
  GstHarness *h;

  tester = g_object_new (GST_VIDEO_ENCODER_TESTER_TYPE, NULL);
  tester->pre_push_result = GST_FLOW_ERROR;

  h = gst_harness_new_with_element (GST_ELEMENT (tester), "sink", "src");
  gst_harness_set_src_caps (h, create_test_caps ());

  fail_unless_equals_int (gst_harness_push (h, create_test_buffer (0)),
      GST_FLOW_ERROR);

  gst_harness_teardown (h);
  gst_object_unref (tester);
}

GST_END_TEST;

static Suite *
gst_videoencoder_suite (void)
{
  Suite *s = suite_create ("GstVideoEncoder");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, videoencoder_playback);

  tcase_add_test (tc, videoencoder_tags_before_eos);
  tcase_add_test (tc, videoencoder_events_before_eos);
  tcase_add_test (tc, videoencoder_flush_events);
  tcase_add_test (tc, videoencoder_pre_push_fails);

  return s;
}

GST_CHECK_MAIN (gst_videoencoder);
