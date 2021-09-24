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

#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-pad-monitor.h>
#include <gst/validate/gst-validate-bin-monitor.h>
#include <gst/check/gstcheck.h>
#include "test-utils.h"

GST_START_TEST (monitors_added)
{
  GList *tmp;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GstElement *pipeline = gst_pipeline_new ("validate-pipeline");
  GstElement *src, *sink;

  src = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  runner = gst_validate_runner_new ();
  fail_unless (GST_IS_VALIDATE_RUNNER (runner));

  monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      runner, NULL);
  fail_unless (GST_IS_VALIDATE_BIN_MONITOR (monitor));

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  /* Check that the elements are properly monitored */
  fail_unless_equals_int (g_list_length (src->srcpads), 1);
  for (tmp = src->srcpads; tmp; tmp = tmp->next)
    fail_unless (GST_IS_VALIDATE_PAD_MONITOR (g_object_get_data ((GObject *)
                tmp->data, "validate-monitor")));

  fail_unless_equals_int (g_list_length (sink->sinkpads), 1);
  for (tmp = sink->sinkpads; tmp; tmp = tmp->next)
    fail_unless (GST_IS_VALIDATE_PAD_MONITOR (g_object_get_data ((GObject *)
                tmp->data, "validate-monitor")));

  /* clean up */
  gst_object_unref (pipeline);
  gst_object_unref (monitor);
  gst_object_unref (runner);
}

GST_END_TEST;

GST_START_TEST (monitors_cleanup)
{
  GstElement *src, *sink;
  GstValidateMonitor *monitor, *pmonitor1, *pmonitor2;

  GstValidateRunner *runner = gst_validate_runner_new ();
  GstElement *pipeline = gst_pipeline_new ("validate-pipeline");

  src = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      runner, NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  gst_element_link (src, sink);

  /* Check cleanup */
  pmonitor1 =
      g_object_get_data ((GObject *) src->srcpads->data, "validate-monitor");
  pmonitor2 =
      g_object_get_data ((GObject *) sink->sinkpads->data, "validate-monitor");
  gst_check_objects_destroyed_on_unref (monitor, pmonitor1, pmonitor2, NULL);
  gst_check_objects_destroyed_on_unref (pipeline, src, sink, NULL);
}

GST_END_TEST;


static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("monitoring");
  TCase *tc_chain = tcase_create ("monitoring");
  suite_add_tcase (s, tc_chain);

  if (atexit (gst_validate_deinit) != 0) {
    GST_ERROR ("failed to set gst_validate_deinit as exit function");
  }

  tcase_add_test (tc_chain, monitors_added);
  tcase_add_test (tc_chain, monitors_cleanup);

  return s;
}

GST_CHECK_MAIN (gst_validate);
