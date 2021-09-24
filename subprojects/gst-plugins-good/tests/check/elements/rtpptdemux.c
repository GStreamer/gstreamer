/* GStreamer
 *
 * unit test for rtpptdemux element
 *
 * Copyright 2017 Pexip
 *  @author: Mikhail Fludkov <misha@pexip.com>
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
#include <gst/check/gstharness.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/gst.h>

static void
new_payload_type (G_GNUC_UNUSED GstElement * element, G_GNUC_UNUSED guint pt,
    GstPad * pad, GstHarness ** h)
{
  gst_harness_add_element_src_pad (*h, pad);
}

static void
test_rtpptdemux_srccaps_from_sinkcaps_base (const gchar * srccaps,
    const gchar * sinkcaps)
{
  GstCaps *caps;
  gchar *caps_str;
  GstHarness *h = gst_harness_new_with_padnames ("rtpptdemux", "sink", NULL);

  gst_harness_set_src_caps_str (h, srccaps);
  g_signal_connect (h->element,
      "new-payload-type", (GCallback) new_payload_type, &h);
  gst_harness_play (h);

  gst_buffer_unref (gst_harness_push_and_pull (h,
          gst_rtp_buffer_new_allocate (0, 0, 0)));

  caps = gst_pad_get_current_caps (h->sinkpad);
  caps_str = gst_caps_to_string (caps);
  fail_unless_equals_string (caps_str, sinkcaps);

  g_free (caps_str);
  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_START_TEST (test_rtpptdemux_srccaps_from_sinkcaps)
{
  test_rtpptdemux_srccaps_from_sinkcaps_base
      ("application/x-rtp, ssrc=(uint)1111",
      "application/x-rtp, ssrc=(uint)1111, payload=(int)0");
}

GST_END_TEST;

GST_START_TEST (test_rtpptdemux_srccaps_from_sinkcaps_nossrc)
{
  test_rtpptdemux_srccaps_from_sinkcaps_base ("application/x-rtp",
      "application/x-rtp, payload=(int)0");
}

GST_END_TEST;

static GstCaps *
request_pt_map (G_GNUC_UNUSED GstElement * demux,
    G_GNUC_UNUSED guint pt, const gchar * caps)
{
  return gst_caps_from_string (caps);
}

static void
test_rtpptdemux_srccaps_from_signal_base (const gchar * srccaps,
    const gchar * sigcaps, const gchar * sinkcaps)
{
  GstCaps *caps;
  gchar *caps_str;
  GstHarness *h = gst_harness_new_with_padnames ("rtpptdemux", "sink", NULL);

  gst_harness_set_src_caps_str (h, srccaps);
  g_signal_connect (h->element,
      "new-payload-type", (GCallback) new_payload_type, &h);
  g_signal_connect (h->element,
      "request-pt-map", (GCallback) request_pt_map, (gpointer) sigcaps);
  gst_harness_play (h);

  gst_buffer_unref (gst_harness_push_and_pull (h,
          gst_rtp_buffer_new_allocate (0, 0, 0)));

  caps = gst_pad_get_current_caps (h->sinkpad);
  caps_str = gst_caps_to_string (caps);
  fail_unless_equals_string (caps_str, sinkcaps);

  g_free (caps_str);
  gst_caps_unref (caps);
  gst_harness_teardown (h);
}

GST_START_TEST (test_rtpptdemux_srccaps_from_signal)
{
  test_rtpptdemux_srccaps_from_signal_base
      ("application/x-rtp, ssrc=(uint)1111",
      "application/x-rtp, encoding-name=(string)H264, media=(string)video, clock-rate=(int)90000",
      "application/x-rtp, encoding-name=(string)H264, media=(string)video, clock-rate=(int)90000, payload=(int)0, ssrc=(uint)1111");
}

GST_END_TEST;

GST_START_TEST (test_rtpptdemux_srccaps_from_signal_nossrc)
{
  test_rtpptdemux_srccaps_from_signal_base ("application/x-rtp",
      "application/x-rtp, encoding-name=(string)H264, media=(string)video, clock-rate=(int)90000",
      "application/x-rtp, encoding-name=(string)H264, media=(string)video, clock-rate=(int)90000, payload=(int)0");
}

GST_END_TEST;

static Suite *
rtpptdemux_suite (void)
{
  Suite *s = suite_create ("rtpptdemux");
  TCase *tc_chain;

  tc_chain = tcase_create ("general");
  tcase_add_test (tc_chain, test_rtpptdemux_srccaps_from_sinkcaps);
  tcase_add_test (tc_chain, test_rtpptdemux_srccaps_from_sinkcaps_nossrc);
  tcase_add_test (tc_chain, test_rtpptdemux_srccaps_from_signal);
  tcase_add_test (tc_chain, test_rtpptdemux_srccaps_from_signal_nossrc);
  suite_add_tcase (s, tc_chain);

  return s;
}

GST_CHECK_MAIN (rtpptdemux)
