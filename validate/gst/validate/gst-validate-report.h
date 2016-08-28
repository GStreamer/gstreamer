/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor-report.h - Validate Element report structures and functions
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

#ifndef __GST_VALIDATE_REPORT_H__
#define __GST_VALIDATE_REPORT_H__

#include <glib-object.h>

typedef struct _GstValidateReport GstValidateReport;
typedef guintptr GstValidateIssueId;

#include <gst/gst.h>
#include <gst/validate/gst-validate-reporter.h>
#include "gst-validate-types.h"

G_BEGIN_DECLS

GType           gst_validate_report_get_type (void);
#define GST_TYPE_VALIDATE_REPORT (gst_validate_report_get_type ())

typedef enum {
  GST_VALIDATE_FATAL_DEFAULT = 0,
  GST_VALIDATE_FATAL_ISSUES = 1 << 0,
  GST_VALIDATE_FATAL_WARNINGS = 1 << 1,
  GST_VALIDATE_FATAL_CRITICALS = 1 << 2,
  GST_VALIDATE_PRINT_ISSUES = 1 << 3,
  GST_VALIDATE_PRINT_WARNINGS = 1 << 4,
  GST_VALIDATE_PRINT_CRITICALS = 1 << 5
} GstValidateDebugFlags;

typedef enum {
  GST_VALIDATE_REPORT_LEVEL_CRITICAL,
  GST_VALIDATE_REPORT_LEVEL_WARNING,
  GST_VALIDATE_REPORT_LEVEL_ISSUE,
  GST_VALIDATE_REPORT_LEVEL_IGNORE,
  GST_VALIDATE_REPORT_LEVEL_UNKNOWN,
  GST_VALIDATE_REPORT_LEVEL_NUM_ENTRIES,
} GstValidateReportLevel;

#define _QUARK g_quark_from_static_string

#define BUFFER_BEFORE_SEGMENT                    _QUARK("buffer::before-segment")
#define BUFFER_IS_OUT_OF_SEGMENT                 _QUARK("buffer::is-out-of-segment")
#define BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE   _QUARK("buffer::timestamp-out-of-received-range")
#define WRONG_FLOW_RETURN                        _QUARK("buffer::wrong-flow-return")
#define BUFFER_AFTER_EOS                         _QUARK("buffer::after-eos")
#define WRONG_BUFFER                             _QUARK("buffer::not-expected-one")
#define FLOW_ERROR_WITHOUT_ERROR_MESSAGE         _QUARK("buffer::flow-error-without-error-message")
#define BUFFER_MISSING_DISCONT                   _QUARK("buffer::missing-discont")

#define CAPS_IS_MISSING_FIELD                    _QUARK("caps::is-missing-field")
#define CAPS_FIELD_HAS_BAD_TYPE                  _QUARK("caps::field-has-bad-type")
#define CAPS_EXPECTED_FIELD_NOT_FOUND            _QUARK("caps::expected-field-not-found")
#define GET_CAPS_NOT_PROXYING_FIELDS             _QUARK("caps::not-proxying-fields")
#define CAPS_FIELD_UNEXPECTED_VALUE              _QUARK("caps::field-unexpected-value")

#define EVENT_NEWSEGMENT_NOT_PUSHED              _QUARK("event::newsegment-not-pushed")
#define SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME    _QUARK("event::serialized-event-wasnt-pushed-in-time")

#define EOS_HAS_WRONG_SEQNUM                    _QUARK("event::eos-has-wrong-seqnum")
#define FLUSH_START_HAS_WRONG_SEQNUM            _QUARK("event::flush-start-has-wrong-seqnum")
#define FLUSH_STOP_HAS_WRONG_SEQNUM             _QUARK("event::flush-stop-has-wrong-seqnum")
#define SEGMENT_HAS_WRONG_SEQNUM                _QUARK("event::segment-has-wrong-seqnum")
#define SEGMENT_HAS_WRONG_START                 _QUARK("event::segment-has-wrong-start")


#define EVENT_SERIALIZED_OUT_OF_ORDER            _QUARK("event::serialized-out-of-order")
#define EVENT_NEW_SEGMENT_MISMATCH               _QUARK("event::segment-mismatch")
#define EVENT_FLUSH_START_UNEXPECTED             _QUARK("event::flush-start-unexpected")
#define EVENT_FLUSH_STOP_UNEXPECTED              _QUARK("event::flush-stop-unexpected")
#define EVENT_CAPS_DUPLICATE                     _QUARK("event::caps-duplicate")
#define EVENT_SEEK_NOT_HANDLED                   _QUARK("event::seek-not-handled")
#define EVENT_SEEK_RESULT_POSITION_WRONG         _QUARK("event::seek-result-position-wrong")
#define EVENT_EOS_WITHOUT_SEGMENT                _QUARK("event::eos-without-segment")

#define STATE_CHANGE_FAILURE                     _QUARK("state::change-failure")

#define FILE_NO_STREAM_INFO                      _QUARK("file-checking::no-stream-info")
#define FILE_NO_STREAM_ID                        _QUARK("file-checking::no-stream-id")
#define FILE_TAG_DETECTION_INCORRECT             _QUARK("file-checking::tag-detection-incorrect")
#define FILE_SIZE_INCORRECT                      _QUARK("file-checking::size-incorrect")
#define FILE_DURATION_INCORRECT                  _QUARK("file-checking::duration-incorrect")
#define FILE_SEEKABLE_INCORRECT                  _QUARK("file-checking::seekable-incorrect")
#define FILE_PROFILE_INCORRECT                   _QUARK("file-checking::profile-incorrect")
#define FILE_FRAMES_INCORRECT                    _QUARK("file-checking::frames-incorrect")

#define ALLOCATION_FAILURE                       _QUARK("runtime::allocation-failure")
#define MISSING_PLUGIN                           _QUARK("runtime::missing-plugin")
#define NOT_NEGOTIATED                           _QUARK("runtime::not-negotiated")
#define WARNING_ON_BUS                           _QUARK("runtime::warning-on-bus")
#define ERROR_ON_BUS                             _QUARK("runtime::error-on-bus")

#define QUERY_POSITION_SUPERIOR_DURATION         _QUARK("query::position-superior-duration")
#define QUERY_POSITION_OUT_OF_SEGMENT            _QUARK("query::position-out-of-segment")

#define SCENARIO_NOT_ENDED                       _QUARK("scenario::not-ended")
#define SCENARIO_FILE_MALFORMED                  _QUARK("scenario::malformed")
#define SCENARIO_ACTION_EXECUTION_ERROR          _QUARK("scenario::execution-error")
#define SCENARIO_ACTION_TIMEOUT                  _QUARK("scenario::action-timeout")
#define SCENARIO_ACTION_EXECUTION_ISSUE          _QUARK("scenario::execution-issue")

#define G_LOG_ISSUE                              _QUARK("g-log::issue")
#define G_LOG_WARNING                            _QUARK("g-log::warning")
#define G_LOG_CRITICAL                           _QUARK("g-log::critical")

typedef struct {
  GstValidateIssueId issue_id;

  /* Summary: one-liner translatable description of the issue */
  gchar *summary;
  /* description: multi-line translatable description of:
  * * what the issue is (and why it's an issue)
  * * what the source problem could be
  * * pointers to fixing the issue
  */
  gchar *description;

  /* The name of the area of issue
   * this one is in */
  gchar *area;
  /*  The name of the issue type */
  gchar *name;

  /* default_level: The default level of severity for this
  * issue. */
  GstValidateReportLevel default_level;

  gint    refcount;

  gpointer _gst_reserved[GST_PADDING];

} GstValidateIssue;

GType           gst_validate_issue_get_type (void);

struct _GstValidateReport {
  GstMiniObject mini_object;

  /* issue: The issue this report corresponds to (to get description, summary,...) */
  GstValidateIssue *issue;

  GstValidateReportLevel level;

  /* The reporter that reported the issue (to get names, info, ...) */
  GstValidateReporter *reporter;

  /* timestamp: The time at which this issue happened since
   * the process start (to stay in sync with gst logging) */
  GstClockTime timestamp;

  /* message: issue-specific message. Gives more detail on the actual
   * issue. Can be NULL */
  gchar *message;

  /* When reporter->intercept_report returns KEEP, the report is not
   * added to the runner. It can be added as a "shadow_report" to
   * the upstream report, which is tracked by the runner. */
  GMutex shadow_reports_lock;
  GstValidateReport *master_report;
  GList *shadow_reports;

  /* Lists the reports that were repeated inside the same reporter */
  GList *repeated_reports;

  GstValidateReportingDetails reporting_level;
  gchar *reporter_name;

  gpointer _gst_reserved[GST_PADDING];
};

void gst_validate_report_add_message (GstValidateReport *report,
    const gchar *message);

#define GST_VALIDATE_ISSUE_FORMAT G_GUINTPTR_FORMAT " (%s) : %s: %s"
#define GST_VALIDATE_ISSUE_ARGS(i) gst_validate_issue_get_id (i), \
                                   gst_validate_report_level_get_name (i->default_level), \
                                   i->area, \
                                   i->summary

#define GST_VALIDATE_ERROR_REPORT_PRINT_FORMAT GST_TIME_FORMAT " <%s>: %" GST_VALIDATE_ISSUE_FORMAT ": %s"
#define GST_VALIDATE_REPORT_PRINT_ARGS(r) GST_TIME_ARGS (r->timestamp), \
                                    gst_validate_reporter_get_name (r->reporter), \
                                    GST_VALIDATE_ISSUE_ARGS (r->issue), \
                                    r->message
void               gst_validate_report_init (void);
GstValidateIssue  *gst_validate_issue_from_id (GstValidateIssueId issue_id);
GstValidateIssueId gst_validate_issue_get_id (GstValidateIssue * issue);
void               gst_validate_issue_register (GstValidateIssue * issue);
GstValidateIssue  *gst_validate_issue_new (GstValidateIssueId issue_id, const gchar * summary,
					   const gchar * description,
					   GstValidateReportLevel default_level);
void gst_validate_issue_set_default_level (GstValidateIssue *issue,
                                           GstValidateReportLevel default_level);

GstValidateReport *gst_validate_report_new (GstValidateIssue * issue,
              GstValidateReporter * reporter,
              const gchar * message);
void               gst_validate_report_unref (GstValidateReport * report);
GstValidateReport *gst_validate_report_ref   (GstValidateReport * report);

GstValidateIssueId gst_validate_report_get_issue_id (GstValidateReport * report);

gboolean           gst_validate_report_check_abort (GstValidateReport * report);
void               gst_validate_report_printf (GstValidateReport * report);
void               gst_validate_report_print_level (GstValidateReport *report);
void               gst_validate_report_print_detected_on (GstValidateReport *report);
void               gst_validate_report_print_details (GstValidateReport *report);
void               gst_validate_report_print_description (GstValidateReport *report);

const gchar *      gst_validate_report_level_get_name (GstValidateReportLevel level);

void               gst_validate_printf        (gpointer source,
                                               const gchar      * format,
                                               ...) G_GNUC_PRINTF (2, 3) G_GNUC_NO_INSTRUMENT;
void               gst_validate_print_action  (GstValidateAction *action, const gchar * message);
void               gst_validate_printf_valist (gpointer source,
                                               const gchar      * format,
                                               va_list            args) G_GNUC_NO_INSTRUMENT;
gboolean gst_validate_report_should_print (GstValidateReport * report);
gboolean gst_validate_report_set_master_report(GstValidateReport *report, GstValidateReport *master_report);
void gst_validate_report_set_reporting_level (GstValidateReport *report, GstValidateReportingDetails level);
void gst_validate_report_add_repeated_report (GstValidateReport *report, GstValidateReport *repeated_report);
GstValidateReportLevel gst_validate_report_level_from_name (const gchar *issue_name);

G_END_DECLS

#endif /* __GST_VALIDATE_REPORT_H__ */

