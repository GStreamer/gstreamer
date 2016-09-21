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

#ifdef HAVE_UNWIND
/* No need for remote debugging so turn on the 'local only' optimizations in
 * libunwind */
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#endif /* HAVE_UNWIND */


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
static FILE **log_files = NULL;

/* Tcp server for communications with gst-validate-launcher */
GSocketClient *socket_client = NULL;
GSocketConnection *server_connection = NULL;
GOutputStream *server_ostream = NULL;

GType _gst_validate_report_type = 0;

#ifdef HAVE_UNWIND
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#ifdef HAVE_DW
#include <elfutils/libdwfl.h>
#include <libunwind.h>
static void
append_debug_info (GString * trace, const void *ip)
{

  char *debuginfo_path = NULL;

  Dwfl_Callbacks callbacks = {
    .find_elf = dwfl_linux_proc_find_elf,
    .find_debuginfo = dwfl_standard_find_debuginfo,
    .debuginfo_path = &debuginfo_path,
  };

  Dwfl *dwfl = dwfl_begin (&callbacks);
  assert (dwfl != NULL);

  assert (dwfl_linux_proc_report (dwfl, getpid ()) == 0);
  assert (dwfl_report_end (dwfl, NULL, NULL) == 0);

  Dwarf_Addr addr = (uintptr_t) ip;

  Dwfl_Module *module = dwfl_addrmodule (dwfl, addr);

  const char *function_name = dwfl_module_addrname (module, addr);

  g_string_append_printf (trace, "%s(", function_name ? function_name : "??");

  Dwfl_Line *line = dwfl_getsrc (dwfl, addr);
  if (line != NULL) {
    int nline;
    Dwarf_Addr addr;
    const char *filename = dwfl_lineinfo (line, &addr,
        &nline, NULL, NULL, NULL);
    g_string_append_printf (trace, "%s:%d", strrchr (filename, '/') + 1, nline);
  } else {
    const gchar *eflfile = NULL;

    dwfl_module_info (module, NULL, NULL, NULL, NULL, NULL, &eflfile, NULL);
    g_string_append_printf (trace, "%s:%p", eflfile ? eflfile : "??", ip);
  }
}
#endif

static gchar *
generate_unwind_trace (void)
{
  unw_context_t uc;
  unw_cursor_t cursor;
  GString *trace = g_string_new (NULL);

  unw_getcontext (&uc);
  unw_init_local (&cursor, &uc);

  while (unw_step (&cursor) > 0) {
#ifdef HAVE_DW
    unw_word_t ip;

    unw_get_reg (&cursor, UNW_REG_IP, &ip);
    append_debug_info (trace, (void *) (ip - 4));
    g_string_append (trace, ")\n");
#else
    char name[32];

    unw_word_t offset;
    unw_get_proc_name (&cursor, name, sizeof (name), &offset);
    g_string_append_printf (trace, "%s (0x%lx)\n", name, (long) offset);
#endif
  }

  return g_string_free (trace, FALSE);
}

#endif /* HAVE_UNWIND */

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#define BT_BUF_SIZE 100
static gchar *
generate_backtrace_trace (void)
{
  int j, nptrs;
  void *buffer[BT_BUF_SIZE];
  char **strings;
  GString *trace;

  trace = g_string_new (NULL);
  nptrs = backtrace (buffer, BT_BUF_SIZE);

  strings = backtrace_symbols (buffer, nptrs);

  if (!strings)
    return NULL;

  for (j = 0; j < nptrs; j++)
    g_string_append_printf (trace, "%s\n", strings[j]);

  return g_string_free (trace, FALSE);
}
#endif /* HAVE_BACKTRACE */

static gchar *
generate_trace (void)
{
  gchar *trace = NULL;

#ifdef HAVE_UNWIND
  trace = generate_unwind_trace ();
  if (trace)
    return trace;
#endif /* HAVE_UNWIND */

#ifdef HAVE_BACKTRACE
  trace = generate_backtrace_trace ();
#endif /* HAVE_BACKTRACE */


  return trace;
}

static JsonNode *
gst_validate_report_serialize (GstValidateReport * report)
{
  JsonNode *node = json_node_alloc ();
  JsonObject *jreport = json_object_new ();

  json_object_set_string_member (jreport, "type", "report");
  json_object_set_string_member (jreport, "summary", report->issue->summary);
  json_object_set_string_member (jreport, "level",
      gst_validate_report_level_get_name (report->level));
  json_object_set_string_member (jreport, "detected-on", report->reporter_name);
  json_object_set_string_member (jreport, "details", report->message);

  node = json_node_init_object (node, jreport);

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

    g_slice_free (GstValidateIssue, issue);
  }
}


G_DEFINE_BOXED_TYPE (GstValidateIssue, gst_validate_issue,
    (GBoxedCopyFunc) gst_validate_issue_ref,
    (GBoxedFreeFunc) gst_validate_issue_unref);

GstValidateIssueId
gst_validate_issue_get_id (GstValidateIssue * issue)
{
  return issue->issue_id;
}

/**
 * gst_validate_issue_new:
 * @issue_id: The ID of the issue, should be a GQuark
 * @summary: A summary of the issue
 * @description: A more complete of what the issue is about
 * @default_level: The level at which the issue will be reported by default
 *
 * Returns: (transfer full): The newly created #GstValidateIssue
 */
GstValidateIssue *
gst_validate_issue_new (GstValidateIssueId issue_id, const gchar * summary,
    const gchar * description, GstValidateReportLevel default_level)
{
  GstValidateIssue *issue = g_slice_new (GstValidateIssue);
  gchar **area_name = g_strsplit (g_quark_to_string (issue_id), "::", 2);

  g_return_val_if_fail (area_name[0] != NULL && area_name[1] != 0 &&
      area_name[2] == NULL, NULL);

  issue->issue_id = issue_id;
  issue->summary = g_strdup (summary);
  issue->description = g_strdup (description);
  issue->default_level = default_level;
  issue->area = area_name[0];
  issue->name = area_name[1];

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
          (gpointer) gst_validate_issue_get_id (issue)) == NULL);

  g_hash_table_insert (_gst_validate_issues,
      (gpointer) gst_validate_issue_get_id (issue), issue);
}

#define REGISTER_VALIDATE_ISSUE(lvl,id,sum,desc)			\
  gst_validate_issue_register (gst_validate_issue_new (id, \
						       sum, desc, GST_VALIDATE_REPORT_LEVEL_##lvl))
static void
gst_validate_report_load_issues (void)
{
  g_return_if_fail (_gst_validate_issues == NULL);

  _gst_validate_issues = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_validate_issue_unref);

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
  REGISTER_VALIDATE_ISSUE (WARNING, WRONG_BUFFER,
      _("Received buffer does not correspond to wanted one."),
      _("When checking playback of a file against a MediaInfo file"
          " all buffers coming into the decoders might be checked"
          " and should have the exact expected metadatas and hash of the"
          " content"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, WRONG_FLOW_RETURN,
      _("flow return from pad push doesn't match expected value"),
      _("flow return from a 1:1 sink/src pad element is as simple as "
          "returning what downstream returned. For elements that have multiple "
          "src pads, flow returns should be properly combined"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_AFTER_EOS,
      _("buffer was received after EOS"),
      _("a pad shouldn't receive any more buffers after it gets EOS"));
  REGISTER_VALIDATE_ISSUE (WARNING, FLOW_ERROR_WITHOUT_ERROR_MESSAGE,
      _("GST_FLOW_ERROR returned without posting an ERROR on the bus"),
      _("Element MUST post a GST_MESSAGE_ERROR with GST_ELEMENT_ERROR before"
          " returning GST_FLOW_ERROR"));
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_MISSING_DISCONT,
      _("Buffer didn't have expected DISCONT flag"),
      _("Buffers after SEGMENT and FLUSH must have a DISCONT flag"));

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
  REGISTER_VALIDATE_ISSUE (ISSUE, EOS_HAS_WRONG_SEQNUM,
      _("EOS events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, FLUSH_START_HAS_WRONG_SEQNUM,
      _
      ("FLUSH_START events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, FLUSH_STOP_HAS_WRONG_SEQNUM,
      _
      ("FLUSH_STOP events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (ISSUE, SEGMENT_HAS_WRONG_SEQNUM,
      _("SEGMENT events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, SEGMENT_HAS_WRONG_START,
      _("A segment doesn't have the proper time value after an ACCURATE seek"),
      _("If a seek with the ACCURATE flag was accepted, the following segment "
          "should have a time value corresponding exactly to the requested start "
          "seek time"));
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

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_EOS_WITHOUT_SEGMENT,
      _("EOS received without segment event before"),
      _("A segment event should always be sent before data flow"
          " EOS being some kind of data flow, there is no exception"
          " in that regard"));

  REGISTER_VALIDATE_ISSUE (CRITICAL, STATE_CHANGE_FAILURE,
      _("state change failed"), NULL);

  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SIZE_INCORRECT,
      _("resulting file size wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_DURATION_INCORRECT,
      _("resulting file duration wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SEEKABLE_INCORRECT,
      _("resulting file wasn't seekable or not seekable as expected"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PROFILE_INCORRECT,
      _("resulting file stream profiles didn't match expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, FILE_TAG_DETECTION_INCORRECT,
      _("detected tags are different than expected ones"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_FRAMES_INCORRECT,
      _("resulting file frames are not as expected"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_INFO,
      _("the discoverer could not determine the stream info"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_ID,
      _("the discoverer found a stream that had no stream ID"), NULL);


  REGISTER_VALIDATE_ISSUE (CRITICAL, ALLOCATION_FAILURE,
      _("a memory allocation failed during Validate run"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, MISSING_PLUGIN,
      _("a gstreamer plugin is missing and prevented Validate from running"),
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, NOT_NEGOTIATED,
      _("a NOT NEGOTIATED message has been posted on the bus."), NULL);
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
      _("All the actions were not executed before the program stopped"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_ACTION_TIMEOUT,
      _("The execution of an action timed out"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_FILE_MALFORMED,
      _("The scenario file was malformed"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_ACTION_EXECUTION_ERROR,
      _("The execution of an action did not properly happen"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, SCENARIO_ACTION_EXECUTION_ISSUE,
      _("An issue happend during the execution of a scenario"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, G_LOG_WARNING, _("We got a g_log warning"),
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, G_LOG_CRITICAL,
      _("We got a g_log critical issue"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, G_LOG_ISSUE, _("We got a g_log issue"), NULL);
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
      GST_ERROR ("Stream was busy, trying again later.");

      g_free (message);
      g_object_unref (jgen);
      g_idle_add ((GSourceFunc) gst_validate_send, root);
      return G_SOURCE_REMOVE;
    }

    GST_ERROR ("ERROR: Can't write to remote: %s", error->message);
  } else if (!g_output_stream_flush (server_ostream, NULL, &error)) {
    GST_ERROR ("ERROR: Can't flush stream: %s", error->message);
  }

  g_free (message);
  g_object_unref (jgen);

done:
  json_node_free (root);

  return G_SOURCE_REMOVE;
}

void
gst_validate_report_init (void)
{
  const gchar *var, *file_env, *server_env;
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
    if (var && strlen (var) > 0) {
      _gst_validate_flags =
          g_parse_debug_string (var, keys, G_N_ELEMENTS (keys));
    }

    gst_validate_report_load_issues ();
  }

  server_env = g_getenv ("GST_VALIDATE_SERVER");
  if (server_env) {
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
  if (server_ostream)
    g_output_stream_close (server_ostream, NULL, NULL);
  g_clear_object (&socket_client);
  g_clear_object (&server_connection);
  g_clear_object (&server_ostream);
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

GstValidateIssueId
gst_validate_report_get_issue_id (GstValidateReport * report)
{
  return gst_validate_issue_get_id (report->issue);
}

static void
_report_free (GstValidateReport * report)
{
  g_free (report->message);
  g_free (report->reporter_name);
  g_list_free_full (report->shadow_reports,
      (GDestroyNotify) gst_validate_report_unref);
  g_list_free_full (report->repeated_reports,
      (GDestroyNotify) gst_validate_report_unref);
  g_mutex_clear (&report->shadow_reports_lock);
  g_slice_free (GstValidateReport, report);
}

GstValidateReport *
gst_validate_report_new (GstValidateIssue * issue,
    GstValidateReporter * reporter, const gchar * message)
{
  GstValidateReport *report = g_slice_new0 (GstValidateReport);
  GstValidateReportingDetails reporter_details, default_details,
      issue_type_details;
  GstValidateRunner *runner = gst_validate_reporter_get_runner (reporter);

  gst_mini_object_init (((GstMiniObject *) report), 0,
      _gst_validate_report_type, NULL, NULL,
      (GstMiniObjectFreeFunction) _report_free);

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
  if (reporter_details != GST_VALIDATE_SHOW_ALL &&
      reporter_details != GST_VALIDATE_SHOW_UNKNOWN)
    return report;

  if (default_details == GST_VALIDATE_SHOW_ALL ||
      issue_type_details == GST_VALIDATE_SHOW_ALL ||
      gst_validate_report_check_abort (report) ||
      report->level == GST_VALIDATE_REPORT_LEVEL_CRITICAL)
    report->trace = generate_trace ();

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

static gboolean
_append_value (GQuark field_id, const GValue * value, GString * string)
{
  gchar *val_str = NULL;

  if (g_strcmp0 (g_quark_to_string (field_id), "sub-action") == 0)
    return TRUE;

  if (G_VALUE_TYPE (value) == GST_TYPE_CLOCK_TIME)
    val_str = g_strdup_printf ("%" GST_TIME_FORMAT,
        GST_TIME_ARGS (g_value_get_uint64 (value)));
  else
    val_str = gst_value_serialize (value);

  g_string_append (string, g_quark_to_string (field_id));
  g_string_append_len (string, "=", 1);
  g_string_append (string, val_str);
  g_string_append_len (string, " ", 1);

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
    gint nrepeats;

    string = g_string_new (NULL);

    if (gst_validate_action_is_subaction (action))
      g_string_append_printf (string, "(subaction)");

    if (gst_structure_get_int (action->structure, "repeat", &nrepeats))
      g_string_append_printf (string, "(%d/%d)", action->repeat, nrepeats);

    g_string_append_printf (string, " %s",
        gst_structure_get_name (action->structure));

    g_string_append_len (string, ": ", 2);
    gst_structure_foreach (action->structure,
        (GstStructureForeachFunc) _append_value, string);
    g_string_append_len (string, "\n", 1);
    message = string->str;
  }

  gst_validate_printf (action, "%s", message);

  if (string)
    g_string_free (string, TRUE);
}

static void
print_action_parametter (GString * string, GstValidateActionType * type,
    GstValidateActionParameter * param)
{
  gint nw = 0;

  gchar *desc, *tmp;
  gchar *param_head = g_strdup_printf ("    %s", param->name);
  gchar *tmp_head = g_strdup_printf ("\n %-30s : %s",
      param_head, "something");


  while (tmp_head[nw] != ':')
    nw++;

  g_free (tmp_head);

  tmp = g_strdup_printf ("\n%*s", nw + 1, " ");

  if (g_strcmp0 (param->description, "")) {
    desc =
        g_regex_replace (newline_regex, param->description,
        -1, 0, tmp, 0, NULL);
  } else {
    desc = g_strdup_printf ("No description");
  }

  g_string_append_printf (string, "\n %-30s : %s", param_head, desc);
  g_free (desc);

  if (param->possible_variables) {
    gchar *tmp1 = g_strdup_printf ("\n%*s", nw + 4, " ");
    desc =
        g_regex_replace (newline_regex,
        param->possible_variables, -1, 0, tmp1, 0, NULL);
    g_string_append_printf (string, "%sPossible variables:%s%s", tmp,
        tmp1, desc);

    g_free (tmp1);
  }

  if (param->types) {
    gchar *tmp1 = g_strdup_printf ("\n%*s", nw + 4, " ");
    desc = g_regex_replace (newline_regex, param->types, -1, 0, tmp1, 0, NULL);
    g_string_append_printf (string, "%sPossible types:%s%s", tmp, tmp1, desc);

    g_free (tmp1);
  }

  if (!param->mandatory) {
    g_string_append_printf (string, "%sDefault: %s", tmp, param->def);
  }

  g_string_append_printf (string, "%s%s", tmp,
      param->mandatory ? "Mandatory." : "Optional.");

  g_free (tmp);
  g_free (param_head);
}

void
gst_validate_printf_valist (gpointer source, const gchar * format, va_list args)
{
  gint i;
  GString *string = g_string_new (NULL);

  if (source) {
    if (*(GType *) source == GST_TYPE_VALIDATE_ACTION) {
      GstValidateAction *action = (GstValidateAction *) source;

      if (_action_check_and_set_printed (action))
        goto out;

      g_string_assign (string, "Executing ");

    } else if (*(GType *) source == GST_TYPE_VALIDATE_ACTION_TYPE) {
      gint i;
      gchar *desc, *tmp;
      gboolean has_parameters = FALSE;

      GstValidateActionParameter playback_time_param = {
        .name = "playback-time",
        .description =
            "The playback time at which the action " "will be executed",
        .mandatory = FALSE,
        .types = "double,string",
        .possible_variables =
            "position: The current position in the stream\n"
            "duration: The duration of the stream",
        .def = "0.0"
      };

      GstValidateActionType *type = GST_VALIDATE_ACTION_TYPE (source);

      g_string_assign (string, "\nAction type:");
      g_string_append_printf (string, "\n  Name: %s", type->name);
      g_string_append_printf (string, "\n  Implementer namespace: %s",
          type->implementer_namespace);

      if (IS_CONFIG_ACTION_TYPE (type->flags))
        g_string_append_printf (string,
            "\n    Is config action (meaning it will be executing right "
            "at the begining of the execution of the pipeline)");


      tmp = g_strdup_printf ("\n    ");
      desc =
          g_regex_replace (newline_regex, type->description, -1, 0, tmp, 0,
          NULL);
      g_string_append_printf (string, "\n\n  Description: \n    %s", desc);
      g_free (desc);
      g_free (tmp);

      if (!IS_CONFIG_ACTION_TYPE (type->flags))
        print_action_parametter (string, type, &playback_time_param);

      if (type->parameters) {
        has_parameters = TRUE;
        g_string_append_printf (string, "\n\n  Parametters:");
        for (i = 0; type->parameters[i].name; i++) {
          print_action_parametter (string, type, &type->parameters[i]);
        }

      }

      if ((type->flags & GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL)) {
        has_parameters = TRUE;
        g_string_append_printf (string, "\n     %-26s : %s", "optional",
            "Don't raise an error if this action hasn't been executed of failed");
        g_string_append_printf (string, "\n     %-28s %s", "",
            "Possible types:");
        g_string_append_printf (string, "\n     %-31s %s", "", "boolean");
        g_string_append_printf (string, "\n     %-28s %s", "",
            "Default: false");
      }

      if (!has_parameters)
        g_string_append_printf (string, "\n\n  No Parameters");
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

  g_string_append_vprintf (string, format, args);

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

  if (master_report->reporting_level >= GST_VALIDATE_SHOW_MONITOR)
    return FALSE;

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
  }
}

static void
gst_validate_report_print_dotfile (GstValidateReport * report)
{
  const gchar *dotdir = g_getenv ("GST_DEBUG_DUMP_DOT_DIR");

  if (!report->dotfile_name)
    return;

  if (dotdir)
    gst_validate_printf (NULL, "%*s dotfile : %s%s%s.dot\n", 12, "",
        dotdir, G_DIR_SEPARATOR_S, report->dotfile_name);
  else
    gst_validate_printf (NULL,
        "%*s dotfile : not dotfile produced as GST_DEBUG_DUMP_DOT_DIR is not set.\n",
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
  gst_validate_report_print_dotfile (report);
  gst_validate_report_print_trace (report);

  for (tmp = report->repeated_reports; tmp; tmp = tmp->next) {
    gst_validate_report_print_details (report);
  }

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
