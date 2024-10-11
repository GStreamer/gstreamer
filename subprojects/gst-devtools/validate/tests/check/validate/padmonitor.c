/* GstValidate
 * Copyright (C) 2014 Thibault Saunier <thibault.saunier@collabora.com>
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

#include <gio/gio.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-pad-monitor.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/check/gstcheck.h>
#include "test-utils.h"

static GstValidateRunner *
_start_monitoring_bin (GstBin * bin)
{
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;

  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT (bin), runner, NULL);

  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));
  return runner;
}

static void
_stop_monitoring_bin (GstBin * bin, GstValidateRunner * runner)
{
  GstValidateMonitor *monitor;

  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (bin),
      "validate-monitor");
  gst_object_unref (bin);
  ASSERT_OBJECT_REFCOUNT (monitor, "monitor", 1);
  gst_object_unref (monitor);
  ASSERT_OBJECT_REFCOUNT (runner, "runner", 2);
  gst_object_unref (runner);
}

static GstValidateMonitor *
_start_monitoring_element (GstElement * element, GstValidateRunner * runner)
{
  GstValidateMonitor *monitor;

  monitor = gst_validate_monitor_factory_create (GST_OBJECT (element),
      runner, NULL);

  return monitor;
}

static void
_check_reports_refcount (GstPad * pad, gint refcount)
{
  GList *tmp, *reports;
  GstValidateReporter *reporter =
      (GstValidateReporter *) g_object_get_data (G_OBJECT (pad),
      "validate-monitor");

  reports = gst_validate_reporter_get_reports (reporter);
  /* We take a ref here */
  refcount += 1;

  for (tmp = reports; tmp; tmp = tmp->next)
    fail_unless_equals_int (((GstMiniObject *) tmp->data)->refcount, refcount);

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
}

static GstBuffer *
gst_discont_buffer_new (void)
{
  GstBuffer *buffer = gst_buffer_new ();

  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  return buffer;
}

GST_START_TEST (buffer_before_segment)
{
  GstPad *srcpad, *sinkpad;
  GstElement *sink;
  GstValidateRunner *runner;
  GstValidateReport *report;
  GstValidateMonitor *monitor;
  GList *reports;

  /* getting an existing element class is cheating, but easier */
  sink = gst_element_factory_make ("fakesink", "fakesink");

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK);
  gst_clear_object (&sinkpad);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT (srcpad), runner, NULL);
  fail_unless (GST_IS_VALIDATE_PAD_MONITOR (monitor));


  /* We want to handle the src behaviour ourself */
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  /* Send a buffer before pushing any segment (FAILS) */
  {
    _gst_check_expecting_log = TRUE;
    fail_unless_equals_int (gst_pad_push (srcpad,
            gst_discont_buffer_new ()), GST_FLOW_OK);

    reports = gst_validate_runner_get_reports (runner);
    assert_equals_int (g_list_length (reports), 1);
    report = reports->data;
    fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
    fail_unless_equals_int (report->issue->issue_id, BUFFER_BEFORE_SEGMENT);
    g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  }

  /* Setup all needed event and push a new buffer (WORKS) */
  {
    _gst_check_expecting_log = FALSE;
    gst_check_setup_events (srcpad, sink, NULL, GST_FORMAT_TIME);
    fail_unless_equals_int (gst_pad_push (srcpad, gst_discont_buffer_new ()),
        GST_FLOW_OK);
    reports = gst_validate_runner_get_reports (runner);
    assert_equals_int (g_list_length (reports), 1);
    g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  }

  /* clean up */
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, FALSE));
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  _check_reports_refcount (srcpad, 2);
  gst_object_unref (srcpad);
  gst_check_object_destroyed_on_unref (sink);
  ASSERT_OBJECT_REFCOUNT (runner, "runner", 2);
  gst_object_unref (runner);
}

GST_END_TEST;

GST_START_TEST (buffer_outside_segment)
{
  GstPad *srcpad, *pad;
  GstBuffer *buffer;
  GstSegment segment;
  GstElement *sink, *identity;
  gchar *identity_klass;
  GstValidateReport *report;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GList *reports;

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  identity = gst_element_factory_make ("identity", NULL);
  sink = gst_element_factory_make ("fakesink", "fakesink");

  identity_klass =
      g_strdup (gst_element_class_get_metadata (GST_ELEMENT_GET_CLASS
          (identity), "klass"));

  /* Testing if a buffer is outside a segment is only done for buffer outputed
   * from decoders for the moment, fake a Decoder so that the test is properly
   * executed */
  gst_element_class_add_metadata (GST_ELEMENT_GET_CLASS (identity), "klass",
      "Decoder");

  pad = gst_element_get_static_pad (identity, "sink");
  fail_unless (gst_pad_link (srcpad, pad) == GST_PAD_LINK_OK);
  gst_clear_object (&pad);

  fail_unless (gst_element_link (identity, sink));

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT (identity), runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  pad = gst_element_get_static_pad (identity, "src");
  fail_unless (GST_IS_VALIDATE_PAD_MONITOR (g_object_get_data ((GObject *)
              pad, "validate-monitor")));
  gst_clear_object (&pad);

  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));
  fail_unless_equals_int (gst_element_set_state (identity, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("the-stream")));
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&segment)));

  /*  Pushing a buffer that is outside the segment */
  {
    buffer = gst_discont_buffer_new ();
    GST_BUFFER_PTS (buffer) = 10 * GST_SECOND;
    GST_BUFFER_DURATION (buffer) = GST_SECOND;
    fail_if (GST_PAD_IS_FLUSHING (gst_element_get_static_pad (identity,
                "sink")));
    fail_if (GST_PAD_IS_FLUSHING (gst_element_get_static_pad (identity,
                "src")));
    fail_if (GST_PAD_IS_FLUSHING (gst_element_get_static_pad (sink, "sink")));
    fail_unless_equals_int (gst_pad_push (srcpad, buffer), GST_FLOW_OK);

    reports = gst_validate_runner_get_reports (runner);
    assert_equals_int (g_list_length (reports), 1);
    report = reports->data;
    fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_ISSUE);
    fail_unless_equals_int (report->issue->issue_id, BUFFER_IS_OUT_OF_SEGMENT);
    g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  }

  /* Pushing a buffer inside the segment */
  {
    fail_unless_equals_int (gst_pad_push (srcpad, gst_discont_buffer_new ()),
        GST_FLOW_OK);
    reports = gst_validate_runner_get_reports (runner);
    assert_equals_int (g_list_length (reports), 1);
    g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  }


  /* clean up */
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, FALSE));
  gst_object_unref (srcpad);

  gst_element_class_add_metadata (GST_ELEMENT_GET_CLASS (identity), "klass",
      identity_klass);
  g_free (identity_klass);
  gst_object_unref (runner);

  fail_unless_equals_int (gst_element_set_state (identity, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_element_unlink (identity, sink);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  gst_object_unref (identity);
  gst_object_unref (sink);
  gst_object_unref (monitor);
}

GST_END_TEST;

static void
fake_demuxer_prepare_pads (GstBin * pipeline, GstElement * demux,
    GstValidateRunner * runner)
{
  gint i = 0;
  GList *tmp;

  fail_unless (g_list_length (demux->srcpads), 3);

  for (tmp = demux->srcpads; tmp; tmp = tmp->next) {
    GstPad *new_peer;
    gchar *name = g_strdup_printf ("sink-%d", i++);
    GstElement *sink = gst_element_factory_make ("fakesink", name);

    gst_bin_add (pipeline, sink);

    new_peer = sink->sinkpads->data;
    gst_pad_link (tmp->data, new_peer);
    gst_element_set_state (sink, GST_STATE_PLAYING);
    gst_pad_activate_mode (tmp->data, GST_PAD_MODE_PUSH, TRUE);

    g_free (name);
  }

  fail_unless (gst_pad_activate_mode (demux->sinkpads->data, GST_PAD_MODE_PUSH,
          TRUE));
}

static void
_test_flow_aggregation (GstFlowReturn flow, GstFlowReturn flow1,
    GstFlowReturn flow2, GstFlowReturn demux_flow, gboolean should_fail)
{
  GstPad *srcpad;
  GstValidateReport *report;
  GstPad *p, *p1, *p2;
  GstElement *demuxer = fake_demuxer_new ();
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateRunner *runner;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add (pipeline, demuxer);
  fake_demuxer_prepare_pads (pipeline, demuxer, runner);

  srcpad = gst_pad_new ("srcpad1", GST_PAD_SRC);
  gst_pad_link (srcpad, demuxer->sinkpads->data);
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));
  gst_check_setup_events_with_stream_id (srcpad, demuxer, NULL,
      GST_FORMAT_TIME, "the-stream");

  p = gst_pad_get_peer (demuxer->srcpads->data);
  p1 = gst_pad_get_peer (demuxer->srcpads->next->data);
  p2 = gst_pad_get_peer (demuxer->srcpads->next->next->data);

  p->ABI.abi.last_flowret = flow;
  p1->ABI.abi.last_flowret = flow1;
  p2->ABI.abi.last_flowret = flow2;
  FAKE_DEMUXER (demuxer)->return_value = demux_flow;

  gst_object_unref (p);
  gst_object_unref (p1);
  gst_object_unref (p2);

  fail_unless_equals_int (gst_pad_push (srcpad, gst_discont_buffer_new ()),
      demux_flow);

  reports = gst_validate_runner_get_reports (runner);
  if (should_fail) {
    assert_equals_int (g_list_length (reports), 1);
    report = reports->data;
    fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_CRITICAL);
    fail_unless_equals_int (report->issue->issue_id, WRONG_FLOW_RETURN);
  } else {
    assert_equals_int (g_list_length (reports), 0);

  }

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  clean_bus (GST_ELEMENT (pipeline));

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  ASSERT_OBJECT_REFCOUNT (pipeline, "ours", 1);
  gst_object_ref (demuxer);
  _stop_monitoring_bin (pipeline, runner);

  ASSERT_OBJECT_REFCOUNT (demuxer, "plop", 1);
  gst_object_unref (demuxer);
  gst_object_unref (srcpad);
}

#define FLOW_TEST(name, flow1, flow2, flow3, demux_flow, fails) \
GST_START_TEST(flow_aggregation_##name) { \
  _test_flow_aggregation (GST_FLOW_##flow1, GST_FLOW_##flow2, \
      GST_FLOW_##flow3, GST_FLOW_##demux_flow, fails); \
} GST_END_TEST

FLOW_TEST (ok_ok_error_ok, OK, OK, ERROR, OK, TRUE);
FLOW_TEST (eos_eos_eos_ok, EOS, EOS, EOS, OK, TRUE);
FLOW_TEST (not_neg_ok_ok_ok, NOT_NEGOTIATED, OK, OK, OK, TRUE);
/*[> Passing cases: <]*/
FLOW_TEST (flushing_ok_ok_ok, FLUSHING, OK, OK, OK, FALSE);
FLOW_TEST (eos_eos_eos_eos, EOS, EOS, EOS, EOS, FALSE);
FLOW_TEST (eos_eos_ok_ok, EOS, EOS, OK, OK, FALSE);
FLOW_TEST (ok_ok_ok_eos, OK, OK, OK, EOS, FALSE);
FLOW_TEST (not_neg_ok_ok_not_neg, NOT_NEGOTIATED, OK, OK, NOT_NEGOTIATED,
    FALSE);
#undef FLOW_TEST

GST_START_TEST (issue_concatenation)
{
  GstPad *srcpad1, *srcpad2, *sinkpad, *fakemixer_sink1, *fakemixer_sink2;
  GstElement *src1, *src2, *sink, *fakemixer;
  GstValidateRunner *runner;
  GstValidatePadMonitor *srcpad_monitor1, *srcpad_monitor2, *sinkpad_monitor;
  GstValidatePadMonitor *fakemixer_sink_monitor1, *fakemixer_sink_monitor2;
  GList *reports;
  gint n_reports;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "subchain", TRUE));
  runner = gst_validate_runner_new ();

  src1 = create_and_monitor_element ("fakesrc2", NULL, runner);
  src2 = create_and_monitor_element ("fakesrc2", NULL, runner);
  fakemixer = create_and_monitor_element ("fakemixer", "fakemixer", runner);
  sink = create_and_monitor_element ("fakesink", "fakesink", runner);

  srcpad1 = gst_element_get_static_pad (src1, "src");
  srcpad_monitor1 = g_object_get_data (G_OBJECT (srcpad1), "validate-monitor");
  srcpad2 = gst_element_get_static_pad (src2, "src");
  srcpad_monitor2 = g_object_get_data (G_OBJECT (srcpad2), "validate-monitor");
  fakemixer_sink1 = gst_element_request_pad_simple (fakemixer, "sink_%u");
  fakemixer_sink_monitor1 =
      g_object_get_data (G_OBJECT (fakemixer_sink1), "validate-monitor");
  fakemixer_sink2 = gst_element_request_pad_simple (fakemixer, "sink_%u");
  fakemixer_sink_monitor2 =
      g_object_get_data (G_OBJECT (fakemixer_sink2), "validate-monitor");
  sinkpad = gst_element_get_static_pad (sink, "sink");
  sinkpad_monitor = g_object_get_data (G_OBJECT (sinkpad), "validate-monitor");

  fail_unless (gst_element_link (fakemixer, sink));
  fail_unless (gst_pad_link (srcpad1, fakemixer_sink1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (srcpad2, fakemixer_sink2) == GST_PAD_LINK_OK);

  /* We want to handle the src behaviour ourselves */
  fail_unless (gst_pad_activate_mode (srcpad1, GST_PAD_MODE_PUSH, TRUE));
  fail_unless (gst_pad_activate_mode (srcpad2, GST_PAD_MODE_PUSH, TRUE));

  gst_check_setup_events_with_stream_id (srcpad1, fakemixer, NULL,
      GST_FORMAT_TIME, "stream1");
  gst_check_setup_events_with_stream_id (srcpad2, fakemixer, NULL,
      GST_FORMAT_TIME, "stream2");

  fail_unless_equals_int (gst_element_set_state (fakemixer, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);


  /* Send an unexpected flush stop */
  _gst_check_expecting_log = TRUE;
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE)));

  /* The runner only sees one report */
  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 1);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  /* Each pad monitor on the way actually holds a report */
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      srcpad_monitor1);
  fail_unless_equals_int (n_reports, 1);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      sinkpad_monitor);
  fail_unless_equals_int (n_reports, 1);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      fakemixer_sink_monitor1);
  fail_unless_equals_int (n_reports, 1);

  /* But not the pad monitor of the other fakemixer sink */
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      fakemixer_sink_monitor2);
  fail_unless_equals_int (n_reports, 0);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      srcpad_monitor2);
  fail_unless_equals_int (n_reports, 0);

  /* Once again but on the other fakemixer sink */
  fail_unless (gst_pad_push_event (srcpad2, gst_event_new_flush_stop (TRUE)));

  /* The runner now sees two reports */
  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 2);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  /* These monitors already saw that issue */
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      srcpad_monitor1);
  fail_unless_equals_int (n_reports, 1);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      sinkpad_monitor);
  fail_unless_equals_int (n_reports, 1);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      fakemixer_sink_monitor1);
  fail_unless_equals_int (n_reports, 1);

  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      fakemixer_sink_monitor2);
  fail_unless_equals_int (n_reports, 1);
  n_reports = gst_validate_reporter_get_reports_count ((GstValidateReporter *)
      srcpad_monitor2);
  fail_unless_equals_int (n_reports, 1);

  /* clean up */
  fail_unless (gst_pad_activate_mode (srcpad1, GST_PAD_MODE_PUSH, FALSE));
  fail_unless (gst_pad_activate_mode (srcpad2, GST_PAD_MODE_PUSH, FALSE));
  fail_unless_equals_int (gst_element_set_state (fakemixer, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  /* The reporter, the runner */
  _check_reports_refcount (srcpad1, 2);
  /* The reporter, the master report */
  _check_reports_refcount (fakemixer_sink1, 2);
  free_element_monitor (src1);
  free_element_monitor (src2);
  free_element_monitor (fakemixer);
  free_element_monitor (sink);
  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  gst_object_unref (sinkpad);
  gst_object_unref (fakemixer_sink1);
  gst_object_unref (fakemixer_sink2);
  gst_check_objects_destroyed_on_unref (fakemixer, fakemixer_sink1,
      fakemixer_sink2, NULL);
  gst_check_objects_destroyed_on_unref (src1, srcpad1, NULL);
  gst_check_objects_destroyed_on_unref (src2, srcpad2, NULL);
  gst_check_objects_destroyed_on_unref (sink, sinkpad, NULL);
  ASSERT_OBJECT_REFCOUNT (runner, "runner", 2);
  gst_object_unref (runner);
}

GST_END_TEST;

/* *INDENT-OFF* */
static const gchar * media_info =
"<file duration='10031000000' frame-detection='1' uri='file:///I/am/so/fake.fakery' seekable='true'>"
"  <streams caps='video/quicktime'>"
"    <stream type='video' caps='video/x-raw'>"
"       <frame duration='1' id='0' is-keyframe='true'  offset='18446744073709551615' offset-end='18446744073709551615' pts='0'  dts='0' checksum='cfeb9b47da2bb540cd3fa84cffea4df9'/>"  /* buffer1 */
"       <frame duration='1' id='1' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='1'  dts='1' checksum='e40d7cd997bd14462468d201f1e1a3d4'/>" /* buffer2 */
"       <frame duration='1' id='2' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='2'  dts='2' checksum='4136320f0da0738a06c787dce827f034'/>" /* buffer3 */
"       <frame duration='1' id='3' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='3'  dts='3' checksum='sure my dear'/>" /* gonna fail */
"       <frame duration='1' id='4' is-keyframe='true'  offset='18446744073709551615' offset-end='18446744073709551615' pts='4'  dts='4' checksum='569d8927835c44fd4ff40b8408657f9e'/>"  /* buffer4 */
"       <frame duration='1' id='5' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='5'  dts='5' checksum='fcea4caed9b2c610fac1f2a6b38b1d5f'/>" /* buffer5 */
"       <frame duration='1' id='6' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='6'  dts='6' checksum='c7536747446a1503b1d9b02744144fa9'/>" /* buffer6 */
"       <frame duration='1' id='7' is-keyframe='false' offset='18446744073709551615' offset-end='18446744073709551615' pts='7'  dts='7' checksum='sure my dear'/>" /* gonna fail */
"      <tags>"
"      </tags>"
"    </stream>"
"  </streams>"
"</file>";
/* *INDENT-ON* */

typedef struct _BufferDesc
{
  const gchar *content;
  GstClockTime pts;
  GstClockTime dts;
  GstClockTime duration;
  gboolean keyframe;

  gint num_issues;
} BufferDesc;

static GstBuffer *
_create_buffer (BufferDesc * bdesc)
{
  gchar *tmp = g_strdup (bdesc->content);
  GstBuffer *buffer =
      gst_buffer_new_wrapped (tmp, strlen (tmp) * sizeof (gchar));

  GST_BUFFER_DTS (buffer) = bdesc->dts;
  GST_BUFFER_PTS (buffer) = bdesc->pts;
  GST_BUFFER_DURATION (buffer) = bdesc->duration;

  if (bdesc->keyframe)
    GST_BUFFER_FLAG_UNSET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  else
    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DELTA_UNIT);

  GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  return buffer;
}

static void
_check_media_info (GstSegment * segment, BufferDesc * bufs)
{
  GstEvent *segev;
  GstBuffer *buffer;
  GstElement *decoder;
  GstPad *srcpad, *sinkpad;
  GstValidateReport *report;
  GstValidateMonitor *monitor;
  GstValidateRunner *runner;
  GstValidateMediaDescriptor *mdesc;
  GstCaps *caps;

  GError *err = NULL;
  gint i, num_issues = 0;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = gst_validate_runner_new ();

  mdesc = (GstValidateMediaDescriptor *)
      gst_validate_media_descriptor_parser_new_from_xml (runner, media_info,
      &err);

  decoder = fake_decoder_new ();
  monitor = _start_monitoring_element (decoder, runner);
  gst_validate_monitor_set_media_descriptor (monitor, mdesc);
  gst_object_unref (mdesc);

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  sinkpad = decoder->sinkpads->data;
  ASSERT_OBJECT_REFCOUNT (sinkpad, "decoder ref", 1);
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));
  fail_unless_equals_int (gst_element_set_state (decoder, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  assert_equals_string (gst_pad_link_get_name (gst_pad_link (srcpad, sinkpad)),
      gst_pad_link_get_name (GST_PAD_LINK_OK));

  caps =
      gst_caps_from_string
      ("video/x-raw, width=360, height=42, framerate=24/1, pixel-aspect-ratio =1/1, format=AYUV");
  gst_check_setup_events_with_stream_id (srcpad, decoder, caps, GST_FORMAT_TIME,
      "the-stream");
  gst_caps_unref (caps);

  if (segment) {
    segev = gst_event_new_segment (segment);
    fail_unless (gst_pad_push_event (srcpad, segev));
  }

  for (i = 0; bufs[i].content != NULL; i++) {
    GList *reports;
    BufferDesc *buf = &bufs[i];
    buffer = _create_buffer (buf);

    assert_equals_string (gst_flow_get_name (gst_pad_push (srcpad, buffer)),
        gst_flow_get_name (GST_FLOW_OK));
    reports = gst_validate_runner_get_reports (runner);

    num_issues += buf->num_issues;
    assert_equals_int (g_list_length (reports), num_issues);

    if (buf->num_issues) {
      GList *tmp = g_list_nth (reports, num_issues - buf->num_issues);

      while (tmp) {
        report = tmp->data;

        fail_unless_equals_int (report->level,
            GST_VALIDATE_REPORT_LEVEL_WARNING);
        fail_unless_equals_int (report->issue->issue_id, WRONG_BUFFER);
        tmp = tmp->next;
      }
    }
    g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);
  }

  /* clean up */
  fail_unless (gst_pad_activate_mode (sinkpad, GST_PAD_MODE_PUSH, FALSE));
  fail_unless_equals_int (gst_element_set_state (decoder, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (srcpad);
  gst_check_objects_destroyed_on_unref (decoder, sinkpad, NULL);
  ASSERT_OBJECT_REFCOUNT (runner, "runner", 2);
  gst_object_unref (runner);
  gst_object_unref (monitor);
}

#define MEDIA_INFO_TEST(name,segment_start,bufs) \
GST_START_TEST(media_info_##name) { \
  if (segment_start >= 0) { \
    GstSegment segment; \
    gst_segment_init (&segment, GST_FORMAT_TIME); \
    segment.start = segment_start; \
     _check_media_info (&segment, (bufs)); \
  } else \
     _check_media_info (NULL, (bufs)); \
} GST_END_TEST

/* *INDENT-OFF* */
MEDIA_INFO_TEST (1, -1,
      ((BufferDesc []) {
      {
        .content = "buffer1",
        .pts = 0,
        .dts = 0,
        .duration = 1,
        .keyframe = TRUE,
        .num_issues = 0
      },
      {
        .content = "buffer2",
        .pts = 1,
        .dts = 1,
        .duration = 1,
        .keyframe = FALSE,
        .num_issues = 0
      },
      {
        .content = "buffer3",
        .pts = 2,
        .dts = 2,
        .duration = 1,
        .keyframe = FALSE,
        .num_issues = 0
      },
      {
        .content = "fail please",
        .pts = 3,
        .dts = 3,
        .duration = 1,
        .keyframe = FALSE,
        .num_issues = 1
      },
      { NULL}
    }));

/* Segment start is 2, the first buffer is expected (first Keyframe) */
MEDIA_INFO_TEST (2, 2,
      ((BufferDesc []) {
      {
        .content = "buffer2", /* Wrong checksum */
        .pts = 0,
        .dts = 0,
        .duration = 1,
        .keyframe = TRUE,
        .num_issues = 1
      },
      { NULL}
    }));

/* Segment start is 2, the first buffer is expected (first Keyframe) */
MEDIA_INFO_TEST (3, 2,
      ((BufferDesc []) {
      { /*  The right first buffer */
        .content = "buffer1",
        .pts = 0,
        .dts = 0,
        .duration = 1,
        .keyframe = TRUE,
        .num_issues = 0
      },
      { NULL}
    }));

/* Segment start is 6, the 4th buffer is expected (first Keyframe) */
MEDIA_INFO_TEST (4, 6,
      ((BufferDesc []) {
      { /*  The right fourth buffer */
        .content = "buffer4",
        .pts = 4,
        .dts = 4,
        .duration = 1,
        .keyframe = TRUE,
        .num_issues = 0
      },
      { NULL}
    }));

/* Segment start is 6, the 4th buffer is expected (first Keyframe) */
MEDIA_INFO_TEST (5, 6,
      ((BufferDesc []) {
      { /*  The sixth buffer... all wrong! */
        .content = "buffer6",
        .pts = 6,
        .dts = 6,
        .duration = 1,
        .keyframe = FALSE,
        .num_issues = 1
      },
      { NULL}
    }));
/* *INDENT-ON* */

GST_START_TEST (caps_events)
{
  GstPad *srcpad, *sinkpad;
  GstElement *decoder = fake_decoder_new ();
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateReport *report;
  GstValidateRunner *runner;
  GstCaps *caps;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add_many (pipeline, decoder, sink, NULL);
  srcpad = gst_pad_new ("srcpad1", GST_PAD_SRC);
  sinkpad = decoder->sinkpads->data;
  gst_pad_link (srcpad, sinkpad);

  gst_element_link (decoder, sink);
  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));

  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 0);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  caps =
      gst_caps_from_string
      ("video/x-raw, format=AYUV, width=320, height=240, pixel-aspect-ratio=1/1");
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);
  reports = gst_validate_runner_get_reports (runner);

  /* Our caps didn't have a framerate, the decoder sink should complain about
   * that */
  assert_equals_int (g_list_length (reports), 1);
  report = reports->data;
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_ISSUE);
  fail_unless_equals_int (report->issue->issue_id, CAPS_IS_MISSING_FIELD);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  caps =
      gst_caps_from_string
      ("video/x-raw, format=AYUV, framerate=24/1, width=(fraction)320, height=240, pixel-aspect-ratio=1/1");
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);

  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 2);
  report = reports->next->data;
  /* A width isn't supposed to be a fraction */
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
  fail_unless_equals_int (report->issue->issue_id, CAPS_FIELD_HAS_BAD_TYPE);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  caps =
      gst_caps_from_string
      ("video/x-raw, format=AYUV, framerate=24/1, width=320, height=240, pixel-aspect-ratio=1/1");
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);

  caps =
      gst_caps_from_string
      ("video/x-raw, format=AYUV, framerate=24/1, width=320, height=240, pixel-aspect-ratio=1/1");
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_caps (caps)));
  gst_caps_unref (caps);

  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 3);
  report = reports->next->next->data;
  /* A width isn't supposed to be a fraction */
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
  /* Pushing the same twice isn't very useful */
  fail_unless_equals_int (report->issue->issue_id, EVENT_CAPS_DUPLICATE);


  clean_bus (GST_ELEMENT (pipeline));

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  _stop_monitoring_bin (pipeline, runner);

  gst_object_unref (srcpad);
}

GST_END_TEST;

GST_START_TEST (eos_without_segment)
{
  GstPad *srcpad, *sinkpad;
  GstValidateReport *report;
  GstElement *decoder = fake_decoder_new ();
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateRunner *runner;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add_many (pipeline, decoder, sink, NULL);
  srcpad = gst_pad_new ("srcpad1", GST_PAD_SRC);
  sinkpad = decoder->sinkpads->data;
  gst_pad_link (srcpad, sinkpad);

  gst_element_link (decoder, sink);
  fail_unless_equals_int (gst_element_set_state (GST_ELEMENT (pipeline),
          GST_STATE_PLAYING), GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));

  reports = gst_validate_runner_get_reports (runner);
  assert_equals_int (g_list_length (reports), 0);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  fail_unless (gst_pad_push_event (srcpad, gst_event_new_eos ()));
  reports = gst_validate_runner_get_reports (runner);

  /* Getting the issue on the srcpad -> decoder.sinkpad -> decoder->srcpad */
  assert_equals_int (g_list_length (reports), 3);
  report = reports->data;
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
  fail_unless_equals_int (report->issue->issue_id, EVENT_EOS_WITHOUT_SEGMENT);
  clean_bus (GST_ELEMENT (pipeline));

  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  _stop_monitoring_bin (pipeline, runner);

  gst_object_unref (srcpad);
}

GST_END_TEST;

GST_START_TEST (buffer_timestamp_out_of_received_range)
{
  GstPad *srcpad, *sinkpad;
  GstElement *decoder = fake_decoder_new ();
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateRunner *runner;
  GstSegment segment;
  GstBuffer *buffer;
  GstPad *decoder_srcpad;
  GstValidateReport *report;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add_many (pipeline, decoder, sink, NULL);
  srcpad = gst_pad_new ("srcpad1", GST_PAD_SRC);
  sinkpad = decoder->sinkpads->data;
  gst_pad_link (srcpad, sinkpad);

  gst_element_link (decoder, sink);
  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_PLAYING,
      GST_STATE_CHANGE_ASYNC);
  fail_unless (gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE));

  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = GST_SECOND;
  fail_unless (gst_pad_push_event (srcpad,
          gst_event_new_stream_start ("the-stream")));
  fail_unless (gst_pad_push_event (srcpad, gst_event_new_segment (&segment)));

  {
    buffer = gst_discont_buffer_new ();
    GST_BUFFER_PTS (buffer) = 0 * GST_SECOND;
    GST_BUFFER_DURATION (buffer) = 0.1 * GST_SECOND;
    fail_unless (gst_pad_push (srcpad, buffer) == GST_FLOW_OK);
  }

  decoder_srcpad = gst_element_get_static_pad (decoder, "src");

  {
    buffer = gst_discont_buffer_new ();
    GST_BUFFER_PTS (buffer) = 0.9 * GST_SECOND;
    GST_BUFFER_DURATION (buffer) = 0.1 * GST_SECOND;
    fail_unless (gst_pad_push (decoder_srcpad, buffer) == GST_FLOW_OK);
  }

  reports = gst_validate_runner_get_reports (runner);

  assert_equals_int (g_list_length (reports), 1);
  report = reports->data;
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
  fail_unless_equals_int (report->issue->issue_id,
      BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (decoder_srcpad);
  gst_object_unref (srcpad);

  _stop_monitoring_bin (pipeline, runner);
}

GST_END_TEST;


GST_START_TEST (flow_error_without_message)
{
  GstElement *decoder = fake_decoder_new ();
  GstElement *src = gst_element_factory_make ("fakesrc", NULL);
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateRunner *runner;
  GstValidateReport *report;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add_many (pipeline, src, decoder, sink, NULL);
  fail_unless (gst_element_link_many (src, decoder, sink, NULL));

  FAKE_DECODER (decoder)->return_value = GST_FLOW_ERROR;
  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_PLAYING,
      GST_STATE_CHANGE_ASYNC);

  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  reports = gst_validate_runner_get_reports (runner);

  fail_unless (g_list_length (reports) >= 1);
  report = reports->data;
  fail_unless_equals_int (report->level, GST_VALIDATE_REPORT_LEVEL_WARNING);
  fail_unless_equals_int (report->issue->issue_id,
      FLOW_ERROR_WITHOUT_ERROR_MESSAGE);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  _stop_monitoring_bin (pipeline, runner);
}

GST_END_TEST;


GST_START_TEST (flow_error_with_message)
{
  GstElement *decoder = fake_decoder_new ();
  GstElement *src = gst_element_factory_make ("fakesrc", NULL);
  GstElement *sink = gst_element_factory_make ("fakesink", NULL);
  GstBin *pipeline = GST_BIN (gst_pipeline_new ("validate-pipeline"));
  GList *reports;
  GstValidateRunner *runner;
  GstValidateReport *report;
  const GError gerror =
      { G_IO_ERROR, G_IO_ERROR_FAILED, (gchar *) "fake error" };

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = _start_monitoring_bin (pipeline);

  gst_bin_add_many (pipeline, src, decoder, sink, NULL);
  fail_unless (gst_element_link_many (src, decoder, sink, NULL));

  g_object_set (src, "is-live", TRUE, NULL);

  FAKE_DECODER (decoder)->return_value = GST_FLOW_ERROR;

  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_PAUSED,
      GST_STATE_CHANGE_NO_PREROLL);

  gst_element_post_message (decoder,
      gst_message_new_error (GST_OBJECT (decoder),
          (GError *) & gerror, "This is a fake error"));

  ASSERT_SET_STATE (GST_ELEMENT (pipeline), GST_STATE_PLAYING,
      GST_STATE_CHANGE_ASYNC);

  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL,
      GST_CLOCK_TIME_NONE);

  reports = gst_validate_runner_get_reports (runner);

  assert_equals_int (g_list_length (reports), 1);
  report = reports->data;
  fail_unless_equals_int (report->issue->issue_id, ERROR_ON_BUS);
  g_list_free_full (reports, (GDestroyNotify) gst_validate_report_unref);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  _stop_monitoring_bin (pipeline, runner);
}

GST_END_TEST;



static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("padmonitor");
  TCase *tc_chain = tcase_create ("padmonitor");
  suite_add_tcase (s, tc_chain);

  if (atexit (gst_validate_deinit) != 0) {
    GST_ERROR ("failed to set gst_validate_deinit as exit function");
  }
  // Do not abort on critical issues, as this test will generate them on purpose.
  g_setenv ("GST_VALIDATE", "print_issues", TRUE);

  fake_elements_register ();

  tcase_add_test (tc_chain, buffer_before_segment);
  tcase_add_test (tc_chain, buffer_outside_segment);
  tcase_add_test (tc_chain, buffer_timestamp_out_of_received_range);

  tcase_add_test (tc_chain, media_info_1);
  tcase_add_test (tc_chain, media_info_2);
  tcase_add_test (tc_chain, media_info_3);
  tcase_add_test (tc_chain, media_info_4);
  tcase_add_test (tc_chain, media_info_5);

  tcase_add_test (tc_chain, flow_aggregation_ok_ok_error_ok);
  tcase_add_test (tc_chain, flow_aggregation_eos_eos_eos_ok);
  tcase_add_test (tc_chain, flow_aggregation_flushing_ok_ok_ok);
  tcase_add_test (tc_chain, flow_aggregation_not_neg_ok_ok_ok);
  tcase_add_test (tc_chain, flow_aggregation_eos_eos_eos_eos);
  tcase_add_test (tc_chain, flow_aggregation_eos_eos_ok_ok);
  tcase_add_test (tc_chain, flow_aggregation_ok_ok_ok_eos);
  tcase_add_test (tc_chain, flow_aggregation_not_neg_ok_ok_not_neg);

  tcase_add_test (tc_chain, issue_concatenation);
  tcase_add_test (tc_chain, eos_without_segment);
  tcase_add_test (tc_chain, caps_events);
  tcase_add_test (tc_chain, flow_error_without_message);
  tcase_add_test (tc_chain, flow_error_with_message);

  return s;
}

GST_CHECK_MAIN (gst_validate);
