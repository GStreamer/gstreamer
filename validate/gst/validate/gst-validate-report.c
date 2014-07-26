/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor-report.c - Validate report/issues functions
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

#include <stdio.h>              /* fprintf */
#include <glib/gstdio.h>
#include <errno.h>

#include <string.h>
#include "gst-validate-i18n-lib.h"
#include "gst-validate-internal.h"

#include "gst-validate-report.h"
#include "gst-validate-reporter.h"
#include "gst-validate-monitor.h"
#include "gst-validate-scenario.h"

static GstClockTime _gst_validate_report_start_time = 0;
static GstValidateDebugFlags _gst_validate_flags = 0;
static GHashTable *_gst_validate_issues = NULL;
static FILE *log_file;

#ifndef GST_DISABLE_GST_DEBUG
static GRegex *regex = NULL;
#endif

GST_DEBUG_CATEGORY_STATIC (gst_validate_report_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_report_debug

G_DEFINE_BOXED_TYPE (GstValidateReport, gst_validate_report,
    (GBoxedCopyFunc) gst_validate_report_ref,
    (GBoxedFreeFunc) gst_validate_report_unref);

GstValidateIssueId
gst_validate_issue_get_id (GstValidateIssue * issue)
{
  return issue->issue_id;
}

GstValidateIssue *
gst_validate_issue_new (GstValidateIssueId issue_id, const gchar * summary,
    const gchar * description, GstValidateReportLevel default_level)
{
  GstValidateIssue *issue = g_slice_new (GstValidateIssue);

  issue->issue_id = issue_id;
  issue->summary = g_strdup (summary);
  issue->description = g_strdup (description);
  issue->default_level = default_level;
  issue->repeat = FALSE;

  return issue;
}

static void
gst_validate_issue_free (GstValidateIssue * issue)
{
  g_free (issue->summary);
  g_free (issue->description);
  g_slice_free (GstValidateIssue, issue);
}

void
gst_validate_issue_register (GstValidateIssue * issue)
{
  g_return_if_fail (g_hash_table_lookup (_gst_validate_issues,
          (gpointer) gst_validate_issue_get_id (issue)) == NULL);

  g_hash_table_insert (_gst_validate_issues,
      (gpointer) gst_validate_issue_get_id (issue), issue);
}

#define REGISTER_VALIDATE_ISSUE(lvl,id,sum,desc)			\
  gst_validate_issue_register (gst_validate_issue_new (GST_VALIDATE_ISSUE_ID_##id, \
						       sum, desc, GST_VALIDATE_REPORT_LEVEL_##lvl))
static void
gst_validate_report_load_issues (void)
{
  g_return_if_fail (_gst_validate_issues == NULL);

  _gst_validate_issues = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_validate_issue_free);

  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_BEFORE_SEGMENT,
      _("buffer was received before a segment"),
      _("in push mode, a segment event must be received before a buffer"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_IS_OUT_OF_SEGMENT,
      _("buffer is out of the segment range"),
      _("buffer being pushed is out of the current segment's start-stop "
          " range. Meaning it is going to be discarded downstream without "
          "any use"));
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
      _("buffer timestamp is out of the received buffer timestamps' range"),
      _("a buffer leaving an element should have its timestamps in the range "
          "of the received buffers timestamps. i.e. If an element received "
          "buffers with timestamps from 0s to 10s, it can't push a buffer with "
          "with a 11s timestamp, because it doesn't have data for that"));
  REGISTER_VALIDATE_ISSUE (WARNING, FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO,
      _("first buffer's running time isn't 0"),
      _("the first buffer's received running time is expected to be 0"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, WRONG_FLOW_RETURN,
      _("flow return from pad push doesn't match expected value"),
      _("flow return from a 1:1 sink/src pad element is as simple as "
          "returning what downstream returned. For elements that have multiple "
          "src pads, flow returns should be properly combined"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_AFTER_EOS,
      _("buffer was received after EOS"),
      _("a pad shouldn't receive any more buffers after it gets EOS"));

  REGISTER_VALIDATE_ISSUE (ISSUE, CAPS_IS_MISSING_FIELD,
      _("caps is missing a required field for its type"),
      _("some caps types are expected to contain a set of basic fields. "
          "For example, raw video should have 'width', 'height', 'framerate' "
          "and 'pixel-aspect-ratio'"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_FIELD_HAS_BAD_TYPE,
      _("caps field has an unexpected type"),
      _("some common caps fields should always use the same expected types"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_EXPECTED_FIELD_NOT_FOUND,
      _("caps expected field wasn't present"),
      _("a field that should be present in the caps wasn't found. "
          "Fields sets on a sink pad caps should be propagated downstream "
          "when it makes sense to do so"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, GET_CAPS_NOT_PROXYING_FIELDS,
      _("getcaps function isn't proxying downstream fields correctly"),
      _("elements should set downstream caps restrictions on its caps when "
          "replying upstream's getcaps queries to avoid upstream sending data"
          " in an unsupported format"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, CAPS_FIELD_UNEXPECTED_VALUE,
      _("a field in caps has an unexpected value"),
      _("fields set on a sink pad should be propagated downstream via "
          "set caps"));

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEWSEGMENT_NOT_PUSHED,
      _("new segment event wasn't propagated downstream"),
      _("segments received from upstream should be pushed downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
      _("a serialized event received should be pushed in the same 'time' "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received and serialized with buffers. If an event is received after"
          " a buffer with timestamp end 'X', it should be pushed right after "
          "buffers with timestamp end 'X'"));
  REGISTER_VALIDATE_ISSUE (ISSUE, EVENT_HAS_WRONG_SEQNUM,
      _("events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_SERIALIZED_OUT_OF_ORDER,
      _("a serialized event received should be pushed in the same order "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received."));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEW_SEGMENT_MISMATCH,
      _("a new segment event has different value than the received one"),
      _("when receiving a new segment, an element should push an equivalent"
          "segment downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_START_UNEXPECTED,
      _("received an unexpected flush start event"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_STOP_UNEXPECTED,
      _("received an unexpected flush stop event"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_CAPS_DUPLICATE,
      _("received the same caps twice"), NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_NOT_HANDLED,
      _("seek event wasn't handled"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_RESULT_POSITION_WRONG,
      _("position after a seek is wrong"), NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, STATE_CHANGE_FAILURE,
      _("state change failed"), NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_SIZE_IS_ZERO,
      _("resulting file size is 0"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SIZE_INCORRECT,
      _("resulting file size wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_DURATION_INCORRECT,
      _("resulting file duration wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SEEKABLE_INCORRECT,
      _("resulting file wasn't seekable or not seekable as expected"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, FILE_TAG_DETECTION_INCORRECT,
      _("detected tags are different than expected ones"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PROFILE_INCORRECT,
      _("resulting file stream profiles didn't match expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_NOT_FOUND,
      _("resulting file could not be found for testing"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_CHECK_FAILURE,
      _("an error occured while checking the file for conformance"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PLAYBACK_START_FAILURE,
      _("an error occured while starting playback of the test file"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PLAYBACK_ERROR,
      _("an error during playback of the file"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_ID,
      _("the discoverer found a stream that had no stream ID"), NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, ALLOCATION_FAILURE,
      _("a memory allocation failed during Validate run"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, MISSING_PLUGIN,
      _("a gstreamer plugin is missing and prevented Validate from running"),
      NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, WARNING_ON_BUS,
      _("We got a WARNING message on the bus"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, ERROR_ON_BUS,
      _("We got an ERROR message on the bus"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_SUPERIOR_DURATION,
      _("Query position reported a value superior than what query duration "
          "returned"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_OUT_OF_SEGMENT,
      _("Query position reported a value outside of the current expected "
          "segment"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_NOT_ENDED,
      _("All the actions were not executed before the program stoped"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_ACTION_EXECUTION_ERROR,
      _("The execution of an action did not properly happen"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, SCENARIO_ACTION_EXECUTION_ISSUE,
      _("An issue happend during the execution of a scenario"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, G_LOG_WARNING, _("We got a g_log warning"),
      NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, G_LOG_CRITICAL,
      _("We got a g_log critical issue"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, G_LOG_ISSUE, _("We got a g_log issue"), NULL);
}

void
gst_validate_report_init (void)
{
  const gchar *var, *file_env;
  const GDebugKey keys[] = {
    {"fatal_criticals", GST_VALIDATE_FATAL_CRITICALS},
    {"fatal_warnings", GST_VALIDATE_FATAL_WARNINGS},
    {"fatal_issues", GST_VALIDATE_FATAL_ISSUES},
    {"print_issues", GST_VALIDATE_PRINT_ISSUES},
    {"print_warnings", GST_VALIDATE_PRINT_WARNINGS},
    {"print_criticals", GST_VALIDATE_PRINT_CRITICALS}
  };

  GST_DEBUG_CATEGORY_INIT (gst_validate_report_debug, "gstvalidatereport",
      GST_DEBUG_FG_YELLOW, "Gst validate reporting");

  if (_gst_validate_report_start_time == 0) {
    _gst_validate_report_start_time = gst_util_get_timestamp ();

    /* init the debug flags */
    var = g_getenv ("GST_VALIDATE");
    if (var && strlen (var) > 0) {
      _gst_validate_flags =
          g_parse_debug_string (var, keys, G_N_ELEMENTS (keys));
    }

    gst_validate_report_load_issues ();
  }

  file_env = g_getenv ("GST_VALIDATE_FILE");
  if (file_env != NULL && *file_env != '\0') {
    log_file = g_fopen (file_env, "w");
    if (log_file == NULL) {
      g_printerr ("Could not open log file '%s' for writing: %s\n", file_env,
          g_strerror (errno));
      log_file = stderr;
    }
  } else {
    log_file = stdout;
  }

#ifndef GST_DISABLE_GST_DEBUG
  regex = g_regex_new ("\n", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
#endif
}

GstValidateIssue *
gst_validate_issue_from_id (GstValidateIssueId issue_id)
{
  return g_hash_table_lookup (_gst_validate_issues, (gpointer) issue_id);
}

/* TODO how are these functions going to work with extensions */
const gchar *
gst_validate_report_level_get_name (GstValidateReportLevel level)
{
  switch (level) {
    case GST_VALIDATE_REPORT_LEVEL_CRITICAL:
      return "critical";
    case GST_VALIDATE_REPORT_LEVEL_WARNING:
      return "warning";
    case GST_VALIDATE_REPORT_LEVEL_ISSUE:
      return "issue";
    case GST_VALIDATE_REPORT_LEVEL_IGNORE:
      return "ignore";
    default:
      return "unknown";
  }
}

const gchar *
gst_validate_report_area_get_name (GstValidateReportArea area)
{
  switch (area) {
    case GST_VALIDATE_AREA_EVENT:
      return "event";
    case GST_VALIDATE_AREA_BUFFER:
      return "buffer";
    case GST_VALIDATE_AREA_QUERY:
      return "query";
    case GST_VALIDATE_AREA_CAPS:
      return "caps";
    case GST_VALIDATE_AREA_SEEK:
      return "seek";
    case GST_VALIDATE_AREA_STATE:
      return "state";
    case GST_VALIDATE_AREA_FILE_CHECK:
      return "file-check";
    case GST_VALIDATE_AREA_RUN_ERROR:
      return "run-error";
    case GST_VALIDATE_AREA_OTHER:
      return "other";
    case GST_VALIDATE_AREA_SCENARIO:
      return "scenario";
    default:
      g_assert_not_reached ();
      return "unknown";
  }
}

gboolean
gst_validate_report_should_print (GstValidateReport * report)
{
  if ((!(_gst_validate_flags & GST_VALIDATE_PRINT_ISSUES) &&
          !(_gst_validate_flags & GST_VALIDATE_PRINT_WARNINGS) &&
          !(_gst_validate_flags & GST_VALIDATE_PRINT_CRITICALS))) {
    return TRUE;
  }

  if ((report->level <= GST_VALIDATE_REPORT_LEVEL_ISSUE &&
          _gst_validate_flags & GST_VALIDATE_PRINT_ISSUES) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_WARNING &&
          _gst_validate_flags & GST_VALIDATE_PRINT_WARNINGS) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_CRITICAL &&
          _gst_validate_flags & GST_VALIDATE_PRINT_CRITICALS)) {

    return TRUE;
  }

  return FALSE;
}

gboolean
gst_validate_report_check_abort (GstValidateReport * report)
{
  if ((report->level <= GST_VALIDATE_REPORT_LEVEL_ISSUE &&
          _gst_validate_flags & GST_VALIDATE_FATAL_ISSUES) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_WARNING &&
          _gst_validate_flags & GST_VALIDATE_FATAL_WARNINGS) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_CRITICAL &&
          _gst_validate_flags & GST_VALIDATE_FATAL_CRITICALS)) {

    return TRUE;
  }

  return FALSE;
}

GstValidateIssueId
gst_validate_report_get_issue_id (GstValidateReport * report)
{
  return gst_validate_issue_get_id (report->issue);
}

GstValidateReport *
gst_validate_report_new (GstValidateIssue * issue,
    GstValidateReporter * reporter, const gchar * message)
{
  GstValidateReport *report = g_slice_new0 (GstValidateReport);

  report->refcount = 1;
  report->issue = issue;
  report->reporter = reporter;  /* TODO should we ref? */
  report->message = g_strdup (message);
  report->timestamp =
      gst_util_get_timestamp () - _gst_validate_report_start_time;
  report->level = issue->default_level;

  return report;
}

void
gst_validate_report_unref (GstValidateReport * report)
{
  if (G_UNLIKELY (g_atomic_int_dec_and_test (&report->refcount))) {
    g_free (report->message);
    g_slice_free (GstValidateReport, report);
  }
}

GstValidateReport *
gst_validate_report_ref (GstValidateReport * report)
{
  g_atomic_int_inc (&report->refcount);

  return report;
}

void
gst_validate_printf (gpointer source, const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_validate_printf_valist (source, format, var_args);
  va_end (var_args);
}

void
gst_validate_printf_valist (gpointer source, const gchar * format, va_list args)
{
  GString *string = g_string_new (NULL);

  if (source) {
    if (*(GType *) source == GST_TYPE_VALIDATE_ACTION) {
      GstValidateAction *action = (GstValidateAction *) source;

      g_string_printf (string,
          "\n(Executing action: %s, number: %u at position: %" GST_TIME_FORMAT
          " repeat: %i) | ", g_strcmp0 (action->name,
              "") == 0 ? "Unnamed" : action->name, action->action_number,
          GST_TIME_ARGS (action->playback_time), action->repeat);
    } else if (GST_IS_OBJECT (source)) {
      g_string_printf (string, "\n%s --> ", GST_OBJECT_NAME (source));
    } else if (G_IS_OBJECT (source)) {
      g_string_printf (string, "\n<%s@%p> --> ", G_OBJECT_TYPE_NAME (source),
          source);
    }
  }

  g_string_append_vprintf (string, format, args);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *str = g_regex_replace (regex, string->str, string->len, 0,
        "", 0, NULL);

    if (source)
      GST_INFO ("%s", str);
    else
      GST_DEBUG ("%s", str);

    g_free (str);
  }
#endif

  fprintf (log_file, "%s", string->str);
  fflush (log_file);

  g_string_free (string, TRUE);
}

void
gst_validate_report_printf (GstValidateReport * report)
{
  gst_validate_printf (NULL, "%10s : %s\n",
      gst_validate_report_level_get_name (report->level),
      report->issue->summary);
  gst_validate_printf (NULL, "%*s Detected on <%s> at %" GST_TIME_FORMAT "\n",
      12, "", gst_validate_reporter_get_name (report->reporter),
      GST_TIME_ARGS (report->timestamp));
  if (report->message)
    gst_validate_printf (NULL, "%*s Details : %s\n", 12, "", report->message);
  if (report->issue->description)
    gst_validate_printf (NULL, "%*s Description : %s\n", 12, "",
        report->issue->description);
  gst_validate_printf (NULL, "\n");
}
