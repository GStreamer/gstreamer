/* GStreamer unit tests for the srtp elements
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 * Copyright (C) 2016 Collabora Ltd <vincent.penquerch@collabora.co.uk>
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
# include "config.h"
#endif

#ifdef HAVE_VALGRIND
# include <valgrind/valgrind.h>
#endif

#include <gst/check/gstcheck.h>

#include <gst/check/gstharness.h>

GST_START_TEST (test_create_and_unref)
{
  GstElement *e;

  e = gst_element_factory_make ("srtpenc", NULL);
  fail_unless (e != NULL);
  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);

  e = gst_element_factory_make ("srtpdec", NULL);
  fail_unless (e != NULL);
  gst_element_set_state (e, GST_STATE_NULL);
  gst_object_unref (e);
}

GST_END_TEST;

GST_START_TEST (test_play)
{
  GstElement *source_pipeline, *sink_pipeline;
  GstBus *source_bus;
  GstMessage *msg;

  source_pipeline =
      gst_parse_launch
      ("audiotestsrc num-buffers=50 ! alawenc ! rtppcmapay ! application/x-rtp, payload=(int)8, ssrc=(uint)1356955624 ! srtpenc name=enc key=012345678901234567890123456789012345678901234567890123456789 ! udpsink port=5004 sync=false",
      NULL);
  sink_pipeline =
      gst_parse_launch
      ("udpsrc port=5004 caps=\"application/x-srtp, payload=(int)8, ssrc=(uint)1356955624, srtp-key=(buffer)012345678901234567890123456789012345678901234567890123456789, srtp-cipher=(string)aes-128-icm, srtp-auth=(string)hmac-sha1-80, srtcp-cipher=(string)aes-128-icm, srtcp-auth=(string)hmac-sha1-80\" ! srtpdec name=dec ! rtppcmadepay ! alawdec ! fakesink",
      NULL);

  fail_unless (gst_element_set_state (source_pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  fail_unless (gst_element_set_state (sink_pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

  source_bus = gst_pipeline_get_bus (GST_PIPELINE (source_pipeline));

  msg =
      gst_bus_timed_pop_filtered (source_bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (source_bus);

  gst_element_set_state (source_pipeline, GST_STATE_NULL);
  gst_element_set_state (sink_pipeline, GST_STATE_NULL);

  gst_object_unref (source_pipeline);
  gst_object_unref (sink_pipeline);
}

GST_END_TEST;

typedef struct
{
  guint counter;
  guint start_roc;
} roc_check_data;

static guint
get_roc (GstElement * e)
{
  const GstStructure *s, *ss;
  const GValue *v;
  guint roc = 0;

  g_object_get (e, "stats", &s, NULL);
  v = gst_structure_get_value (s, "streams");
  fail_unless (v);
  v = gst_value_array_get_value (v, 0);
  ss = gst_value_get_structure (v);
  gst_structure_get_uint (ss, "roc", &roc);
  return roc;
}

static GstPadProbeReturn
roc_check_probe (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  roc_check_data *data = user_data;
  GstElement *e = GST_PAD_PARENT (pad);

  /* record first roc, then wait for 2^16 packets to pass */
  if (data->counter == 0) {
    data->start_roc = get_roc (e);
  } else if (data->counter == 65536) {
    /* get roc and check it's one more than what we started with */
    fail_unless ((get_roc (e) & 0xffff) == ((data->start_roc + 1) & 0xffff));
  }
  data->counter++;
  return GST_PAD_PROBE_OK;
}

static GstCaps *
request_key (void)
{
  GstCaps *caps;

  caps =
      gst_caps_from_string
      ("application/x-srtp, payload=(int)8, ssrc=(uint)1356955624, srtp-key=(buffer)012345678901234567890123456789012345678901234567890123456789, srtp-cipher=(string)aes-128-icm, srtp-auth=(string)hmac-sha1-80, srtcp-cipher=(string)aes-128-icm, srtcp-auth=(string)hmac-sha1-80");
  return caps;
}

GST_START_TEST (test_roc)
{
  GstElement *source_pipeline, *sink_pipeline;
  GstElement *srtpenc, *srtpdec;
  GstBus *source_bus, *sink_bus;
  GstMessage *msg;
  GstPad *pad;
  roc_check_data source_roc_check_data, sink_roc_check_data;

  source_pipeline =
      gst_parse_launch
      ("audiotestsrc num-buffers=65555 ! alawenc ! rtppcmapay ! application/x-rtp, payload=(int)8, ssrc=(uint)1356955624 ! srtpenc name=enc key=012345678901234567890123456789012345678901234567890123456789 ! udpsink port=5004 sync=false",
      NULL);
  sink_pipeline =
      gst_parse_launch
      ("udpsrc port=5004 caps=\"application/x-srtp, payload=(int)8, ssrc=(uint)1356955624, srtp-key=(buffer)012345678901234567890123456789012345678901234567890123456789, srtp-cipher=(string)aes-128-icm, srtp-auth=(string)hmac-sha1-80, srtcp-cipher=(string)aes-128-icm, srtcp-auth=(string)hmac-sha1-80\" ! srtpdec name=dec ! rtppcmadepay ! alawdec ! fakesink",
      NULL);

  fail_unless (gst_element_set_state (source_pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  fail_unless (gst_element_set_state (sink_pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

  source_bus = gst_pipeline_get_bus (GST_PIPELINE (source_pipeline));
  sink_bus = gst_pipeline_get_bus (GST_PIPELINE (sink_pipeline));

  /* install a pad probe on the srtp elements' source pads */
  srtpenc = gst_bin_get_by_name (GST_BIN (source_pipeline), "enc");
  fail_unless (srtpenc != NULL);
  pad = gst_element_get_static_pad (srtpenc, "rtp_src_0");
  fail_unless (pad != NULL);
  source_roc_check_data.counter = 0;
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, roc_check_probe,
      &source_roc_check_data, NULL);
  gst_object_unref (pad);
  gst_object_unref (srtpenc);

  srtpdec = gst_bin_get_by_name (GST_BIN (sink_pipeline), "dec");
  fail_unless (srtpdec != NULL);
  g_signal_connect (srtpdec, "request_key", G_CALLBACK (request_key),
      GINT_TO_POINTER (0));
  pad = gst_element_get_static_pad (srtpdec, "rtp_src");
  sink_roc_check_data.counter = 0;
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, roc_check_probe,
      &sink_roc_check_data, NULL);
  fail_unless (pad != NULL);
  gst_object_unref (pad);
  gst_object_unref (srtpdec);

  msg =
      gst_bus_timed_pop_filtered (source_bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  gst_object_unref (source_bus);
  gst_object_unref (sink_bus);

  gst_element_set_state (source_pipeline, GST_STATE_NULL);
  gst_element_set_state (sink_pipeline, GST_STATE_NULL);

  gst_object_unref (source_pipeline);
  gst_object_unref (sink_pipeline);
}

GST_END_TEST;

static Suite *
srtp_suite (void)
{
  Suite *s = suite_create ("srtp");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_create_and_unref);
  tcase_add_test (tc_chain, test_play);
  tcase_add_test (tc_chain, test_roc);

  return s;
}

GST_CHECK_MAIN (srtp);
