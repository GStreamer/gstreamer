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

/**
 * SECTION:gstvalidatereport
 * @title: GstValidateReport
 * @short_description: A Validate report
 * @see_also: #GstValidateRunner
 *
 */

#include <stdlib.h>             /* exit */
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
static GstValidateDebugFlags _gst_validate_flags =
    GST_VALIDATE_FATAL_CRITICALS | GST_VALIDATE_PRINT_ISSUES;
static GHashTable *_gst_validate_issues = NULL;
static FILE **log_files = NULL;
static gboolean output_is_tty = TRUE;

/* Tcp server for communications with gst-validate-launcher */
GSocketClient *socket_client = NULL;
GSocketConnection *server_connection = NULL;
GOutputStream *server_ostream = NULL;

static GType _gst_validate_report_type = 0;

static JsonNode *
gst_validate_report_serialize (GstValidateReport * report)
{
  JsonNode *node = json_node_alloc ();
  JsonObject *jreport = json_object_new ();

  json_object_set_string_member (jreport, "type", "report");
  json_object_set_string_member (jreport, "issue-id",
      g_quark_to_string (report->issue->issue_id));
  json_object_set_string_member (jreport, "summary", report->issue->summary);
  json_object_set_string_member (jreport, "level",
      gst_validate_report_level_get_name (report->level));
  json_object_set_string_member (jreport, "detected-on", report->reporter_name);
  json_object_set_string_member (jreport, "details", report->message);

  node = json_node_init_object (node, jreport);
  json_object_unref (jreport);

  return node;
}

GType
gst_validate_report_get_type (void)
{
  if (_gst_validate_report_type == 0) {
    _gst_validate_report_type =
        g_boxed_type_register_static (g_intern_static_string
        ("GstValidateReport"), (GBoxedCopyFunc) gst_mini_object_ref,
        (GBoxedFreeFunc) gst_mini_object_unref);

    json_boxed_register_serialize_func (_gst_validate_report_type,
        JSON_NODE_OBJECT,
        (JsonBoxedSerializeFunc) gst_validate_report_serialize);
  }

  return _gst_validate_report_type;
}


GRegex *newline_regex = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_validate_report_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_report_debug

#define GST_VALIDATE_REPORT_SHADOW_REPORTS_LOCK(r)			\
  G_STMT_START {					\
  (g_mutex_lock (&((GstValidateReport *) r)->shadow_reports_lock));		\
  } G_STMT_END

#define GST_VALIDATE_REPORT_SHADOW_REPORTS_UNLOCK(r)			\
  G_STMT_START {					\
  (g_mutex_unlock (&((GstValidateReport *) r)->shadow_reports_lock));		\
  } G_STMT_END


static GstValidateIssue *
gst_validate_issue_ref (GstValidateIssue * issue)
{
  g_return_val_if_fail (issue != NULL, NULL);

  g_atomic_int_inc (&issue->refcount);

  return issue;
}

static void
gst_validate_issue_unref (GstValidateIssue * issue)
{
  if (G_UNLIKELY (g_atomic_int_dec_and_test (&issue->refcount))) {
    g_free (issue->summary);
    g_free (issue->description);

    /* We are using an string array for area and name */
    g_strfreev (&issue->area);

    g_free (issue);
  }
}


G_DEFINE_BOXED_TYPE (GstValidateIssue, gst_validate_issue,
    (GBoxedCopyFunc) gst_validate_issue_ref,
    (GBoxedFreeFunc) gst_validate_issue_unref);

guint32
gst_validate_issue_get_id (GstValidateIssue * issue)
{
  return issue->issue_id;
}

#define MAKE_GETTER_COPY(type, field, copier) \
  type gst_validate_report_get_##field(GstValidateReport *report) { return copier(report->field); }

#define MAKE_GETTER(type, field) \
  type gst_validate_report_get_##field(GstValidateReport *report) { return report->field; }

/**
 * gst_validate_report_get_level:
 *
 * Returns: report level
 * Since: 1.22
 */
MAKE_GETTER (GstValidateReportLevel, level);
/**
 * gst_validate_report_get_timestamp:
 *
 * Returns: report timestamp
 * Since: 1.22
 */
MAKE_GETTER (GstClockTime, timestamp);
/**
 * gst_validate_report_get_reporting_level:
 *
 * Returns: reporting level
 * Since: 1.22
 */
MAKE_GETTER (GstValidateReportingDetails, reporting_level);
/**
 * gst_validate_report_get_issue:
 *
 * Returns: (transfer full): report issue
 * Since: 1.22
 */
MAKE_GETTER_COPY (GstValidateIssue *, issue, gst_validate_issue_ref);
/**
 * gst_validate_report_get_reporter:
 *
 * Returns: (transfer full): report reporter
 * Since: 1.22
 */
MAKE_GETTER_COPY (GstValidateReporter *, reporter, gst_object_ref);
/**
 * gst_validate_report_get_message:
 *
 * Returns: (transfer full): report message
 * Since: 1.22
 */
MAKE_GETTER_COPY (gchar *, message, g_strdup);
/**
 * gst_validate_report_get_reporter_name:
 *
 * Returns: (transfer full): report issue
 * Since: 1.22
 */
MAKE_GETTER_COPY (gchar *, reporter_name, g_strdup);
/**
 * gst_validate_report_get_trace:
 *
 * Returns: (transfer full) (nullable): report backtrace
 * Since: 1.22
 */
MAKE_GETTER_COPY (gchar *, trace, g_strdup);
/**
 * gst_validate_report_get_dotfile_name:
 *
 * Returns: (transfer full) (nullable): report dot file name
 * Since: 1.22
 */
MAKE_GETTER_COPY (gchar *, dotfile_name, g_strdup);

#undef MAKE_GETTER
#undef MAKE_GETTER_COPY

/**
 * gst_validate_issue_new_full:
 * @issue_id: The ID of the issue, should be a GQuark
 * @summary: A summary of the issue
 * @description: A more complete description of the issue
 * @default_level: The level at which the issue will be reported by default
 * @flags: The flags to determine behaviour of the issue
 *
 * Returns: (transfer full): The newly created #GstValidateIssue
 */
GstValidateIssue *
gst_validate_issue_new_full (GstValidateIssueId issue_id, const gchar * summary,
    const gchar * description, GstValidateReportLevel default_level,
    GstValidateIssueFlags flags)
{
  GstValidateIssue *issue;
  gchar **area_name = g_strsplit (g_quark_to_string (issue_id), "::", 2);

  if (!(area_name[0] != NULL && area_name[1] != NULL && area_name[2] == NULL)) {
    g_warning ("Wrong issue ID: %s (should be in the form: area::name)",
        g_quark_to_string (issue_id));
    g_strfreev (area_name);

    return NULL;
  }

  issue = g_new (GstValidateIssue, 1);
  issue->issue_id = issue_id;
  issue->summary = g_strdup (summary);
  issue->description = g_strdup (description);
  issue->default_level = default_level;
  issue->area = area_name[0];
  issue->name = area_name[1];
  issue->flags = flags;

  g_free (area_name);
  return issue;
}

/**
 * gst_validate_issue_new:
 * @issue_id: The ID of the issue, should be a GQuark
 * @summary: A summary of the issue
 * @description: A more complete description of the issue
 * @default_level: The level at which the issue will be reported by default
 *
 * Returns: (transfer full): The newly created #GstValidateIssue
 */
GstValidateIssue *
gst_validate_issue_new (GstValidateIssueId issue_id, const gchar * summary,
    const gchar * description, GstValidateReportLevel default_level)
{
  GstValidateIssue *issue;
  gchar **area_name = g_strsplit (g_quark_to_string (issue_id), "::", 2);

  if (!(area_name[0] != NULL && area_name[1] != NULL && area_name[2] == NULL)) {
    g_warning ("Wrong issue ID: %s (should be in the form: area::name)",
        g_quark_to_string (issue_id));
    g_strfreev (area_name);

    return NULL;
  }

  issue = g_new (GstValidateIssue, 1);
  issue->issue_id = issue_id;
  issue->summary = g_strdup (summary);
  issue->description = g_strdup (description);
  issue->default_level = default_level;
  issue->area = area_name[0];
  issue->name = area_name[1];
  issue->flags = GST_VALIDATE_ISSUE_FLAGS_NONE;

  g_free (area_name);
  return issue;
}

void
gst_validate_issue_set_default_level (GstValidateIssue * issue,
    GstValidateReportLevel default_level)
{
  GST_INFO ("Setting issue %s::%s default level to %s",
      issue->area, issue->name,
      gst_validate_report_level_get_name (default_level));

  issue->default_level = default_level;
}

/**
 * gst_validate_issue_register:
 * @issue: (transfer none): The #GstValidateIssue to register
 *
 * Registers @issue in the issue type system
 */
void
gst_validate_issue_register (GstValidateIssue * issue)
{
  g_return_if_fail (g_hash_table_lookup (_gst_validate_issues,
          GINT_TO_POINTER (gst_validate_issue_get_id (issue))) == NULL);

  g_hash_table_insert (_gst_validate_issues,
      GINT_TO_POINTER (gst_validate_issue_get_id (issue)), issue);
}

#define REGISTER_VALIDATE_ISSUE(lvl,id,sum,desc)			\
  gst_validate_issue_register (gst_validate_issue_new (id, \
						       sum, desc, GST_VALIDATE_REPORT_LEVEL_##lvl))

#define REGISTER_VALIDATE_ISSUE_FULL(lvl,id,sum,desc,flags)			\
  gst_validate_issue_register (gst_validate_issue_new_full (id, \
						       sum, desc, GST_VALIDATE_REPORT_LEVEL_##lvl, flags))
static void
gst_validate_report_load_issues (void)
{
  g_return_if_fail (_gst_validate_issues == NULL);

  _gst_validate_issues = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_validate_issue_unref);

  /* **
   * WARNING: The `summary` is used to define known issues in the testsuites.
   * Avoid changing them or **make sure** to at least update the validate test
   * suite if you do so.
   * **/
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_BEFORE_SEGMENT,
      "buffer was received before a segment",
      _("in push mode, a segment event must be received before a buffer"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_IS_OUT_OF_SEGMENT,
      "buffer is out of the segment range",
      _("buffer being pushed is out of the current segment's start-stop "
          "range. Meaning it is going to be discarded downstream without "
          "any use"));
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
      "buffer timestamp is out of the received buffer timestamps' range",
      _("a buffer leaving an element should have its timestamps in the range "
          "of the received buffers timestamps. i.e. If an element received "
          "buffers with timestamps from 0s to 10s, it can't push a buffer with "
          "a 11s timestamp, because it doesn't have data for that"));
  REGISTER_VALIDATE_ISSUE (WARNING, WRONG_BUFFER,
      "Received buffer does not correspond to wanted one.",
      _("When checking playback of a file against a MediaInfo file"
          " all buffers coming into the decoders might be checked"
          " and should have the exact expected metadatas and hash of the"
          " content"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, WRONG_FLOW_RETURN,
      "flow return from pad push doesn't match expected value",
      _("flow return from a 1:1 sink/src pad element is as simple as "
          "returning what downstream returned. For elements that have multiple "
          "src pads, flow returns should be properly combined"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_AFTER_EOS,
      "buffer was received after EOS",
      _("a pad shouldn't receive any more buffers after it gets EOS"));
  REGISTER_VALIDATE_ISSUE (WARNING, FLOW_ERROR_WITHOUT_ERROR_MESSAGE,
      "GST_FLOW_ERROR returned without posting an ERROR on the bus",
      _("Element MUST post a GST_MESSAGE_ERROR with GST_ELEMENT_ERROR before"
          " returning GST_FLOW_ERROR"));
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_MISSING_DISCONT,
      _("Buffer didn't have expected DISCONT flag"),
      _("Buffers after SEGMENT and FLUSH must have a DISCONT flag"));

  REGISTER_VALIDATE_ISSUE (ISSUE, CAPS_IS_MISSING_FIELD,
      "caps is missing a required field for its type",
      _("some caps types are expected to contain a set of basic fields. "
          "For example, raw video should have 'width', 'height', 'framerate' "
          "and 'pixel-aspect-ratio'"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_FIELD_HAS_BAD_TYPE,
      "caps field has an unexpected type",
      _("some common caps fields should always use the same expected types"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_EXPECTED_FIELD_NOT_FOUND,
      "caps expected field wasn't present",
      _("a field that should be present in the caps wasn't found. "
          "Fields sets on a sink pad caps should be propagated downstream "
          "when it makes sense to do so"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, GET_CAPS_NOT_PROXYING_FIELDS,
      "getcaps function isn't proxying downstream fields correctly",
      _("elements should set downstream caps restrictions on its caps when "
          "replying upstream's getcaps queries to avoid upstream sending data"
          " in an unsupported format"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, CAPS_FIELD_UNEXPECTED_VALUE,
      "a field in caps has an unexpected value",
      _("fields set on a sink pad should be propagated downstream via "
          "set caps"));

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEWSEGMENT_NOT_PUSHED,
      "new segment event wasn't propagated downstream",
      _("segments received from upstream should be pushed downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
      "a serialized event received should be pushed in the same 'time' "
      "as it was received",
      _("serialized events should be pushed in the same order they are "
          "received and serialized with buffers. If an event is received after"
          " a buffer with timestamp end 'X', it should be pushed right after "
          "buffers with timestamp end 'X'"));
  REGISTER_VALIDATE_ISSUE (ISSUE, EOS_HAS_WRONG_SEQNUM,
      "EOS events that are part of the same pipeline 'operation' should "
      "have the same seqnum",
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, FLUSH_START_HAS_WRONG_SEQNUM,
      "FLUSH_START events that are part of the same pipeline 'operation' should "
      "have the same seqnum",
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, FLUSH_STOP_HAS_WRONG_SEQNUM,
      "FLUSH_STOP events that are part of the same pipeline 'operation' should "
      "have the same seqnum",
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, SEGMENT_HAS_WRONG_SEQNUM,
      "SEGMENT events that are part of the same pipeline 'operation' should "
      "have the same seqnum",
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, SEGMENT_HAS_WRONG_START,
      "A segment doesn't have the proper time value after an ACCURATE seek",
      _("If a seek with the ACCURATE flag was accepted, the following segment "
          "should have a time value corresponding exactly to the requested start "
          "seek time"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_SERIALIZED_OUT_OF_ORDER,
      "a serialized event received should be pushed in the same order "
      "as it was received",
      _("serialized events should be pushed in the same order they are "
          "received."));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEW_SEGMENT_MISMATCH,
      "a new segment event has different value than the received one",
      _("when receiving a new segment, an element should push an equivalent "
          "segment downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_START_UNEXPECTED,
      "received an unexpected flush start event", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_STOP_UNEXPECTED,
      "received an unexpected flush stop event", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_CAPS_DUPLICATE,
      "received the same caps twice", NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_NOT_HANDLED,
      "seek event wasn't handled", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_RESULT_POSITION_WRONG,
      "position after a seek is wrong", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_INVALID_SEQNUM,
      "segments after a seek don't have the same seqnum", NULL);

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_EOS_WITHOUT_SEGMENT,
      "EOS received without segment event before",
      _("A segment event should always be sent before data flow"
          " EOS being some kind of data flow, there is no exception"
          " in that regard"));

  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_INVALID_SEQNUM,
      "Event has an invalid seqnum",
      _("An event is using GST_SEQNUM_INVALID. This should never happen"));

  REGISTER_VALIDATE_ISSUE (CRITICAL, STATE_CHANGE_FAILURE,
      "state change failed", NULL);

  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SIZE_INCORRECT,
      "resulting file size wasn't within the expected values", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_DURATION_INCORRECT,
      "resulting file duration wasn't within the expected values", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SEEKABLE_INCORRECT,
      "resulting file wasn't seekable or not seekable as expected", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PROFILE_INCORRECT,
      "resulting file stream profiles didn't match expected values", NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, FILE_TAG_DETECTION_INCORRECT,
      "detected tags are different than expected ones", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_FRAMES_INCORRECT,
      "resulting file frames are not as expected", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_SEGMENT_INCORRECT,
      "resulting segment is not as expected", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_INFO,
      "the discoverer could not determine the stream info", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_ID,
      "the discoverer found a stream that had no stream ID", NULL);


  REGISTER_VALIDATE_ISSUE (CRITICAL, ALLOCATION_FAILURE,
      "a memory allocation failed during Validate run", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, MISSING_PLUGIN,
      "a gstreamer plugin is missing and prevented Validate from running",
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, NOT_NEGOTIATED,
      "a NOT NEGOTIATED message has been posted on the bus.", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, WARNING_ON_BUS,
      "We got a WARNING message on the bus", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, ERROR_ON_BUS,
      "We got an ERROR message on the bus", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_SUPERIOR_DURATION,
      "Query position reported a value superior than what query duration "
      "returned", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_OUT_OF_SEGMENT,
      "Query position reported a value outside of the current expected "
      "segment", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_NOT_ENDED,
      "The program stopped before some actions were executed", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_ACTION_TIMEOUT,
      "The execution of an action timed out", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_FILE_MALFORMED,
      "The scenario file was malformed", NULL);
  REGISTER_VALIDATE_ISSUE_FULL (CRITICAL, SCENARIO_ACTION_EXECUTION_ERROR,
      "The execution of an action did not properly happen", NULL,
      GST_VALIDATE_ISSUE_FLAGS_NO_BACKTRACE |
      GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS);
  REGISTER_VALIDATE_ISSUE_FULL (CRITICAL, SCENARIO_ACTION_CHECK_ERROR,
      "A check action failed", NULL,
      GST_VALIDATE_ISSUE_FLAGS_NO_BACKTRACE |
      GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS);
  REGISTER_VALIDATE_ISSUE (ISSUE, SCENARIO_ACTION_EXECUTION_ISSUE,
      "An issue happened during the execution of a scenario", NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, SCENARIO_ACTION_ENDED_EARLY,
      "Got EOS before an action playback time", NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, CONFIG_LATENCY_TOO_HIGH,
      "The pipeline latency is higher than the maximum allowed by the scenario",
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, CONFIG_TOO_MANY_BUFFERS_DROPPED,
      "The number of dropped buffers is higher than the maximum allowed by the scenario",
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, CONFIG_BUFFER_FREQUENCY_TOO_LOW,
      _
      ("Pad buffers push frequency is lower than the minimum required by the config"),
      NULL);
  REGISTER_VALIDATE_ISSUE_FULL (WARNING, G_LOG_WARNING,
      _("We got a g_log warning"), NULL,
      GST_VALIDATE_ISSUE_FLAGS_FORCE_BACKTRACE |
      GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS);
  REGISTER_VALIDATE_ISSUE_FULL (CRITICAL, G_LOG_CRITICAL,
      "We got a g_log critical issue", NULL,
      GST_VALIDATE_ISSUE_FLAGS_FORCE_BACKTRACE |
      GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS);
  REGISTER_VALIDATE_ISSUE_FULL (ISSUE, G_LOG_ISSUE, "We got a g_log issue",
      NULL,
      GST_VALIDATE_ISSUE_FLAGS_FORCE_BACKTRACE |
      GST_VALIDATE_ISSUE_FLAGS_FULL_DETAILS);

  REGISTER_VALIDATE_ISSUE (CRITICAL, PULL_RANGE_FROM_WRONG_THREAD,
      "gst_pad_pull_range called from wrong thread",
      _("gst_pad_pull_range has to be called from the sinkpad task thread."));
}

gboolean
gst_validate_send (JsonNode * root)
{
  gboolean res = FALSE;
  JsonGenerator *jgen;
  gsize message_length;
  gchar *object, *message;
  GError *error = NULL;

  if (!server_ostream)
    goto done;

  jgen = json_generator_new ();
  json_generator_set_root (jgen, root);

  object = json_generator_to_data (jgen, &message_length);
  message = g_malloc0 (message_length + 5);
  GST_WRITE_UINT32_BE (message, message_length);
  strcpy (&message[4], object);
  g_free (object);

  res = g_output_stream_write_all (server_ostream, message, message_length + 4,
      NULL, NULL, &error);

  if (!res) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PENDING)) {
      GST_DEBUG ("Stream was busy, trying again later.");

      g_free (message);
      g_object_unref (jgen);
      if (error)
        g_error_free (error);
      g_idle_add ((GSourceFunc) gst_validate_send, root);
      return G_SOURCE_REMOVE;
    }

    GST_ERROR ("ERROR: Can't write to remote: %s", error->message);
  } else if (!g_output_stream_flush (server_ostream, NULL, &error)) {
    GST_ERROR ("ERROR: Can't flush stream: %s", error->message);
  }

  g_free (message);
  g_object_unref (jgen);
  if (error)
    g_error_free (error);

done:
  json_node_free (root);

  return G_SOURCE_REMOVE;
}

void
gst_validate_report_init (void)
{
  const gchar *var, *file_env, *server_env, *uuid;
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

  _gst_validate_report_type = gst_validate_report_get_type ();

  if (_gst_validate_report_start_time == 0) {
    _gst_validate_report_start_time = gst_util_get_timestamp ();

    /* init the debug flags */
    var = g_getenv ("GST_VALIDATE");
    if (var) {
      _gst_validate_flags =
          g_parse_debug_string (var, keys, G_N_ELEMENTS (keys));
    }

    gst_validate_report_load_issues ();
  }
#ifdef HAVE_UNISTD_H
  output_is_tty = isatty (1);
#endif

  server_env = g_getenv ("GST_VALIDATE_SERVER");
  uuid = g_getenv ("GST_VALIDATE_UUID");

  if (server_env && !uuid) {
    GST_INFO ("No GST_VALIDATE_UUID specified !");
  } else if (server_env) {
    GstUri *server_uri = gst_uri_from_string (server_env);

    if (server_uri && !g_strcmp0 (gst_uri_get_scheme (server_uri), "tcp")) {
      JsonBuilder *jbuilder;
      GError *err = NULL;
      socket_client = g_socket_client_new ();

      server_connection = g_socket_client_connect_to_host (socket_client,
          gst_uri_get_host (server_uri), gst_uri_get_port (server_uri),
          NULL, &err);

      if (!server_connection) {
        g_clear_error (&err);
        g_clear_object (&socket_client);

      } else {
        server_ostream =
            g_io_stream_get_output_stream (G_IO_STREAM (server_connection));
        jbuilder = json_builder_new ();
        json_builder_begin_object (jbuilder);
        json_builder_set_member_name (jbuilder, "uuid");
        json_builder_add_string_value (jbuilder, uuid);
        json_builder_set_member_name (jbuilder, "started");
        json_builder_add_boolean_value (jbuilder, TRUE);
        json_builder_end_object (jbuilder);

        gst_validate_send (json_builder_get_root (jbuilder));
        g_object_unref (jbuilder);
      }

      gst_uri_unref (server_uri);
    } else {
      GST_ERROR ("Server URI not valid: %s", server_env);
    }
  }

  file_env = g_getenv ("GST_VALIDATE_FILE");
  if (file_env != NULL && *file_env != '\0') {
    gint i;
    gchar **wanted_files;
    wanted_files = g_strsplit (file_env, G_SEARCHPATH_SEPARATOR_S, 0);

    /* FIXME: Make sure it is freed in the deinit function when that is
     * implemented */
    log_files =
        g_malloc0 (sizeof (FILE *) * (g_strv_length (wanted_files) + 1));
    for (i = 0; i < g_strv_length (wanted_files); i++) {
      FILE *log_file;
      if (g_strcmp0 (wanted_files[i], "stderr") == 0) {
        log_file = stderr;
      } else if (g_strcmp0 (wanted_files[i], "stdout") == 0) {
        log_file = stdout;
      } else {
        log_file = g_fopen (wanted_files[i], "w");
      }

      if (log_file == NULL) {
        g_printerr ("Could not open log file '%s' for writing: %s\n", file_env,
            g_strerror (errno));
        log_file = stderr;
      }

      log_files[i] = log_file;
    }

    g_strfreev (wanted_files);
  } else {
    log_files = g_malloc0 (sizeof (FILE *) * 2);
    log_files[0] = stdout;
  }

#ifndef GST_DISABLE_GST_DEBUG
  if (!newline_regex)
    newline_regex =
        g_regex_new ("\n", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
#endif
}

void
gst_validate_report_deinit (void)
{
  if (server_ostream) {
    g_output_stream_close (server_ostream, NULL, NULL);
    server_ostream = NULL;
  }

  g_clear_object (&socket_client);
  g_clear_object (&server_connection);
}

/**
 * gst_validate_issue_from_id:
 * @issue_id: The issue id
 *
 * Returns: (nullable): The issue if found or NULL otherwise
 */
GstValidateIssue *
gst_validate_issue_from_id (GstValidateIssueId issue_id)
{
  return g_hash_table_lookup (_gst_validate_issues, GINT_TO_POINTER (issue_id));
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
    case GST_VALIDATE_REPORT_LEVEL_EXPECTED:
      return "expected";
    default:
      return "unknown";
  }

  return NULL;
}

GstValidateReportLevel
gst_validate_report_level_from_name (const gchar * level_name)
{
  if (g_strcmp0 (level_name, "critical") == 0)
    return GST_VALIDATE_REPORT_LEVEL_CRITICAL;

  else if (g_strcmp0 (level_name, "warning") == 0)
    return GST_VALIDATE_REPORT_LEVEL_WARNING;

  else if (g_strcmp0 (level_name, "issue") == 0)
    return GST_VALIDATE_REPORT_LEVEL_ISSUE;

  else if (g_strcmp0 (level_name, "ignore") == 0)
    return GST_VALIDATE_REPORT_LEVEL_IGNORE;

  return GST_VALIDATE_REPORT_LEVEL_UNKNOWN;
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

guint32
gst_validate_report_get_issue_id (GstValidateReport * report)
{
  return gst_validate_issue_get_id (report->issue);
}

static void
_report_free (GstValidateReport * report)
{
  g_free (report->message);
  g_free (report->reporter_name);
  g_free (report->trace);
  g_free (report->dotfile_name);
  g_list_free_full (report->shadow_reports,
      (GDestroyNotify) gst_validate_report_unref);
  g_list_free_full (report->repeated_reports,
      (GDestroyNotify) gst_validate_report_unref);
  g_mutex_clear (&report->shadow_reports_lock);
  g_free (report);
}

static gboolean
gst_validate_report_should_generate_backtrace (GstValidateIssue * issue,
    GstValidateReport * report,
    GstValidateReportingDetails default_details,
    GstValidateReportingDetails issue_type_details,
    GstValidateReportingDetails reporter_details)
{
  if (issue->flags & GST_VALIDATE_ISSUE_FLAGS_FORCE_BACKTRACE)
    return TRUE;

  if (issue->flags & GST_VALIDATE_ISSUE_FLAGS_NO_BACKTRACE)
    return FALSE;

  if (default_details == GST_VALIDATE_SHOW_ALL)
    return TRUE;

  if (issue_type_details == GST_VALIDATE_SHOW_ALL)
    return TRUE;

  if (gst_validate_report_check_abort (report))
    return TRUE;

  if (report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
    return TRUE;

  return FALSE;
}

GstValidateReport *
gst_validate_report_new (GstValidateIssue * issue,
    GstValidateReporter * reporter, const gchar * message)
{
  GstValidateReport *report = g_new0 (GstValidateReport, 1);
  GstValidateReportingDetails reporter_details, default_details,
      issue_type_details;
  GstValidateRunner *runner = gst_validate_reporter_get_runner (reporter);

  gst_mini_object_init (((GstMiniObject *) report), 0,
      _gst_validate_report_type, NULL, NULL,
      (GstMiniObjectFreeFunction) _report_free);
  GST_MINI_OBJECT_FLAG_SET (report, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  report->issue = issue;
  /* The reporter is owning a ref on the report so it doesn't keep a ref to
   * avoid reference cycles. But the report can also be used by
   * GstValidateRunner *after* that the reporter has been destroyed, so we
   * cache the reporter name to avoid crashing in
   * gst_validate_report_print_detected_on if the reporter has been destroyed.
   */
  report->reporter = reporter;
  report->reporter_name = g_strdup (gst_validate_reporter_get_name (reporter));
  report->message = g_strdup (message);
  g_mutex_init (&report->shadow_reports_lock);
  report->timestamp =
      gst_util_get_timestamp () - _gst_validate_report_start_time;
  report->level = issue->default_level;
  report->reporting_level = GST_VALIDATE_SHOW_UNKNOWN;

  reporter_details = gst_validate_reporter_get_reporting_level (reporter);
  issue_type_details = gst_validate_runner_get_reporting_level_for_name (runner,
      g_quark_to_string (issue->issue_id));
  default_details = gst_validate_runner_get_default_reporting_details (runner);
  gst_object_unref (runner);
  if (reporter_details != GST_VALIDATE_SHOW_ALL &&
      reporter_details != GST_VALIDATE_SHOW_UNKNOWN)
    return report;

  if (gst_validate_report_should_generate_backtrace (issue, report,
          default_details, issue_type_details, reporter_details))
    report->trace = gst_debug_get_stack_trace (GST_STACK_TRACE_SHOW_FULL);

  return report;
}

void
gst_validate_report_unref (GstValidateReport * report)
{
  gst_mini_object_unref (GST_MINI_OBJECT (report));
}

GstValidateReport *
gst_validate_report_ref (GstValidateReport * report)
{
  return (GstValidateReport *) gst_mini_object_ref (GST_MINI_OBJECT (report));
}

void
gst_validate_printf (gpointer source, const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_validate_printf_valist (source, format, var_args);
  va_end (var_args);
}

typedef struct
{
  GString *str;
  gint indent;
  gint printed;
} PrintActionFieldData;

static gboolean
_append_value (GQuark field_id, const GValue * value, PrintActionFieldData * d)
{
  gchar *val_str = NULL;
  const gchar *fieldname = g_quark_to_string (field_id);

  if (g_str_has_prefix (fieldname, "__") && g_str_has_suffix (fieldname, "__"))
    return TRUE;

  if (g_strcmp0 (fieldname, "repeat") == 0)
    return TRUE;

  d->printed++;
  if (G_VALUE_TYPE (value) == GST_TYPE_CLOCK_TIME)
    val_str = g_strdup_printf ("%" GST_TIME_FORMAT,
        GST_TIME_ARGS (g_value_get_uint64 (value)));
  else
    val_str = gst_value_serialize (value);

  g_string_append_printf (d->str, "\n%*c   - ", d->indent, ' ');
  g_string_append (d->str, fieldname);
  g_string_append_len (d->str, "=", 1);
  g_string_append (d->str, val_str);

  g_free (val_str);

  return TRUE;
}

/**
 * gst_validate_print_action:
 * @action: (allow-none): The source object to log
 * @message: The message to print out in the GstValidate logging system
 *
 * Print @message to the GstValidate logging system
 */
void
gst_validate_print_action (GstValidateAction * action, const gchar * message)
{
  GString *string = NULL;

  if (message == NULL) {
    gint indent = (gst_validate_action_get_level (action) * 2);
    PrintActionFieldData d = { NULL, indent, 0 };
    d.str = string = g_string_new (NULL);

    g_string_append_printf (string, "`%s` at %s:%d", action->type,
        GST_VALIDATE_ACTION_FILENAME (action),
        GST_VALIDATE_ACTION_LINENO (action));

    if (GST_VALIDATE_ACTION_N_REPEATS (action))
      g_string_append_printf (string, " [%s=%d/%d]",
          GST_VALIDATE_ACTION_RANGE_NAME (action) ?
          GST_VALIDATE_ACTION_RANGE_NAME (action) : "repeat", action->repeat,
          GST_VALIDATE_ACTION_N_REPEATS (action));

    g_string_append (string, " ( ");
    gst_structure_foreach (action->structure,
        (GstStructureForeachFunc) _append_value, &d);
    if (d.printed)
      g_string_append_printf (string, "\n%*c)\n", indent, ' ');
    else
      g_string_append (string, ")\n");
    message = string->str;
  }

  gst_validate_printf (action, "%s", message);

  if (string)
    g_string_free (string, TRUE);
}

static void
print_action_parameter (GString * string, GstValidateActionType * type,
    GstValidateActionParameter * param)
{
  gchar *desc;
  g_string_append_printf (string, "\n\n* `%s`:(%s): ", param->name,
      param->mandatory ? "mandatory" : "optional");

  if (g_strcmp0 (param->description, "")) {
    desc = g_strdup (param->description);
  } else {
    desc = g_strdup ("__No description__");
  }

  g_string_append (string, desc);
  g_free (desc);

  if (param->possible_variables) {
    desc =
        g_regex_replace (newline_regex,
        param->possible_variables, -1, 0, "\n\n  * ", 0, NULL);
    g_string_append_printf (string, "\n\n  Possible variables:\n\n  * %s",
        desc);
  }

  if (param->types)
    g_string_append_printf (string, "\n\n  Possible types: `%s`", param->types);

  if (!param->mandatory)
    g_string_append_printf (string, "\n\n  Default: %s", param->def);

}

static void
print_action_parameter_prototype (GString * string,
    GstValidateActionParameter * param, gboolean is_first)
{
  if (!is_first)
    g_string_append (string, ",");
  g_string_append (string, "\n    ");

  if (!param->mandatory)
    g_string_append (string, "[");

  g_string_append (string, param->name);
  if (param->types)
    g_string_append_printf (string, "=(%s)", param->types);

  if (!param->mandatory)
    g_string_append (string, "]");
}

static int
sort_parameters (const GstValidateActionParameter * param1,
    const GstValidateActionParameter * param2)
{
  if (param1->mandatory && !param2->mandatory)
    return -1;

  if (!param1->mandatory && param2->mandatory)
    return 1;

  return g_strcmp0 (param1->name, param2->name);
}

void
gst_validate_printf_valist (gpointer source, const gchar * format, va_list args)
{
  gint i;
  gchar *tmp;
  GString *string = g_string_new (NULL);

  if (source) {
    if (*(GType *) source == GST_TYPE_VALIDATE_ACTION) {
      GstValidateAction *action = (GstValidateAction *) source;
      gint indent = gst_validate_action_get_level (action) * 2;

      if (_action_check_and_set_printed (action))
        goto out;

      if (!indent)
        g_string_assign (string, "Executing ");
      else
        g_string_append_printf (string, "%*c↳ Executing ", indent - 2, ' ');
    } else if (*(GType *) source == GST_TYPE_VALIDATE_ACTION_TYPE) {
      gint i;
      gint n_params;
      gboolean has_parameters = FALSE;
      gboolean is_first = TRUE;

      GstValidateActionParameter playback_time_param = {
        .name = "playback-time",
        .description = "The playback time at which the action will be executed",
        .mandatory = FALSE,
        .types = "double,string",
        .possible_variables =
            "`position`: The current position in the stream\n"
            "`duration`: The duration of the stream",
        .def = "0.0"
      };

      GstValidateActionParameter on_message_param = {
        .name = "on-message",
        .description =
            "Specify on what message type the action will be executed.\n"
            " If both 'playback-time' and 'on-message' is specified, the action will be executed\n"
            " on whatever happens first.",
        .mandatory = FALSE,
        .types = "string",
        .possible_variables = NULL,
        .def = NULL
      };


      GstValidateActionType *type = GST_VALIDATE_ACTION_TYPE (source);

      /* Ignore private action types */
      if (g_str_has_prefix (type->name, "priv_"))
        return;

      g_string_append_printf (string, "\n## %s\n\n", type->name);

      g_string_append_printf (string, "\n``` validate-scenario\n%s,",
          type->name);

      for (n_params = 0; type->parameters[n_params].name != NULL; n_params++);
      qsort (type->parameters, n_params, sizeof (GstValidateActionParameter),
          (GCompareFunc) sort_parameters);
      for (i = 0; type->parameters[i].name; i++) {
        print_action_parameter_prototype (string, &type->parameters[i],
            is_first);
        is_first = FALSE;
      }

      if (!IS_CONFIG_ACTION_TYPE (type->flags)) {
        print_action_parameter_prototype (string, &playback_time_param,
            is_first);
        is_first = FALSE;
      }

      g_string_append (string, ";\n```\n");

      g_string_append_printf (string, "\n%s", type->description);
      g_string_append_printf (string,
          "\n * Implementer namespace: %s", type->implementer_namespace);

      if (IS_CONFIG_ACTION_TYPE (type->flags))
        g_string_append_printf (string,
            "\n * Is config action (meaning it will be executing right "
            "at the beginning of the execution of the pipeline)");


      if (type->parameters || !IS_CONFIG_ACTION_TYPE (type->flags))
        g_string_append_printf (string, "\n\n### Parameters");

      if (type->parameters) {
        has_parameters = TRUE;
        for (i = 0; type->parameters[i].name; i++) {
          print_action_parameter (string, type, &type->parameters[i]);
        }
      }

      if (!IS_CONFIG_ACTION_TYPE (type->flags)) {
        print_action_parameter (string, type, &playback_time_param);
        print_action_parameter (string, type, &on_message_param);
      }


      if ((type->flags & GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL)) {
        has_parameters = TRUE;
        g_string_append_printf (string,
            "\n     optional                   : "
            "Don't raise an error if this action hasn't been executed or failed"
            "\n%-32s  ### Possible types:"
            "\n%-32s    boolean" "\n%-32s  Default: false", "", "", "");
      }

      if (!has_parameters)
        g_string_append_printf (string, "\n\n  ### No Parameters");
    } else if (GST_IS_VALIDATE_REPORTER (source) &&
        gst_validate_reporter_get_name (source)) {
      g_string_printf (string, "\n%s --> ",
          gst_validate_reporter_get_name (source));
    } else if (GST_IS_OBJECT (source)) {
      g_string_printf (string, "\n%s --> ", GST_OBJECT_NAME (source));
    } else if (G_IS_OBJECT (source)) {
      g_string_printf (string, "\n<%s@%p> --> ", G_OBJECT_TYPE_NAME (source),
          source);
    }
  }

  tmp = gst_info_strdup_vprintf (format, args);
  g_string_append (string, tmp);
  g_free (tmp);

  if (!newline_regex)
    newline_regex =
        g_regex_new ("\n", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *str;

    str = g_regex_replace (newline_regex, string->str, string->len, 0,
        "", 0, NULL);

    if (source)
      GST_INFO ("%s", str);
    else
      GST_DEBUG ("%s", str);

    g_free (str);
  }
#endif

  for (i = 0; log_files[i]; i++) {
    fprintf (log_files[i], "%s", string->str);
    fflush (log_files[i]);
  }

out:
  g_string_free (string, TRUE);
}

gboolean
gst_validate_report_set_master_report (GstValidateReport * report,
    GstValidateReport * master_report)
{
  GList *tmp;
  gboolean add_shadow_report = TRUE;

  if (master_report->reporting_level >= GST_VALIDATE_SHOW_MONITOR &&
      master_report->reporting_level != GST_VALIDATE_SHOW_SMART) {
    return FALSE;
  }

  report->master_report = master_report;

  GST_VALIDATE_REPORT_SHADOW_REPORTS_LOCK (master_report);
  for (tmp = master_report->shadow_reports; tmp; tmp = tmp->next) {
    GstValidateReport *shadow_report = (GstValidateReport *) tmp->data;
    if (report->reporter == shadow_report->reporter) {
      add_shadow_report = FALSE;
      break;
    }
  }
  if (add_shadow_report)
    master_report->shadow_reports =
        g_list_append (master_report->shadow_reports,
        gst_validate_report_ref (report));
  GST_VALIDATE_REPORT_SHADOW_REPORTS_UNLOCK (master_report);

  return TRUE;
}

void
gst_validate_report_print_level (GstValidateReport * report)
{
  gst_validate_printf (NULL, "%10s : %s\n",
      gst_validate_report_level_get_name (report->level),
      report->issue->summary);
}

void
gst_validate_report_print_detected_on (GstValidateReport * report)
{
  GList *tmp;

  gst_validate_printf (NULL, "%*s Detected on <%s",
      12, "", report->reporter_name);
  for (tmp = report->shadow_reports; tmp; tmp = tmp->next) {
    GstValidateReport *shadow_report = (GstValidateReport *) tmp->data;
    gst_validate_printf (NULL, ", %s", shadow_report->reporter_name);
  }
  gst_validate_printf (NULL, ">\n");
}

void
gst_validate_report_print_details (GstValidateReport * report)
{
  if (report->message) {
    gint i;
    gchar **lines = g_strsplit (report->message, "\n", -1);

    gst_validate_printf (NULL, "%*s Details : %s\n", 12, "", lines[0]);
    for (i = 1; lines[i]; i++)
      gst_validate_printf (NULL, "%*s%s\n", 21, "", lines[i]);
    g_strfreev (lines);
  }
}

static void
gst_validate_report_print_trace (GstValidateReport * report)
{
  if (report->trace) {
    gint i;
    gchar **lines = g_strsplit (report->trace, "\n", -1);

    gst_validate_printf (NULL, "%*s backtrace :\n", 12, "");
    for (i = 0; lines[i]; i++)
      gst_validate_printf (NULL, "%*s%s\n", 15, "", lines[i]);
    g_strfreev (lines);
  }
}

static void
gst_validate_report_print_dotfile (GstValidateReport * report)
{
  const gchar *dotdir = g_getenv ("GST_DEBUG_DUMP_DOT_DIR");
  const gchar *doturl = g_getenv ("GST_VALIDATE_DEBUG_DUMP_DOT_URL");

  if (!report->dotfile_name)
    return;

  if (doturl)
    gst_validate_printf (NULL, "%*s dotfile : %s%s%s.dot\n", 12, "",
        doturl, G_DIR_SEPARATOR_S, report->dotfile_name);
  else if (dotdir)
    gst_validate_printf (NULL, "%*s dotfile : %s%s%s.dot\n", 12, "",
        dotdir, G_DIR_SEPARATOR_S, report->dotfile_name);
  else
    gst_validate_printf (NULL,
        "%*s dotfile : no dotfile produced as GST_DEBUG_DUMP_DOT_DIR is not set.\n",
        12, "");
}

void
gst_validate_report_print_description (GstValidateReport * report)
{
  if (report->issue->description)
    gst_validate_printf (NULL, "%*s Description : %s\n", 12, "",
        report->issue->description);
}

void
gst_validate_report_printf (GstValidateReport * report)
{
  GList *tmp;

  gst_validate_report_print_level (report);
  gst_validate_report_print_detected_on (report);
  gst_validate_report_print_details (report);
  for (tmp = report->repeated_reports; tmp; tmp = tmp->next) {
    gst_validate_report_print_details (tmp->data);
  }
  gst_validate_report_print_dotfile (report);
  gst_validate_report_print_trace (report);

  gst_validate_report_print_description (report);
  gst_validate_printf (NULL, "\n");
}

void
gst_validate_report_set_reporting_level (GstValidateReport * report,
    GstValidateReportingDetails level)
{
  report->reporting_level = level;
}

void
gst_validate_report_add_repeated_report (GstValidateReport * report,
    GstValidateReport * repeated_report)
{
  report->repeated_reports =
      g_list_append (report->repeated_reports,
      gst_validate_report_ref (repeated_report));
}


void
gst_validate_print_position (GstClockTime position, GstClockTime duration,
    gdouble rate, gchar * extra_info)
{
  JsonBuilder *jbuilder;

  gst_validate_printf (NULL,
      "<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " speed: %f %s/>%c", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
      rate, extra_info ? extra_info : "", output_is_tty ? '\r' : '\n');

  if (!server_ostream)
    return;

  jbuilder = json_builder_new ();
  json_builder_begin_object (jbuilder);
  json_builder_set_member_name (jbuilder, "type");
  json_builder_add_string_value (jbuilder, "position");
  json_builder_set_member_name (jbuilder, "position");
  json_builder_add_int_value (jbuilder, position);
  json_builder_set_member_name (jbuilder, "duration");
  json_builder_add_int_value (jbuilder, duration);
  json_builder_set_member_name (jbuilder, "speed");
  json_builder_add_double_value (jbuilder, rate);
  json_builder_end_object (jbuilder);

  gst_validate_send (json_builder_get_root (jbuilder));
  g_object_unref (jbuilder);

  g_free (extra_info);
}

void
gst_validate_skip_test (const gchar * format, ...)
{
  JsonBuilder *jbuilder;
  va_list va_args;
  gchar *tmp;

  va_start (va_args, format);
  tmp = gst_info_strdup_vprintf (format, va_args);
  va_end (va_args);

  if (!server_ostream) {
    gchar *f = g_strconcat ("ok 1 # SKIP ", tmp, NULL);

    g_free (tmp);
    gst_validate_printf (NULL, "%s", f);
    return;
  }

  jbuilder = json_builder_new ();
  json_builder_begin_object (jbuilder);
  json_builder_set_member_name (jbuilder, "type");
  json_builder_add_string_value (jbuilder, "skip-test");
  json_builder_set_member_name (jbuilder, "details");
  json_builder_add_string_value (jbuilder, tmp);
  json_builder_end_object (jbuilder);
  g_free (tmp);

  gst_validate_send (json_builder_get_root (jbuilder));
  g_object_unref (jbuilder);
}

static void
print_issue (gpointer key, GstValidateIssue * issue, gpointer user_data)
{
  gst_validate_printf (NULL, "\n# `%s` (%" G_GUINT32_FORMAT ")\n\n",
      g_quark_to_string (issue->issue_id), issue->issue_id);
  gst_validate_printf (NULL, "%c%s\n\n", g_ascii_toupper (issue->summary[0]),
      &issue->summary[1]);
  if (issue->description)
    gst_validate_printf (NULL, "%c%s\n\n",
        g_ascii_toupper (issue->description[0]), &issue->description[1]);
  gst_validate_printf (NULL, "Area: %s\n", issue->area);
  gst_validate_printf (NULL, "Name: %s\n", issue->name);
  gst_validate_printf (NULL, "Default severity: %s\n\n",
      gst_validate_report_level_get_name (issue->default_level));
}

void
gst_validate_print_issues (void)
{
  g_return_if_fail (_gst_validate_issues);

  g_hash_table_foreach (_gst_validate_issues, (GHFunc) print_issue, NULL);
}

void
gst_validate_error_structure (gpointer structure, const gchar * format, ...)
{
  gchar *filename = NULL;
  gint lineno = -1;
  gchar *tmp, *debug = NULL;
  GString *f = g_string_new (NULL);
  va_list var_args;
  gchar *color = NULL;

  const gchar *endcolor = "";

  if (g_log_writer_supports_color (fileno (stderr))) {
    color = gst_debug_construct_term_color (GST_DEBUG_FG_RED);
    endcolor = "\033[0m";
  }

  if (structure) {
    if (GST_IS_STRUCTURE (structure)) {
      filename =
          g_strdup (gst_structure_get_string (structure, "__filename__"));
      debug = g_strdup (gst_structure_get_string (structure, "__debug__"));
      gst_structure_get_int (structure, "__lineno__", &lineno);
      /* We are going to assert... we can boutcher the struct! */
      gst_structure_remove_fields (structure, "__filename__", "__lineno__",
          "__debug__", NULL);
    } else {
      filename = g_strdup (GST_VALIDATE_ACTION_FILENAME (structure));
      debug = g_strdup (GST_VALIDATE_ACTION_DEBUG (structure));
      lineno = GST_VALIDATE_ACTION_LINENO (structure);
    }
  }

  va_start (var_args, format);
  tmp = gst_info_strdup_vprintf (format, var_args);
  va_end (var_args);

  g_string_append_printf (f, "%s:%d: %s\n",
      filename ? filename : "Unknown", lineno, tmp);

  if (debug)
    g_string_append (f, debug);

  g_print ("Bail out! %sERROR%s: %s\n\n", color ? color : "", endcolor, f->str);
  g_string_free (f, TRUE);
  g_free (debug);
  g_free (color);
  g_free (filename);
  g_free (tmp);

  exit (-18);
}

void
gst_validate_abort (const gchar * format, ...)
{
  va_list var_args;
  gchar *tmp;

  va_start (var_args, format);
  tmp = gst_info_strdup_vprintf (format, var_args);
  va_end (var_args);

  g_print ("Bail out! %s\n", tmp);
  exit (-18);
}

gboolean
is_tty ()
{
  return output_is_tty;
}
