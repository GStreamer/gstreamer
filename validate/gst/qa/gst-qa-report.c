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
#include "gst-qa-monitor.h"

static GstClockTime _gst_qa_report_start_time = 0;
static GstQaDebugFlags _gst_qa_flags = 0;

G_DEFINE_BOXED_TYPE (GstQaReport, gst_qa_report,
    (GBoxedCopyFunc) gst_qa_report_ref, (GBoxedFreeFunc) gst_qa_report_unref);

void
gst_qa_report_init (void)
{
  const gchar *var;
  const GDebugKey keys[] = {
    {"fatal_criticals", GST_QA_FATAL_CRITICALS},
    {"fatal_warnings", GST_QA_FATAL_WARNINGS},
    {"fatal_issues", GST_QA_FATAL_ISSUES}
  };

  if (_gst_qa_report_start_time == 0) {
    _gst_qa_report_start_time = gst_util_get_timestamp ();

    /* init the debug flags */
    var = g_getenv ("GST_QA");
    if (var && strlen (var) > 0) {
      _gst_qa_flags = g_parse_debug_string (var, keys, 3);
    }
  }
}


/* TODO how are these functions going to work with extensions */
const gchar *
gst_qa_report_level_get_name (GstQaReportLevel level)
{
  switch (level) {
    case GST_QA_REPORT_LEVEL_CRITICAL:
      return "critical";
    case GST_QA_REPORT_LEVEL_WARNING:
      return "warning";
    case GST_QA_REPORT_LEVEL_ISSUE:
      return "issue";
    default:
      return "unknown";
  }
}

const gchar *
gst_qa_report_area_get_name (GstQaReportArea area)
{
  switch (area) {
    case GST_QA_AREA_EVENT:
      return "event";
    case GST_QA_AREA_BUFFER:
      return "buffer";
    case GST_QA_AREA_QUERY:
      return "query";
    case GST_QA_AREA_CAPS_NEGOTIATION:
      return "caps";
    case GST_QA_AREA_OTHER:
      return "other";
    default:
      g_assert_not_reached ();
      return "unknown";
  }
}

const gchar *
gst_qa_area_event_get_subarea_name (GstQaReportAreaEvent subarea)
{
  switch (subarea) {
    case GST_QA_AREA_EVENT_SEQNUM:
      return "seqnum";
    case GST_QA_AREA_EVENT_UNEXPECTED:
      return "unexpected";
    case GST_QA_AREA_EVENT_EXPECTED:
      return "expected";
    default:
      return "unknown";
  }
}

const gchar *
gst_qa_area_buffer_get_subarea_name (GstQaReportAreaEvent subarea)
{
  switch (subarea) {
    case GST_QA_AREA_BUFFER_TIMESTAMP:
      return "timestamp";
    case GST_QA_AREA_BUFFER_DURATION:
      return "duration";
    case GST_QA_AREA_BUFFER_FLAGS:
      return "flags";
    case GST_QA_AREA_BUFFER_UNEXPECTED:
      return "unexpected";
    default:
      return "unknown";
  }
}

const gchar *
gst_qa_area_query_get_subarea_name (GstQaReportAreaEvent subarea)
{
  switch (subarea) {
    case GST_QA_AREA_QUERY_UNEXPECTED:
      return "unexpected";
    default:
      return "unknown";
  }
}

const gchar *
gst_qa_area_caps_get_subarea_name (GstQaReportAreaEvent subarea)
{
  switch (subarea) {
    case GST_QA_AREA_CAPS_NEGOTIATION:
      return "negotiation";
    default:
      return "unknown";
  }
}

const gchar *
gst_qa_report_subarea_get_name (GstQaReportArea area, gint subarea)
{
  switch (area) {
    case GST_QA_AREA_EVENT:
      return gst_qa_area_event_get_subarea_name (subarea);
    case GST_QA_AREA_BUFFER:
      return gst_qa_area_buffer_get_subarea_name (subarea);
    case GST_QA_AREA_QUERY:
      return gst_qa_area_query_get_subarea_name (subarea);
    case GST_QA_AREA_CAPS_NEGOTIATION:
      return gst_qa_area_caps_get_subarea_name (subarea);
    default:
      g_assert_not_reached ();
    case GST_QA_AREA_OTHER:
      return "unknown";
  }
}

static void
gst_qa_report_check_abort (GstQaReport * report)
{
  if ((report->level == GST_QA_REPORT_LEVEL_ISSUE &&
          _gst_qa_flags & GST_QA_FATAL_ISSUES) ||
      (report->level == GST_QA_REPORT_LEVEL_WARNING &&
          _gst_qa_flags & GST_QA_FATAL_WARNINGS) ||
      (report->level == GST_QA_REPORT_LEVEL_CRITICAL &&
          _gst_qa_flags & GST_QA_FATAL_CRITICALS)) {
    g_error ("Fatal report received: %" GST_QA_ERROR_REPORT_PRINT_FORMAT,
        GST_QA_REPORT_PRINT_ARGS (report));
  }
}

GstQaReport *
gst_qa_report_new (const gchar * source_name, GstQaReportLevel level,
    GstQaReportArea area, gint subarea, const gchar * id, const gchar * message)
{
  GstQaReport *report = g_slice_new0 (GstQaReport);

  report->level = level;
  report->area = area;
  report->subarea = subarea;
  report->source_name = g_strdup (source_name);
  report->message = g_strdup (message);
  report->id = g_strdup (id);
  report->timestamp = gst_util_get_timestamp () - _gst_qa_report_start_time;

  /* we might abort here if asked */
  gst_qa_report_check_abort (report);

  return report;
}

void
gst_qa_report_unref (GstQaReport * report)
{
  if (G_UNLIKELY (g_atomic_int_dec_and_test (&report->refcount))) {
    g_free (report->message);
    g_free (report->id);
    g_free (report->source_name);
    g_slice_free (GstQaReport, report);
  }
}

GstQaReport *
gst_qa_report_ref (GstQaReport * report)
{
  g_atomic_int_inc (&report->refcount);

  return report;
}

void
gst_qa_report_printf (GstQaReport * report)
{
  g_print ("%" GST_QA_ERROR_REPORT_PRINT_FORMAT "\n",
      GST_QA_REPORT_PRINT_ARGS (report));
}
