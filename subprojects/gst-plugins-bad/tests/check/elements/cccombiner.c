/* GStreamer
 *
 * Copyright (C) 2018 Sebastian Dröge <sebastian@centricular.com>
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

#include <string.h>

static GstStaticCaps foo_bar_caps = GST_STATIC_CAPS ("foo/bar");
static GstStaticCaps foo_bar_caps_0fps =
GST_STATIC_CAPS ("foo/bar,framerate=0/1");
static GstStaticCaps foo_bar_caps_25fps =
GST_STATIC_CAPS ("foo/bar,framerate=25/1");
static GstStaticCaps foo_bar_caps_60fps =
GST_STATIC_CAPS ("foo/bar,framerate=60/1");
static GstStaticCaps cea708_cc_data_caps =
GST_STATIC_CAPS ("closedcaption/x-cea-708,format=(string) cc_data");

GST_START_TEST (no_captions)
{
  GstHarness *h;
  GstBuffer *buf, *outbuf;
  GstCaps *caps;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");

  gst_harness_set_src_caps_str (h, foo_bar_caps.string);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (buf));

  fail_unless (outbuf != NULL);
  fail_unless (outbuf == buf);

  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_can_intersect (caps,
          gst_static_caps_get (&foo_bar_caps)));
  gst_caps_unref (caps);

  gst_buffer_unref (buf);
  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (no_captions_no_duration)
{
  GstHarness *h;
  GstBuffer *buf1, *buf2, *outbuf;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");

  gst_harness_set_src_caps_str (h, foo_bar_caps_0fps.string);

  /* When sending in frames without durations, we lag one frame */

  buf1 = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf1) = 0;
  gst_harness_push (h, gst_buffer_ref (buf1));

  buf2 = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf2) = 10;
  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (buf2));
  fail_unless (outbuf != NULL);
  fail_unless_equals_pointer (outbuf, buf1);
  gst_buffer_unref (buf1);
  gst_buffer_unref (outbuf);

  buf1 = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf1) = 15;
  outbuf = gst_harness_push_and_pull (h, gst_buffer_ref (buf1));
  fail_unless (outbuf != NULL);
  fail_unless_equals_pointer (outbuf, buf2);
  gst_buffer_unref (buf2);
  gst_buffer_unref (outbuf);

  gst_harness_push_event (h, gst_event_new_eos ());
  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);
  fail_unless_equals_pointer (outbuf, buf1);
  gst_buffer_unref (buf1);
  gst_buffer_unref (outbuf);

  gst_harness_teardown (h);
}

GST_END_TEST;

GstBuffer *expected_video_buffer = NULL;
GstBuffer *expected_caption_buffer = NULL;

static void
samples_selected_cb (GstAggregator * agg, GstSegment * segment,
    GstClockTime pts, GstClockTime dts, GstClockTime duration,
    GstStructure * info, gpointer user_data)
{
  GstBufferList *buflist;
  GstPad *caption_pad =
      gst_element_get_static_pad (GST_ELEMENT (agg), "caption");
  GstPad *video_pad = gst_element_get_static_pad (GST_ELEMENT (agg), "sink");
  GstSample *video_sample =
      gst_aggregator_peek_next_sample (agg, GST_AGGREGATOR_PAD (video_pad));
  GstSample *captions_sample =
      gst_aggregator_peek_next_sample (agg, GST_AGGREGATOR_PAD (caption_pad));

  fail_unless (video_sample != NULL);
  fail_unless (captions_sample != NULL);

  fail_unless (gst_sample_get_buffer (video_sample) == expected_video_buffer);
  gst_sample_unref (video_sample);

  buflist = gst_sample_get_buffer_list (captions_sample);
  fail_unless_equals_int (gst_buffer_list_length (buflist), 1);
  gst_sample_unref (captions_sample);

  gst_object_unref (caption_pad);
  gst_object_unref (video_pad);
}

GST_START_TEST (captions_and_eos)
{
  GstHarness *h, *h2;
  GstBuffer *buf, *outbuf;
  GstPad *caption_pad;
  GstCaps *caps;
  GstVideoCaptionMeta *meta;
  GstBuffer *second_video_buf, *second_caption_buf;
  const guint8 cc_data[3] = { 0xfc, 0x20, 0x20 };

  GstElement *element = gst_element_factory_make ("cccombiner", NULL);
  g_assert_nonnull (element);
  /* these must be set before it changes the state */
  g_object_set (element, "schedule", FALSE, "output-padding", FALSE, NULL);

  h = gst_harness_new_with_element (element, "sink", "src");
  gst_object_unref (element);
  h2 = gst_harness_new_with_element (h->element, NULL, NULL);
  caption_pad = gst_element_request_pad_simple (h->element, "caption");
  gst_harness_add_element_sink_pad (h2, caption_pad);
  gst_object_unref (caption_pad);

  g_object_set (h->element, "emit-signals", TRUE, NULL);
  g_signal_connect (h->element, "samples-selected",
      G_CALLBACK (samples_selected_cb), NULL);

  gst_harness_set_src_caps_str (h, foo_bar_caps.string);
  gst_harness_set_src_caps_str (h2, cea708_cc_data_caps.string);

  /* Push a buffer and caption buffer */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  expected_video_buffer = buf;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (3);
  gst_buffer_fill (buf, 0, cc_data, 3);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  expected_caption_buffer = buf;
  gst_harness_push (h2, buf);

  /* And another one: the first video buffer should be retrievable
   * after the second caption buffer is pushed */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  second_video_buf = buf;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (3);
  gst_buffer_fill (buf, 0, cc_data, 3);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  second_caption_buf = buf;
  gst_harness_push (h2, buf);

  /* Pull the first output buffer */
  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  expected_video_buffer = second_video_buf;
  expected_caption_buffer = second_caption_buf;

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 3);

  gst_buffer_unref (outbuf);

  /* Push EOS on both pads get the second output buffer, we otherwise wait
   * in case there are further captions for the current video buffer */
  gst_harness_push_event (h, gst_event_new_eos ());
  gst_harness_push_event (h2, gst_event_new_eos ());

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 3);

  gst_buffer_unref (outbuf);

  /* Caps should be equal to input caps */
  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_can_intersect (caps,
          gst_static_caps_get (&foo_bar_caps)));
  gst_caps_unref (caps);

  gst_harness_teardown (h);
  gst_harness_teardown (h2);
}

GST_END_TEST;

GST_START_TEST (captions_no_output_padding_60fps_608_field1_only)
{
  GstHarness *h, *h2;
  GstBuffer *outbuf;
  GstPad *caption_pad;
  GstCaps *caps;
  GstVideoCaptionMeta *meta;
  const guint8 cc_data[3] = { 0xfc, 0x20, 0x20 };
  int i = 0;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");
  gst_util_set_object_arg ((GObject *) h->element, "cea608-padding-strategy",
      "0");
  h2 = gst_harness_new_with_element (h->element, NULL, NULL);
  caption_pad = gst_element_request_pad_simple (h->element, "caption");
  gst_harness_add_element_sink_pad (h2, caption_pad);
  gst_object_unref (caption_pad);

  g_object_set (h->element, "emit-signals", TRUE, NULL);
  g_signal_connect (h->element, "samples-selected",
      G_CALLBACK (samples_selected_cb), NULL);
  g_object_set (h->element, "output-padding", FALSE, NULL);

  gst_element_set_state (h->element, GST_STATE_NULL);
  gst_element_set_state (h->element, GST_STATE_PLAYING);

  gst_harness_set_src_caps_str (h, foo_bar_caps_60fps.string);
  gst_harness_set_src_caps_str (h2, cea708_cc_data_caps.string);

  /* Push a buffer and caption buffer */
  for (i = 0; i < 8; i++) {
    GstBuffer *video_buf, *caption_buf = NULL;

    video_buf = gst_buffer_new_and_alloc (128);
    GST_BUFFER_PTS (video_buf) = i * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (video_buf) = 40 * GST_MSECOND;
    gst_harness_push (h, video_buf);

    caption_buf = gst_buffer_new_and_alloc (3);
    gst_buffer_fill (caption_buf, 0, cc_data, 3);
    GST_BUFFER_PTS (caption_buf) = i * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (caption_buf) = 40 * GST_MSECOND;
    gst_harness_push (h2, caption_buf);

    /* Pull the previous output buffer */
    if (i > 0) {
      int j;
      outbuf = gst_harness_pull (h);
      fail_unless (outbuf != NULL);

      meta = gst_buffer_get_video_caption_meta (outbuf);
      fail_unless (meta != NULL);
      fail_unless_equals_int (meta->caption_type,
          GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
      fail_unless_equals_int (meta->size, 3);
      GST_MEMDUMP ("caption data", meta->data, meta->size);

      for (j = 0; j < meta->size; j++) {
        if ((i % 2) == 1) {
          fail_unless_equals_int (meta->data[j], cc_data[j]);
        } else {
          int padding_field2[] = { 0xf9, 0x00, 0x00 };
          fail_unless_equals_int (meta->data[j], padding_field2[j]);
        }
      }

      gst_buffer_unref (outbuf);
    }
    expected_video_buffer = video_buf;
    if (caption_buf)
      expected_caption_buffer = caption_buf;
  }

  /* Push EOS on both pads get the second output buffer, we otherwise wait
   * in case there are further captions for the current video buffer */
  gst_harness_push_event (h, gst_event_new_eos ());
  gst_harness_push_event (h2, gst_event_new_eos ());

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 3);

  gst_buffer_unref (outbuf);

  /* Caps should be equal to input caps */
  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_can_intersect (caps,
          gst_static_caps_get (&foo_bar_caps)));
  gst_caps_unref (caps);

  gst_harness_teardown (h);
  gst_harness_teardown (h2);
}

GST_END_TEST;

GST_START_TEST (captions_50fps_to_25fps)
{
  GstHarness *h, *h2;
  GstBuffer *outbuf;
  GstPad *caption_pad;
  GstCaps *caps;
  GstVideoCaptionMeta *meta;
  const guint8 cc_data_both[72] = {
    0xfc, 0x20, 0x20, 0xfd, 0x20, 0x20, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
  };
  const guint8 cc_data_field1[36] = {
    0xfc, 0x20, 0x20, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
  };
  const guint8 cc_data_field2[36] = {
    0xfd, 0x20, 0x20, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
    0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00, 0xfa, 0x00, 0x00,
  };
  int i = 0;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");
  h2 = gst_harness_new_with_element (h->element, NULL, NULL);
  caption_pad = gst_element_request_pad_simple (h->element, "caption");
  gst_harness_add_element_sink_pad (h2, caption_pad);
  gst_object_unref (caption_pad);

  g_object_set (h->element, "emit-signals", TRUE, NULL);
  g_signal_connect (h->element, "samples-selected",
      G_CALLBACK (samples_selected_cb), NULL);

  gst_element_set_state (h->element, GST_STATE_NULL);
  gst_element_set_state (h->element, GST_STATE_PLAYING);

  gst_harness_set_src_caps_str (h, foo_bar_caps_25fps.string);
  gst_harness_set_src_caps_str (h2, cea708_cc_data_caps.string);

  /* We're rescheduling a 50 fps stream (1 tuple per frame)
   * to 25 fps (2 tuples per frame) */

  /* Push a video buffer and two caption buffers */
  for (i = 0; i < 300; i++) {
    GstBuffer *video_buf, *caption_buf = NULL;

    video_buf = gst_buffer_new_and_alloc (128);
    GST_BUFFER_PTS (video_buf) = i * 40 * GST_MSECOND;
    GST_BUFFER_DURATION (video_buf) = 40 * GST_MSECOND;
    gst_harness_push (h, video_buf);

    caption_buf = gst_buffer_new_and_alloc (36);
    gst_buffer_fill (caption_buf, 0, cc_data_field1, 36);
    GST_BUFFER_PTS (caption_buf) = (2 * i) * 20 * GST_MSECOND;
    GST_BUFFER_DURATION (caption_buf) = 20 * GST_MSECOND;
    gst_harness_push (h2, caption_buf);

    caption_buf = gst_buffer_new_and_alloc (36);
    gst_buffer_fill (caption_buf, 0, cc_data_field2, 36);
    GST_BUFFER_PTS (caption_buf) = (2 * i + 1) * 20 * GST_MSECOND;
    GST_BUFFER_DURATION (caption_buf) = 20 * GST_MSECOND;
    gst_harness_push (h2, caption_buf);

    /* Pull the previous output buffer */
    if (i > 0) {
      int j;
      outbuf = gst_harness_pull (h);
      fail_unless (outbuf != NULL);

      meta = gst_buffer_get_video_caption_meta (outbuf);
      fail_unless (meta != NULL);
      fail_unless_equals_int (meta->caption_type,
          GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
      fail_unless_equals_int (meta->size, 72);
      GST_MEMDUMP ("caption data", meta->data, meta->size);

      for (j = 0; j < meta->size; j++) {
        fail_unless_equals_int (meta->data[j], cc_data_both[j]);
      }

      gst_buffer_unref (outbuf);
    }
    expected_video_buffer = video_buf;
    if (caption_buf)
      expected_caption_buffer = caption_buf;
  }

  /* Push EOS on both pads get the second output buffer, we otherwise wait
   * in case there are further captions for the current video buffer */
  gst_harness_push_event (h, gst_event_new_eos ());
  gst_harness_push_event (h2, gst_event_new_eos ());

  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 72);

  gst_buffer_unref (outbuf);

  /* Caps should be equal to input caps */
  caps = gst_pad_get_current_caps (h->sinkpad);
  fail_unless (caps != NULL);
  fail_unless (gst_caps_can_intersect (caps,
          gst_static_caps_get (&foo_bar_caps)));
  gst_caps_unref (caps);

  gst_harness_teardown (h);
  gst_harness_teardown (h2);
}

GST_END_TEST;

typedef struct
{
  GMutex lock;
  GCond cond;
  guint num_buffers;
  gboolean got_custom_event;
  gboolean got_eos;
} CapsChangeData;

static GstPadProbeReturn
video_caps_change_probe (GstPad * pad, GstPadProbeInfo * info,
    CapsChangeData * data)
{
  g_mutex_lock (&data->lock);
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    GstEvent *ev = GST_PAD_PROBE_INFO_EVENT (info);
    switch (GST_EVENT_TYPE (ev)) {
      case GST_EVENT_CUSTOM_DOWNSTREAM:
        data->got_custom_event = TRUE;
        g_cond_signal (&data->cond);
        break;
      case GST_EVENT_CAPS:
      {
        GstCaps *caps;
        GstStructure *s;
        const gchar *name;

        gst_event_parse_caps (ev, &caps);
        s = gst_caps_get_structure (caps, 0);
        name = gst_structure_get_name (s);

        if (data->num_buffers == 0) {
          fail_unless_equals_string (name, "test/foo");
        } else {
          fail_unless_equals_string (name, "test/bar");
        }
        break;
      }
      case GST_EVENT_EOS:
        data->got_eos = TRUE;
        g_cond_signal (&data->cond);
        break;
      default:
        break;

    }
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))) {
    data->num_buffers++;
  }
  g_mutex_unlock (&data->lock);

  return GST_PAD_PROBE_DROP;
}

GST_START_TEST (video_caps_change)
{
  GstBuffer *buf;
  GstPad *sinkpad;
  GstPad *caption_pad;
  GstPad *srcpad;
  GstCaps *caps;
  const guint8 cc_data[3] = { 0xfc, 0x20, 0x20 };
  GstEvent *event;
  CapsChangeData data;
  GstElement *elem;
  GstSegment segment;
  GstFlowReturn flow_ret;
  gboolean ret;

  g_mutex_init (&data.lock);
  g_cond_init (&data.cond);
  data.num_buffers = 0;
  data.got_custom_event = FALSE;
  data.got_eos = FALSE;

  elem = gst_element_factory_make ("cccombiner", NULL);

  g_object_set (elem, "schedule", FALSE, "output-padding", FALSE, NULL);

  sinkpad = gst_element_get_static_pad (elem, "sink");
  srcpad = gst_element_get_static_pad (elem, "src");
  caption_pad = gst_element_request_pad_simple (elem, "caption");

  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM,
      (GstPadProbeCallback) video_caps_change_probe, &data, NULL);

  gst_element_set_state (elem, GST_STATE_PLAYING);

  /* Send mandatory events */
  gst_pad_send_event (sinkpad, gst_event_new_stream_start ("test-start-0"));
  gst_pad_send_event (caption_pad, gst_event_new_stream_start ("test-start-1"));

  caps = gst_caps_from_string ("test/foo");
  fail_unless (caps);
  gst_pad_send_event (sinkpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  caps = gst_caps_from_string (cea708_cc_data_caps.string);
  fail_unless (caps);
  gst_pad_send_event (caption_pad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_send_event (sinkpad, gst_event_new_segment (&segment));
  gst_pad_send_event (caption_pad, gst_event_new_segment (&segment));

  /* Push a video buffer */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  /* Push a gap event. aggregate() will consume this event
   * and cccombiner will hold the first video buffer without pushing
   * to downstream */
  event = gst_event_new_gap (0, 40 * GST_MSECOND - 1);
  ret = gst_pad_send_event (caption_pad, event);
  fail_unless (ret);

  /* Send a serialized event to ensure aggregate() got called */
  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
      gst_structure_new_empty ("test-caps-serialize"));
  ret = gst_pad_send_event (caption_pad, event);
  fail_unless (ret);

  g_mutex_lock (&data.lock);
  while (!data.got_custom_event)
    g_cond_wait (&data.cond, &data.lock);

  /* there should no buffer pushed at this point */
  fail_unless (data.num_buffers == 0);
  g_mutex_unlock (&data.lock);

  /* Push new caps with buffers */
  caps = gst_caps_new_empty_simple ("test/bar");
  gst_pad_send_event (sinkpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  flow_ret = gst_pad_chain (sinkpad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  buf = gst_buffer_new_and_alloc (3);
  gst_buffer_fill (buf, 0, cc_data, 3);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  flow_ret = gst_pad_chain (caption_pad, buf);
  fail_unless (flow_ret == GST_FLOW_OK);

  ret = gst_pad_send_event (sinkpad, gst_event_new_eos ());
  fail_unless (ret);
  ret = gst_pad_send_event (caption_pad, gst_event_new_eos ());
  fail_unless (ret);

  g_mutex_lock (&data.lock);
  while (!data.got_eos)
    g_cond_wait (&data.cond, &data.lock);

  fail_unless_equals_int (data.num_buffers, 2);
  g_mutex_unlock (&data.lock);

  gst_element_set_state (elem, GST_STATE_NULL);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_element_release_request_pad (elem, caption_pad);
  gst_object_unref (caption_pad);
  gst_object_unref (elem);

  g_mutex_clear (&data.lock);
  g_cond_clear (&data.cond);
}

GST_END_TEST;

static Suite *
cccombiner_suite (void)
{
  Suite *s = suite_create ("cccombiner");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, no_captions);
  tcase_add_test (tc, no_captions_no_duration);
  tcase_add_test (tc, captions_and_eos);
  tcase_add_test (tc, captions_no_output_padding_60fps_608_field1_only);
  tcase_add_test (tc, captions_50fps_to_25fps);
  tcase_add_test (tc, video_caps_change);

  return s;
}

GST_CHECK_MAIN (cccombiner);
