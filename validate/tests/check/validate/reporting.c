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
