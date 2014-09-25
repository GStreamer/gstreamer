/* GStreamer
 *
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *   Author: Thiago Santos <ts.santos@sisa.samsung.com>
 *
 * flowcombiner.c: Unit test for GstFlowCombiner
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
#include <gst/base/gstflowcombiner.h>

static GstFlowReturn sink_flowret = GST_FLOW_OK;

#define CHECK_COMBINED_FLOWS(f1, f2, f3, expected) \
G_STMT_START { \
  combiner = gst_flow_combiner_new (); \
  gst_flow_combiner_add_pad (combiner, pad1); \
  gst_flow_combiner_add_pad (combiner, pad2); \
  gst_flow_combiner_add_pad (combiner, pad3); \
  sink_flowret = f1; \
  gst_pad_push (pad1, gst_buffer_new ()); \
  gst_flow_combiner_update_flow (combiner, f1); \
  sink_flowret = f2; \
  gst_pad_push (pad2, gst_buffer_new ()); \
  gst_flow_combiner_update_flow (combiner, f2); \
  sink_flowret = f3; \
  gst_pad_push (pad3, gst_buffer_new ()); \
  ret = gst_flow_combiner_update_flow (combiner, f3); \
  gst_flow_combiner_free (combiner); \
  fail_unless_equals_int (ret, expected); \
} G_STMT_END

static GstFlowReturn
_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  gst_buffer_unref (buf);
  return sink_flowret;
}

GST_START_TEST (test_combined_flows)
{
  GstFlowReturn ret;
  GstFlowCombiner *combiner;
  GstPad *pad1, *pad2, *pad3;
  GstPad *peer1, *peer2, *peer3;
  GstSegment segment;

  pad1 = gst_pad_new ("src1", GST_PAD_SRC);
  pad2 = gst_pad_new ("src2", GST_PAD_SRC);
  pad3 = gst_pad_new ("src3", GST_PAD_SRC);

  peer1 = gst_pad_new ("sink1", GST_PAD_SINK);
  peer2 = gst_pad_new ("sink2", GST_PAD_SINK);
  peer3 = gst_pad_new ("sink3", GST_PAD_SINK);

  gst_pad_set_chain_function (peer1, _sink_chain);
  gst_pad_set_chain_function (peer2, _sink_chain);
  gst_pad_set_chain_function (peer3, _sink_chain);

  gst_pad_link (pad1, peer1);
  gst_pad_link (pad2, peer2);
  gst_pad_link (pad3, peer3);

  gst_pad_set_active (peer1, TRUE);
  gst_pad_set_active (peer2, TRUE);
  gst_pad_set_active (peer3, TRUE);
  gst_pad_set_active (pad1, TRUE);
  gst_pad_set_active (pad2, TRUE);
  gst_pad_set_active (pad3, TRUE);

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (pad1, gst_event_new_stream_start ("p1"));
  gst_pad_push_event (pad2, gst_event_new_stream_start ("p2"));
  gst_pad_push_event (pad3, gst_event_new_stream_start ("p3"));
  gst_pad_push_event (pad1, gst_event_new_segment (&segment));
  gst_pad_push_event (pad2, gst_event_new_segment (&segment));
  gst_pad_push_event (pad3, gst_event_new_segment (&segment));

  /* ok */
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_OK);

  /* not linked */
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_NOT_LINKED, GST_FLOW_OK,
      GST_FLOW_OK);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_EOS, GST_FLOW_OK, GST_FLOW_OK);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_NOT_LINKED, GST_FLOW_NOT_LINKED,
      GST_FLOW_OK);
  CHECK_COMBINED_FLOWS (GST_FLOW_NOT_LINKED, GST_FLOW_NOT_LINKED,
      GST_FLOW_NOT_LINKED, GST_FLOW_NOT_LINKED);

  /* errors */
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_ERROR, GST_FLOW_OK,
      GST_FLOW_ERROR);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_CUSTOM_ERROR, GST_FLOW_OK,
      GST_FLOW_CUSTOM_ERROR);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_NOT_NEGOTIATED, GST_FLOW_OK,
      GST_FLOW_NOT_NEGOTIATED);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_NOT_NEGOTIATED,
      GST_FLOW_NOT_NEGOTIATED);
  CHECK_COMBINED_FLOWS (GST_FLOW_NOT_LINKED, GST_FLOW_ERROR, GST_FLOW_OK,
      GST_FLOW_ERROR);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_ERROR,
      GST_FLOW_ERROR);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_CUSTOM_ERROR,
      GST_FLOW_CUSTOM_ERROR);

  /* flushing */
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_OK, GST_FLOW_FLUSHING,
      GST_FLOW_FLUSHING);
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_FLUSHING, GST_FLOW_OK,
      GST_FLOW_FLUSHING);
  CHECK_COMBINED_FLOWS (GST_FLOW_FLUSHING, GST_FLOW_FLUSHING, GST_FLOW_FLUSHING,
      GST_FLOW_FLUSHING);

  /* eos */
  CHECK_COMBINED_FLOWS (GST_FLOW_OK, GST_FLOW_NOT_LINKED, GST_FLOW_EOS,
      GST_FLOW_OK);
  CHECK_COMBINED_FLOWS (GST_FLOW_EOS, GST_FLOW_OK, GST_FLOW_EOS, GST_FLOW_OK);
  CHECK_COMBINED_FLOWS (GST_FLOW_EOS, GST_FLOW_EOS, GST_FLOW_EOS, GST_FLOW_EOS);

  /* eos + not-linked */
  CHECK_COMBINED_FLOWS (GST_FLOW_NOT_LINKED, GST_FLOW_EOS, GST_FLOW_EOS,
      GST_FLOW_EOS);
  CHECK_COMBINED_FLOWS (GST_FLOW_NOT_LINKED, GST_FLOW_NOT_LINKED, GST_FLOW_EOS,
      GST_FLOW_EOS);

  gst_object_unref (pad1);
  gst_object_unref (pad2);
  gst_object_unref (pad3);
  gst_object_unref (peer1);
  gst_object_unref (peer2);
  gst_object_unref (peer3);
}

GST_END_TEST;

GST_START_TEST (test_clear)
{
  GstFlowCombiner *combiner;
  GstPad *pad;
  GstPad *peer;
  GstSegment segment;
  GstFlowReturn ret;

  combiner = gst_flow_combiner_new ();

  /* add a pad and make it return _FLUSHING */
  pad = gst_pad_new ("src1", GST_PAD_SRC);
  peer = gst_pad_new ("sink1", GST_PAD_SINK);
  gst_pad_set_chain_function (peer, _sink_chain);
  gst_pad_link (pad, peer);
  gst_pad_set_active (peer, TRUE);
  gst_pad_set_active (pad, TRUE);
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (pad, gst_event_new_stream_start ("test1"));
  gst_pad_push_event (pad, gst_event_new_segment (&segment));
  gst_flow_combiner_add_pad (combiner, pad);
  sink_flowret = GST_FLOW_FLUSHING;
  fail_unless_equals_int (gst_pad_push (pad, gst_buffer_new ()),
      GST_FLOW_FLUSHING);

  /* the combined flow is _FLUSHING */
  ret = gst_flow_combiner_update_flow (combiner, GST_FLOW_FLUSHING);
  fail_unless_equals_int (ret, GST_FLOW_FLUSHING);
  gst_object_unref (pad);
  gst_object_unref (peer);

  /* add one more pad and make it return _OK */
  pad = gst_pad_new ("src2", GST_PAD_SRC);
  peer = gst_pad_new ("sink2", GST_PAD_SINK);
  gst_pad_set_chain_function (peer, _sink_chain);
  gst_pad_link (pad, peer);
  gst_pad_set_active (peer, TRUE);
  gst_pad_set_active (pad, TRUE);
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (pad, gst_event_new_stream_start ("test2"));
  gst_pad_push_event (pad, gst_event_new_segment (&segment));
  gst_flow_combiner_add_pad (combiner, pad);
  sink_flowret = GST_FLOW_OK;
  fail_unless_equals_int (gst_pad_push (pad, gst_buffer_new ()), GST_FLOW_OK);

  /* the combined flow is _FLUSHING because of the first pad */
  ret = gst_flow_combiner_update_flow (combiner, GST_FLOW_OK);
  fail_unless_equals_int (ret, GST_FLOW_FLUSHING);
  gst_object_unref (pad);
  gst_object_unref (peer);

  /* clear the combiner */
  gst_flow_combiner_clear (combiner);

  /* add a pad and make it return _OK */
  pad = gst_pad_new ("src3", GST_PAD_SRC);
  peer = gst_pad_new ("sink3", GST_PAD_SINK);
  gst_pad_set_chain_function (peer, _sink_chain);
  gst_pad_link (pad, peer);
  gst_pad_set_active (peer, TRUE);
  gst_pad_set_active (pad, TRUE);
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (pad, gst_event_new_stream_start ("test3"));
  gst_pad_push_event (pad, gst_event_new_segment (&segment));
  gst_flow_combiner_add_pad (combiner, pad);
  sink_flowret = GST_FLOW_OK;
  fail_unless_equals_int (gst_pad_push (pad, gst_buffer_new ()), GST_FLOW_OK);

  /* the combined flow is _OK since the other pads have been removed */
  ret = gst_flow_combiner_update_flow (combiner, GST_FLOW_OK);
  fail_unless_equals_int (ret, GST_FLOW_OK);
  gst_object_unref (pad);
  gst_object_unref (peer);

  gst_flow_combiner_free (combiner);
}

GST_END_TEST;

static Suite *
flow_combiner_suite (void)
{
  Suite *s = suite_create ("GstFlowCombiner");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_combined_flows);
  tcase_add_test (tc_chain, test_clear);

  return s;
}

GST_CHECK_MAIN (flow_combiner);
