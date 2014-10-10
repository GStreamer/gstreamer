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
  fail_unless (g_setenv ("GST_VALIDATE_REPORT_LEVEL", "AlL", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_default_reporting_level (runner) ==
      GST_VALIDATE_REPORTING_LEVEL_ALL);
  g_object_unref (runner);

  /* Try to set the default reporting level to subchain, the code is supposed to
   * parse numbers as well */
  fail_unless (g_setenv ("GST_VALIDATE_REPORT_LEVEL", "2", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_default_reporting_level (runner) ==
      GST_VALIDATE_REPORTING_LEVEL_SYNTHETIC);
  g_object_unref (runner);

  /* Try to set the reporting level for an object */
  fail_unless (g_setenv ("GST_VALIDATE_REPORT_LEVEL",
          "synthetic,test_object:monitor,other_*:all", TRUE));
  runner = gst_validate_runner_new ();
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "test_object") == GST_VALIDATE_REPORTING_LEVEL_MONITOR);
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "other_test_object") == GST_VALIDATE_REPORTING_LEVEL_ALL);
  fail_unless (gst_validate_runner_get_reporting_level_for_name (runner,
          "dummy_test_object") == GST_VALIDATE_REPORTING_LEVEL_UNKNOWN);

  g_object_unref (runner);

  /* Now let's try to see if the created monitors actually understand the
   * situation they've put themselves into */
  fail_unless (g_setenv ("GST_VALIDATE_REPORT_LEVEL",
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
          (monitor)) == GST_VALIDATE_REPORTING_LEVEL_ALL);

  pad = gst_element_get_static_pad (element, "src");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (pad),
      "validate-monitor");
  /* The pad should have inherited the reporting level */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_REPORTING_LEVEL_ALL);
  gst_object_unref (pad);

  gst_object_unref (element);

  element = gst_bin_get_by_name (GST_BIN (pipeline), "sofake2");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (element),
      "validate-monitor");
  /* The element should have inherited its reporting level from the pipeline */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_REPORTING_LEVEL_MONITOR);

  pad = gst_element_get_static_pad (element, "sink");
  monitor =
      (GstValidateMonitor *) g_object_get_data (G_OBJECT (pad),
      "validate-monitor");
  /* But its pad should not as it falls in the sofake*::sink pattern */
  fail_unless (gst_validate_reporter_get_reporting_level (GST_VALIDATE_REPORTER
          (monitor)) == GST_VALIDATE_REPORTING_LEVEL_SUBCHAIN);
  gst_object_unref (pad);

  gst_object_unref (element);

  g_object_unref (pipeline_monitor);
  gst_object_unref (pipeline);
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

  tcase_add_test (tc_chain, test_report_levels);

  return s;
}

GST_CHECK_MAIN (gst_validate);
