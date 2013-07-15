/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor-preload.c - QA Element monitors preload functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gst-qa-report.h"

static GstClockTime _gst_qa_report_start_time = 0;

void
gst_qa_report_init (void)
{
  if (_gst_qa_report_start_time == 0)
    _gst_qa_report_start_time = gst_util_get_timestamp ();
}

const gchar *
gst_qa_error_area_get_name (GstQaErrorArea area)
{
  switch (area) {
    case GST_QA_ERROR_AREA_EVENT:
      return "event";
    case GST_QA_ERROR_AREA_BUFFER:
      return "buffer";
    case GST_QA_ERROR_AREA_QUERY:
      return "query";
    case GST_QA_ERROR_AREA_OTHER:
      return "other";
    default:
      g_assert_not_reached ();
      return "unknown";
  }
}

GstQaErrorReport *
gst_qa_error_report_new (GstObject * source, GstQaErrorArea area,
    const gchar * message, const gchar * detail)
{
  GstQaErrorReport *report = g_slice_new0 (GstQaErrorReport);

  report->source = g_object_ref (source);
  report->area = area;
  report->message = g_strdup (message);
  report->detail = g_strdup (detail);
  report->timestamp = gst_util_get_timestamp () - _gst_qa_report_start_time;

  return report;
}

void
gst_qa_error_report_free (GstQaErrorReport * report)
{
  g_free (report->message);
  g_free (report->detail);
  g_object_unref (report->source);
  g_slice_free (GstQaErrorReport, report);
}

void
gst_qa_error_report_printf (GstQaErrorReport * report)
{
  g_print ("%" GST_QA_ERROR_REPORT_PRINT_FORMAT "\n",
      GST_QA_REPORT_PRINT_ARGS (report));
}
