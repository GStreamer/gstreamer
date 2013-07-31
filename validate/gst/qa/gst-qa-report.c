/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor-report.c - QA report/issues functions
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include "gst-qa-i18n-lib.h"

#include "gst-qa-report.h"
#include "gst-qa-reporter.h"
#include "gst-qa-monitor.h"

static GstClockTime _gst_qa_report_start_time = 0;
static GstQaDebugFlags _gst_qa_flags = 0;
static GHashTable *_gst_qa_issues = NULL;

G_DEFINE_BOXED_TYPE (GstQaReport, gst_qa_report,
    (GBoxedCopyFunc) gst_qa_report_ref, (GBoxedFreeFunc) gst_qa_report_unref);

GstQaIssueId
gst_qa_issue_get_id (GstQaIssue * issue)
{
  return issue->issue_id;
}

GstQaIssue *
gst_qa_issue_new (GstQaIssueId issue_id, gchar * summary,
    gchar * description, GstQaReportLevel default_level)
{
  GstQaIssue *issue = g_slice_new (GstQaIssue);

  issue->issue_id = issue_id;
  issue->summary = summary;
  issue->description = description;
  issue->default_level = default_level;
  issue->repeat = FALSE;

  return issue;
}

static void
gst_qa_issue_free (GstQaIssue * issue)
{
  g_free (issue->summary);
  g_free (issue->description);
  g_slice_free (GstQaIssue, issue);
}

void
gst_qa_issue_register (GstQaIssue * issue)
{
  g_return_if_fail (g_hash_table_lookup (_gst_qa_issues,
          (gpointer) gst_qa_issue_get_id (issue)) == NULL);

  g_hash_table_insert (_gst_qa_issues, (gpointer) gst_qa_issue_get_id (issue),
      issue);
}

#define REGISTER_QA_ISSUE(id,sum,desc,lvl) gst_qa_issue_register (gst_qa_issue_new (id, sum, desc, lvl))
static void
gst_qa_report_load_issues (void)
{
  g_return_if_fail (_gst_qa_issues == NULL);

  _gst_qa_issues = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_qa_issue_free);

  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_BUFFER_BEFORE_SEGMENT,
      _("buffer was received before a segment"),
      _("in push mode, a segment event must be received before a buffer"),
      GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_BUFFER_IS_OUT_OF_SEGMENT,
      _("buffer is out of the segment range"),
      _("buffer being pushed is out of the current segment's start-stop "
          " range. Meaning it is going to be discarded downstream without "
          "any use"), GST_QA_REPORT_LEVEL_ISSUE);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
      _("buffer timestamp is out of the received buffer timestamps' range"),
      _("a buffer leaving an element should have its timestamps in the range "
          "of the received buffers timestamps. i.e. If an element received "
          "buffers with timestamps from 0s to 10s, it can't push a buffer with "
          "with a 11s timestamp, because it doesn't have data for that"),
      GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO,
      _("first buffer's running time isn't 0"),
      _("the first buffer's received running time is expected to be 0"),
      GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_WRONG_FLOW_RETURN, _("flow return from pad push doesn't match expected value"), _("flow return from a 1:1 sink/src pad element is as simple as " "returning what downstream returned. For elements that have multiple " "src pads, flow returns should be properly combined"),     /* TODO fill me more */
      GST_QA_REPORT_LEVEL_CRITICAL);

  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_CAPS_IS_MISSING_FIELD,
      _("caps is missing a required field for its type"),
      _("some caps types are expected to contain a set of basic fields. "
          "For example, raw video should have 'width', 'height', 'framerate' "
          "and 'pixel-aspect-ratio'"), GST_QA_REPORT_LEVEL_ISSUE);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_CAPS_FIELD_HAS_BAD_TYPE,
      _("caps field has an unexpected type"),
      _("some common caps fields should always use the same expected types"),
      GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_CAPS_EXPECTED_FIELD_NOT_FOUND,
      _("caps expected field wasn't present"),
      _("a field that should be present in the caps wasn't found. "
          "Fields sets on a sink pad caps should be propagated downstream "
          "when it makes sense to do so"), GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_GET_CAPS_NOT_PROXYING_FIELDS,
      _("getcaps function isn't proxying downstream fields correctly"),
      _("elements should set downstream caps restrictions on its caps when "
          "replying upstream's getcaps queries to avoid upstream sending data"
          " in an unsupported format"), GST_QA_REPORT_LEVEL_CRITICAL);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_CAPS_FIELD_UNEXPECTED_VALUE,
      _("a field in caps has an unexpected value"),
      _("fields set on a sink pad should be propagated downstream via "
          "set caps"), GST_QA_REPORT_LEVEL_CRITICAL);

  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_NEWSEGMENT_NOT_PUSHED,
      _("new segment event wasn't propagated downstream"),
      _("segments received from upstream should be pushed downstream"),
      GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
      _("a serialized event received should be pushed in the same 'time' "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received and serialized with buffers. If an event is received after"
          " a buffer with timestamp end 'X', it should be pushed right after "
          "buffers with timestamp end 'X'"), GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_HAS_WRONG_SEQNUM,
      _("events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"), GST_QA_REPORT_LEVEL_ISSUE);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_SERIALIZED_OUT_OF_ORDER,
      _("a serialized event received should be pushed in the same order "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received."), GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_NEW_SEGMENT_MISMATCH,
      _("a new segment event has different value than the received one"),
      _("when receiving a new segment, an element should push an equivalent"
          "segment downstream"), GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_FLUSH_START_UNEXPECTED,
      _("received an unexpected flush start event"),
      NULL, GST_QA_REPORT_LEVEL_WARNING);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_FLUSH_STOP_UNEXPECTED,
      _("received an unexpected flush stop event"),
      NULL, GST_QA_REPORT_LEVEL_WARNING);

  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_SEEK_NOT_HANDLED,
      _("seek event wasn't handled"), NULL, GST_QA_REPORT_LEVEL_CRITICAL);
  REGISTER_QA_ISSUE (GST_QA_ISSUE_ID_EVENT_SEEK_RESULT_POSITION_WRONG,
      _("position after a seek is wrong"), NULL, GST_QA_REPORT_LEVEL_CRITICAL);
}

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

    gst_qa_report_load_issues ();
  }
}

GstQaIssue *
gst_qa_issue_from_id (GstQaIssueId issue_id)
{
  return g_hash_table_lookup (_gst_qa_issues, (gpointer) issue_id);
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
    case GST_QA_REPORT_LEVEL_IGNORE:
      return "ignore";
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
    case GST_QA_AREA_CAPS:
      return "caps";
    case GST_QA_AREA_SEEK:
      return "seek";
    case GST_QA_AREA_OTHER:
      return "other";
    default:
      g_assert_not_reached ();
      return "unknown";
  }
}

void
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

GstQaIssueId
gst_qa_report_get_issue_id (GstQaReport * report)
{
  return gst_qa_issue_get_id (report->issue);
}

GstQaReport *
gst_qa_report_new (GstQaIssue * issue, GstQaReporter * reporter,
    const gchar * message)
{
  GstQaReport *report = g_slice_new0 (GstQaReport);

  report->issue = issue;
  report->reporter = reporter;  /* TODO should we ref? */
  report->message = g_strdup (message);
  report->timestamp = gst_util_get_timestamp () - _gst_qa_report_start_time;
  report->level = issue->default_level;

  return report;
}

void
gst_qa_report_unref (GstQaReport * report)
{
  if (G_UNLIKELY (g_atomic_int_dec_and_test (&report->refcount))) {
    g_free (report->message);
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
