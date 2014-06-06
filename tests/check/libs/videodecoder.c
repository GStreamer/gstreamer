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
#include <gst/video/video.h>
#include <gst/app/app.h>

static GstPad *mysrcpad, *mysinkpad;
static GstElement *dec;
static GList *events = NULL;

#define TEST_VIDEO_WIDTH 640
#define TEST_VIDEO_HEIGHT 480
#define TEST_VIDEO_FPS_N 30
#define TEST_VIDEO_FPS_D 1

#define GST_VIDEO_DECODER_TESTER_TYPE gst_video_decoder_tester_get_type()
static GType gst_video_decoder_tester_get_type (void);

typedef struct _GstVideoDecoderTester GstVideoDecoderTester;
typedef struct _GstVideoDecoderTesterClass GstVideoDecoderTesterClass;

struct _GstVideoDecoderTester
{
  GstVideoDecoder parent;

  guint64 last_buf_num;
  guint64 last_kf_num;
};

struct _GstVideoDecoderTesterClass
{
  GstVideoDecoderClass parent_class;
};

G_DEFINE_TYPE (GstVideoDecoderTester, gst_video_decoder_tester,
    GST_TYPE_VIDEO_DECODER);

static gboolean
gst_video_decoder_tester_start (GstVideoDecoder * dec)
{
  GstVideoDecoderTester *dectester = (GstVideoDecoderTester *) dec;

  dectester->last_buf_num = -1;
  dectester->last_kf_num = -1;

  return TRUE;
}

static gboolean
gst_video_decoder_tester_stop (GstVideoDecoder * dec)
{
  return TRUE;
}

static gboolean
gst_video_decoder_tester_flush (GstVideoDecoder * dec)
{
  GstVideoDecoderTester *dectester = (GstVideoDecoderTester *) dec;

  dectester->last_buf_num = -1;
  dectester->last_kf_num = -1;

  return TRUE;
}

static gboolean
gst_video_decoder_tester_set_format (GstVideoDecoder * dec,
    GstVideoCodecState * state)
{
  GstVideoCodecState *res = gst_video_decoder_set_output_state (dec,
      GST_VIDEO_FORMAT_GRAY8, TEST_VIDEO_WIDTH, TEST_VIDEO_HEIGHT, NULL);

  gst_video_codec_state_unref (res);
  return TRUE;
}

static GstFlowReturn
gst_video_decoder_tester_handle_frame (GstVideoDecoder * dec,
    GstVideoCodecFrame * frame)
{
  GstVideoDecoderTester *dectester = (GstVideoDecoderTester *) dec;
  guint64 input_num;
  guint8 *data;
  gint size;
  GstMapInfo map;

  gst_buffer_map (frame->input_buffer, &map, GST_MAP_READ);

  input_num = *((guint64 *) map.data);

  if ((input_num == dectester->last_buf_num + 1
          && dectester->last_buf_num != -1)
      || !GST_BUFFER_FLAG_IS_SET (frame->input_buffer,
          GST_BUFFER_FLAG_DELTA_UNIT)) {

    /* the output is gray8 */
    size = TEST_VIDEO_WIDTH * TEST_VIDEO_HEIGHT;
    data = g_malloc0 (size);

    memcpy (data, map.data, sizeof (guint64));

    frame->output_buffer = gst_buffer_new_wrapped (data, size);
    frame->pts = GST_BUFFER_PTS (frame->input_buffer);
    frame->duration = GST_BUFFER_DURATION (frame->input_buffer);
    dectester->last_buf_num = input_num;
    if (!GST_BUFFER_FLAG_IS_SET (frame->input_buffer,
            GST_BUFFER_FLAG_DELTA_UNIT))
      dectester->last_kf_num = input_num;
  }

  gst_buffer_unmap (frame->input_buffer, &map);

  if (frame->output_buffer)
    return gst_video_decoder_finish_frame (dec, frame);
  gst_video_codec_frame_unref (frame);
  return GST_FLOW_OK;
}

static void
gst_video_decoder_tester_class_init (GstVideoDecoderTesterClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *audiosink_class = GST_VIDEO_DECODER_CLASS (klass);

  static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom"));

  static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw"));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));

  gst_element_class_set_metadata (element_class,
      "VideoDecoderTester", "Decoder/Video", "yep", "me");

  audiosink_class->start = gst_video_decoder_tester_start;
  audiosink_class->stop = gst_video_decoder_tester_stop;
  audiosink_class->flush = gst_video_decoder_tester_flush;
  audiosink_class->handle_frame = gst_video_decoder_tester_handle_frame;
  audiosink_class->set_format = gst_video_decoder_tester_set_format;
}

static void
gst_video_decoder_tester_init (GstVideoDecoderTester * tester)
{
}

static gboolean
_mysinkpad_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  events = g_list_append (events, event);
  return TRUE;
}

static void
setup_videodecodertester (void)
{
  static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-raw")
      );
  static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/x-test-custom")
      );

  dec = g_object_new (GST_VIDEO_DECODER_TESTER_TYPE, NULL);
  mysrcpad = gst_check_setup_src_pad (dec, &srctemplate);
  mysinkpad = gst_check_setup_sink_pad (dec, &sinktemplate);

  gst_pad_set_event_function (mysinkpad, _mysinkpad_event);
}

static void
cleanup_videodecodertest (void)
{
  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
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

#define NUM_BUFFERS 1000
GST_START_TEST (videodecoder_playback)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GList *iter;

  setup_videodecodertester ();

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

  cleanup_videodecodertest ();
}

GST_END_TEST;


GST_START_TEST (videodecoder_playback_with_events)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GList *iter;
  GList *events_iter;

  setup_videodecodertester ();

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

  /* check that all buffers were received by our source pad */
  iter = buffers;
  for (i = 0; i < NUM_BUFFERS; i++) {
    if (i % 10 == 0) {
      guint tag_v;
      GstEvent *tag_event = events_iter->data;
      GstTagList *taglist = NULL;

      gst_event_parse_tag (tag_event, &taglist);

      fail_unless (gst_tag_list_get_uint (taglist, GST_TAG_TRACK_NUMBER,
              &tag_v));
      fail_unless (tag_v == i);

      events_iter = g_list_next (events_iter);
    } else {
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
      iter = g_list_next (iter);
    }
  }
  fail_unless (iter == NULL);

  /* check that EOS was received */
  {
    GstEvent *eos = events_iter->data;

    fail_unless (GST_EVENT_TYPE (eos) == GST_EVENT_EOS);
    events_iter = g_list_next (events_iter);
  }

  fail_unless (events_iter == NULL);

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_videodecodertest ();
}

GST_END_TEST;

GST_START_TEST (videodecoder_flush_events)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GList *events_iter;

  setup_videodecodertester ();

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

  cleanup_videodecodertest ();
}

GST_END_TEST;


/* Check https://bugzilla.gnome.org/show_bug.cgi?id=721835 */
GST_START_TEST (videodecoder_playback_first_frames_not_decoded)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i = 0;

  setup_videodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push a buffer, to have the segment attached to it.
   * unfortunatelly this buffer can't be decoded as it isn't a keyframe */
  buffer = create_test_buffer (i++);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  /* now be evil and ask this frame to be released
   * this frame has the segment event attached to it, and the
   * segment shouldn't disappear with it */
  {
    GList *l, *ol;

    ol = l = gst_video_decoder_get_frames (GST_VIDEO_DECODER (dec));
    fail_unless (g_list_length (l) == 1);
    while (l) {
      GstVideoCodecFrame *tmp = l->data;

      gst_video_decoder_release_frame (GST_VIDEO_DECODER (dec), tmp);

      l = g_list_next (l);
    }
    g_list_free (ol);
  }

  buffer = create_test_buffer (i++);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless (g_list_length (buffers) == 1);

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_videodecodertest ();
}

GST_END_TEST;

GST_START_TEST (videodecoder_buffer_after_segment)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GstClockTime pos;
  GList *iter;

  setup_videodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers until we fill our segment */
  i = 0;
  pos = 0;
  while (pos < GST_SECOND) {
    buffer = create_test_buffer (i++);

    pos = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
  }

  /* pushing the next buffer should result in EOS */
  buffer = create_test_buffer (i);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_EOS);

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that all buffers were received by our source pad */
  fail_unless (g_list_length (buffers) == i);
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

  cleanup_videodecodertest ();
}

GST_END_TEST;


GST_START_TEST (videodecoder_backwards_playback)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GList *iter;

  setup_videodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment with -1 rate */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.stop = (NUM_BUFFERS + 1) * gst_util_uint64_scale_round (GST_SECOND,
      TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  i = NUM_BUFFERS;
  while (i > 0) {
    gint target = i;
    gint j;

    /* push groups of 10 buffers
     * every number that is divisible by 10 is set as a discont,
     * if it is divisible by 20 it is also a keyframe
     *
     * The logic here is that hte current i is the target, and then
     * it pushes buffers from 'target - 10' up to target.
     */
    for (j = MAX (target - 10, 0); j < target; j++) {
      GstBuffer *buffer = create_test_buffer (j);

      if (j % 10 == 0)
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      if (j % 20 != 0)
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

      fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
      i--;
    }
  }

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* check that all buffers were received by our source pad */
  fail_unless (g_list_length (buffers) == NUM_BUFFERS);
  i = NUM_BUFFERS - 1;
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
    i--;
  }

  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  cleanup_videodecodertest ();
}

GST_END_TEST;


GST_START_TEST (videodecoder_backwards_buffer_after_segment)
{
  GstSegment segment;
  GstBuffer *buffer;
  guint64 i;
  GstClockTime pos;

  setup_videodecodertester ();

  gst_pad_set_active (mysrcpad, TRUE);
  gst_element_set_state (dec, GST_STATE_PLAYING);
  gst_pad_set_active (mysinkpad, TRUE);

  send_startup_events ();

  /* push a new segment with -1 rate */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.rate = -1.0;
  segment.start = GST_SECOND;
  segment.stop = (NUM_BUFFERS + 1) * gst_util_uint64_scale_round (GST_SECOND,
      TEST_VIDEO_FPS_D, TEST_VIDEO_FPS_N);
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_segment (&segment)));

  /* push buffers, the data is actually a number so we can track them */
  i = NUM_BUFFERS;
  pos = segment.stop;
  while (pos >= GST_SECOND) {
    gint target = i;
    gint j;

    g_assert (i > 0);

    /* push groups of 10 buffers
     * every number that is divisible by 10 is set as a discont,
     * if it is divisible by 20 it is also a keyframe
     *
     * The logic here is that hte current i is the target, and then
     * it pushes buffers from 'target - 10' up to target.
     */
    for (j = MAX (target - 10, 0); j < target; j++) {
      buffer = create_test_buffer (j);

      pos = MIN (GST_BUFFER_TIMESTAMP (buffer), pos);
      if (j % 10 == 0)
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
      if (j % 20 != 0)
        GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

      fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_OK);
      i--;
    }
  }

  /* push a discont buffer so it flushes the decoding */
  buffer = create_test_buffer (i - 10);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  fail_unless (gst_pad_push (mysrcpad, buffer) == GST_FLOW_EOS);

  /* check that the last received buffer doesn't contain a
   * timestamp before the segment */
  buffer = g_list_last (buffers)->data;
  fail_unless (GST_BUFFER_TIMESTAMP (buffer) <= segment.start
      && GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) >
      segment.start);

  /* flush our decoded data queue */
  g_list_free_full (buffers, (GDestroyNotify) gst_buffer_unref);
  buffers = NULL;

  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  fail_unless (buffers == NULL);

  cleanup_videodecodertest ();
}

GST_END_TEST;


static Suite *
gst_videodecoder_suite (void)
{
  Suite *s = suite_create ("GstVideoDecoder");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, videodecoder_playback);
  tcase_add_test (tc, videodecoder_playback_with_events);
  tcase_add_test (tc, videodecoder_playback_first_frames_not_decoded);
  tcase_add_test (tc, videodecoder_buffer_after_segment);

  tcase_add_test (tc, videodecoder_backwards_playback);
  tcase_add_test (tc, videodecoder_backwards_buffer_after_segment);
  tcase_add_test (tc, videodecoder_flush_events);

  return s;
}

GST_CHECK_MAIN (gst_videodecoder);
