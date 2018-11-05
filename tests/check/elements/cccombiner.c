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
static GstStaticCaps cea708_cc_data_caps =
GST_STATIC_CAPS ("closedcaption/x-cea-708,format=(string) cc_data");
static GstStaticCaps cea708_cdp_caps =
GST_STATIC_CAPS ("closedcaption/x-cea-708,format=(string) cdp");

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

GST_START_TEST (captions_and_eos)
{
  GstHarness *h, *h2;
  GstBuffer *buf, *outbuf;
  GstPad *caption_pad;
  GstCaps *caps;
  GstVideoCaptionMeta *meta;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");
  h2 = gst_harness_new_with_element (h->element, NULL, NULL);
  caption_pad = gst_element_get_request_pad (h->element, "caption");
  gst_harness_add_element_sink_pad (h2, caption_pad);
  gst_object_unref (caption_pad);

  gst_harness_set_src_caps_str (h, foo_bar_caps.string);
  gst_harness_set_src_caps_str (h2, cea708_cc_data_caps.string);

  /* Push a buffer and caption buffer */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h2, buf);

  /* And another one: the first video buffer should be retrievable
   * after the second caption buffer is pushed */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h2, buf);

  /* Pull the first output buffer */
  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 128);

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
  fail_unless_equals_int (meta->size, 128);

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

GST_START_TEST (captions_type_change_and_eos)
{
  GstHarness *h, *h2;
  GstBuffer *buf, *outbuf;
  GstPad *caption_pad;
  GstCaps *caps;
  GstVideoCaptionMeta *meta;

  h = gst_harness_new_with_padnames ("cccombiner", "sink", "src");
  h2 = gst_harness_new_with_element (h->element, NULL, NULL);
  caption_pad = gst_element_get_request_pad (h->element, "caption");
  gst_harness_add_element_sink_pad (h2, caption_pad);
  gst_object_unref (caption_pad);

  gst_harness_set_src_caps_str (h, foo_bar_caps.string);
  gst_harness_set_src_caps_str (h2, cea708_cc_data_caps.string);

  /* Push a buffer and caption buffer */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 0;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h2, buf);

  /* Change caption type */
  gst_harness_set_src_caps_str (h2, cea708_cdp_caps.string);

  /* And another one: the first video buffer should be retrievable
   * after the second caption buffer is pushed */
  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h, buf);

  buf = gst_buffer_new_and_alloc (128);
  GST_BUFFER_PTS (buf) = 40 * GST_MSECOND;
  GST_BUFFER_DURATION (buf) = 40 * GST_MSECOND;
  gst_harness_push (h2, buf);

  /* Pull the first output buffer */
  outbuf = gst_harness_pull (h);
  fail_unless (outbuf != NULL);

  meta = gst_buffer_get_video_caption_meta (outbuf);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->caption_type,
      GST_VIDEO_CAPTION_TYPE_CEA708_RAW);
  fail_unless_equals_int (meta->size, 128);

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
      GST_VIDEO_CAPTION_TYPE_CEA708_CDP);
  fail_unless_equals_int (meta->size, 128);

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

static Suite *
cccombiner_suite (void)
{
  Suite *s = suite_create ("cccombiner");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);

  tcase_add_test (tc, no_captions);
  tcase_add_test (tc, captions_and_eos);
  tcase_add_test (tc, captions_type_change_and_eos);

  return s;
}

GST_CHECK_MAIN (cccombiner);
