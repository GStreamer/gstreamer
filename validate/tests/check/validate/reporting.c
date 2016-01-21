/* GstValidate
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@collabora.com>
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

#include <gst/validate/validate.h>
#include <gst/check/gstcheck.h>
#include "test-utils.h"

GST_START_TEST (test_report_levels)
{
  GstValidateRunner *runner;
  GstObject *pipeline;
  GError *error = NULL;
  GstElement *element;
  GstValidateMonitor *monitor, *pipeline_monitor;
  GstPad *pad;

  /* FIXME: for now the only interface to set the reporting level is through an
   * environment variable parsed at the time of the runner initialization,
   * we can simplify that if the runner exposes API for that at some point
   */

  /* Try to set the default reporting level to ALL, the code is supposed to
   * be case insensitive */
  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "AlL", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_default_reporting_level (runner) ==
      GST_VALIDATE_SHOW_ALL);
  g_object_unref (runner);

  /* Try to set the default reporting level to subchain, the code is supposed to
   * parse numbers as well */
  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "2", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_default_reporting_level (runner) ==
      GST_VALIDATE_SHOW_SYNTHETIC);
  g_object_unref (runner);

  /* Try to set the reporting level for an object */
  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS",
          "synthetic,test_object:monitor,other_*:all", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "test_object") == GST_VALIDATE_SHOW_MONITOR);
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "other_test_object") == GST_VALIDATE_SHOW_ALL);
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "dummy_test_object") == GST_VALIDATE_SHOW_UNKNOWN);

  g_object_unref (runner);

  /* Now let's try to see if the created monitors actually understand the
   * situation they've put themselves into */
  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS",
          "none,pipeline*:monitor,sofake1:all,sofake*::sink:subchain", TRUE));
  runner = gst_validate_runner_new ();

  pipeline = (GstObject *)
      gst_parse_launch ("fakesrc name=sofake1 ! fakesink name=sofake2", &error);
  fail_unless (pipeline != NULL);
  pipeline_monitor =
      gst_validate_monitor_factory_create (GST_OBJECT (pipeline), runner, NULL);

  element = gst_bin_get_by_name (GST_BIN (pipeline), "sofake1");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (element),
      "validate-monitor");
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_SHOW_ALL);

  pad = gst_element_get_static_pad (element, "src");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (pad),
      "validate-monitor");
  /* The pad should have inherited the reporting level */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_SHOW_ALL);
  gst_object_unref (pad);

  gst_object_unref (element);

  element = gst_bin_get_by_name (GST_BIN (pipeline), "sofake2");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (element),
      "validate-monitor");
  /* The element should have inherited its reporting level from the pipeline */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_SHOW_MONITOR);

  pad = gst_element_get_static_pad (element, "sink");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (pad),
      "validate-monitor");
  /* But its pad should not as it falls in the sofake*::sink pattern */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_SHOW_SUBCHAIN);
  gst_object_unref (pad);

  gst_object_unref (element);

  g_object_unref (pipeline_monitor);
  gst_object_unref (pipeline);
  g_object_unref (runner);
}

GST_END_TEST;

static void
_create_issues (GstValidateRunner * runner)
{
  GstPad *srcpad1, *srcpad2, *sinkpad, *funnel_sink1, *funnel_sink2;
  GstElement *src1, *src2, *sink, *fakemixer;
  GstSegment segment;

  src1 = create_and_monitor_element ("fakesrc2", "fakesrc1", runner);
  src2 = create_and_monitor_element ("fakesrc2", "fakesrc2", runner);
  fakemixer = create_and_monitor_element ("fakemixer", "fakemixer", runner);
  sink = create_and_monitor_element ("fakesink", "fakesink", runner);

  srcpad1 = gst_element_get_static_pad (src1, "src");
  srcpad2 = gst_element_get_static_pad (src2, "src");
  funnel_sink1 = gst_element_get_request_pad (fakemixer, "sink_%u");
  funnel_sink2 = gst_element_get_request_pad (fakemixer, "sink_%u");
  sinkpad = gst_element_get_static_pad (sink, "sink");

  fail_unless (gst_element_link (fakemixer, sink));
  fail_unless (gst_pad_link (srcpad1, funnel_sink1) == GST_PAD_LINK_OK);
  fail_unless (gst_pad_link (srcpad2, funnel_sink2) == GST_PAD_LINK_OK);

  /* We want to handle the src behaviour ourselves */
  fail_unless (gst_pad_activate_mode (srcpad1, GST_PAD_MODE_PUSH, TRUE));
  fail_unless (gst_pad_activate_mode (srcpad2, GST_PAD_MODE_PUSH, TRUE));

  /* Setup all needed events */
  gst_segment_init (&segment, GST_FORMAT_TIME);
  segment.start = 0;
  segment.stop = GST_SECOND;

  fail_unless (gst_pad_push_event (srcpad1,
          gst_event_new_stream_start ("the-stream")));
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_segment (&segment)));

  fail_unless (gst_pad_push_event (srcpad2,
          gst_event_new_stream_start ("the-stream")));
  fail_unless (gst_pad_push_event (srcpad2, gst_event_new_segment (&segment)));

  fail_unless_equals_int (gst_element_set_state (fakemixer, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);

  /* Send an unexpected flush stop */
  _gst_check_expecting_log = TRUE;
  fail_unless (gst_pad_push_event (srcpad1, gst_event_new_flush_stop (TRUE)));

  /* Once again but on the other fakemixer sink */
  fail_unless (gst_pad_push_event (srcpad2, gst_event_new_flush_stop (TRUE)));

  /* clean up */
  fail_unless (gst_pad_activate_mode (srcpad1, GST_PAD_MODE_PUSH, FALSE));
  fail_unless (gst_pad_activate_mode (srcpad2, GST_PAD_MODE_PUSH, FALSE));
  fail_unless_equals_int (gst_element_set_state (fakemixer, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  fail_unless_equals_int (gst_element_set_state (sink, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (srcpad1);
  gst_object_unref (srcpad2);
  gst_object_unref (sinkpad);
  gst_object_unref (funnel_sink1);
  gst_object_unref (funnel_sink2);
  check_destroyed (fakemixer, funnel_sink1, funnel_sink2, NULL);
  check_destroyed (src1, srcpad1, NULL);
  check_destroyed (src2, srcpad2, NULL);
  check_destroyed (sink, sinkpad, NULL);
}

GST_START_TEST (test_global_levels)
{
  GstValidateRunner *runner;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "none", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* None shall pass */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 0);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "synthetic", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* Two reports of the same type */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 1);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "monitor", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* One report for each pad monitor */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 6);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* One report for each pad monitor, plus one for fakemixer src and fakesink sink */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 8);
  g_object_unref (runner);
}

GST_END_TEST;

GST_START_TEST (test_specific_levels)
{
  GstValidateRunner *runner;

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS",
          "none,fakesrc1:synthetic", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* One issue should go through the none filter */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 1);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "monitor,sink:none",
          TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* 5 issues because all pads will report their own issues separately, except
   * for the sink which will not report an issue */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 5);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS",
          "subchain,sink:monitor", TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* 3 issues because both fake sources will have subsequent subchains of
   * issues, and the sink will report its issue separately */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 3);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS",
          "synthetic,fakesrc1:subchain,fakesrc2:subchain,fakemixer*::src*:monitor",
          TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* 4 issues because the fakemixer sink issues will be concatenated with the
   * fakesrc issues, the fakemixer src will report its issue separately, and the
   * sink will not find a report immediately upstream */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 4);
  g_object_unref (runner);

  fail_unless (g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "none,fakesink*:all",
          TRUE));
  runner = gst_validate_runner_new ();
  _create_issues (runner);
  /* 2 issues repeated on the fakesink's sink */
  fail_unless_equals_int (gst_validate_runner_get_reports_count (runner), 2);
  g_object_unref (runner);
}

GST_END_TEST;

static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("reporting");
  TCase *tc_chain = tcase_create ("reporting");
  suite_add_tcase (s, tc_chain);

  gst_validate_init ();
  fake_elements_register ();

  tcase_add_test (tc_chain, test_report_levels);
  tcase_add_test (tc_chain, test_global_levels);
  tcase_add_test (tc_chain, test_specific_levels);

  gst_validate_deinit ();
  return s;
}

GST_CHECK_MAIN (gst_validate);
