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

G_BEGIN_DECLS

GType           gst_validate_report_get_type (void);
#define GST_TYPE_VALIDATE_REPORT (gst_validate_report_get_type ())

typedef enum {
  GST_VALIDATE_FATAL_DEFAULT = 0,
  GST_VALIDATE_FATAL_ISSUES = 1 << 0,
  GST_VALIDATE_FATAL_WARNINGS = 1 << 1,
  GST_VALIDATE_FATAL_CRITICALS = 1 << 2
} GstValidateDebugFlags;

typedef enum {
  GST_VALIDATE_REPORT_LEVEL_CRITICAL,
  GST_VALIDATE_REPORT_LEVEL_WARNING,
  GST_VALIDATE_REPORT_LEVEL_ISSUE,
  GST_VALIDATE_REPORT_LEVEL_IGNORE,
  GST_VALIDATE_REPORT_LEVEL_NUM_ENTRIES,
} GstValidateReportLevel;

typedef enum {
  GST_VALIDATE_AREA_EVENT=1,
  GST_VALIDATE_AREA_BUFFER,
  GST_VALIDATE_AREA_QUERY,
  GST_VALIDATE_AREA_CAPS,
  GST_VALIDATE_AREA_SEEK,
  GST_VALIDATE_AREA_STATE,
  GST_VALIDATE_AREA_FILE_CHECK,
  GST_VALIDATE_AREA_SCENARIO,
  GST_VALIDATE_AREA_RUN_ERROR,
  GST_VALIDATE_AREA_OTHER=100,
} GstValidateReportArea;

#define GST_VALIDATE_ISSUE_ID_UNKNOWN 0

#define GST_VALIDATE_ISSUE_ID_SHIFT 16
#define GST_VALIDATE_ISSUE_ID_CUSTOM_FIRST (2 << 15)

#define GST_VALIDATE_ISSUE_ID_BUFFER_BEFORE_SEGMENT                    (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_BUFFER_IS_OUT_OF_SEGMENT                 (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE   (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)
#define GST_VALIDATE_ISSUE_ID_FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO    (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 4)
#define GST_VALIDATE_ISSUE_ID_WRONG_FLOW_RETURN                        (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 5)
#define GST_VALIDATE_ISSUE_ID_BUFFER_AFTER_EOS                         (((GstValidateIssueId) GST_VALIDATE_AREA_BUFFER) << GST_VALIDATE_ISSUE_ID_SHIFT | 6)

#define GST_VALIDATE_ISSUE_ID_CAPS_IS_MISSING_FIELD         (((GstValidateIssueId) GST_VALIDATE_AREA_CAPS) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_CAPS_FIELD_HAS_BAD_TYPE       (((GstValidateIssueId) GST_VALIDATE_AREA_CAPS) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_CAPS_EXPECTED_FIELD_NOT_FOUND (((GstValidateIssueId) GST_VALIDATE_AREA_CAPS) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)
#define GST_VALIDATE_ISSUE_ID_GET_CAPS_NOT_PROXYING_FIELDS  (((GstValidateIssueId) GST_VALIDATE_AREA_CAPS) << GST_VALIDATE_ISSUE_ID_SHIFT | 4)
#define GST_VALIDATE_ISSUE_ID_CAPS_FIELD_UNEXPECTED_VALUE   (((GstValidateIssueId) GST_VALIDATE_AREA_CAPS) << GST_VALIDATE_ISSUE_ID_SHIFT | 5)

#define GST_VALIDATE_ISSUE_ID_EVENT_NEWSEGMENT_NOT_PUSHED           (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_EVENT_HAS_WRONG_SEQNUM                (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)
#define GST_VALIDATE_ISSUE_ID_EVENT_SERIALIZED_OUT_OF_ORDER         (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 4)
#define GST_VALIDATE_ISSUE_ID_EVENT_NEW_SEGMENT_MISMATCH            (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 5)
#define GST_VALIDATE_ISSUE_ID_EVENT_FLUSH_START_UNEXPECTED          (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 6)
#define GST_VALIDATE_ISSUE_ID_EVENT_FLUSH_STOP_UNEXPECTED           (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 7)
#define GST_VALIDATE_ISSUE_ID_EVENT_CAPS_DUPLICATE                  (((GstValidateIssueId) GST_VALIDATE_AREA_EVENT) << GST_VALIDATE_ISSUE_ID_SHIFT | 8)

#define GST_VALIDATE_ISSUE_ID_EVENT_SEEK_NOT_HANDLED           (((GstValidateIssueId) GST_VALIDATE_AREA_SEEK) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_EVENT_SEEK_RESULT_POSITION_WRONG (((GstValidateIssueId) GST_VALIDATE_AREA_SEEK) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)

#define GST_VALIDATE_ISSUE_ID_STATE_CHANGE_FAILURE (((GstValidateIssueId) GST_VALIDATE_AREA_STATE) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)

#define GST_VALIDATE_ISSUE_ID_FILE_SIZE_IS_ZERO    (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_FILE_SIZE_INCORRECT      (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_FILE_DURATION_INCORRECT  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)
#define GST_VALIDATE_ISSUE_ID_FILE_SEEKABLE_INCORRECT  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 4)
#define GST_VALIDATE_ISSUE_ID_FILE_PROFILE_INCORRECT  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 5)
#define GST_VALIDATE_ISSUE_ID_FILE_NOT_FOUND  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 6)
#define GST_VALIDATE_ISSUE_ID_FILE_CHECK_FAILURE  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 7)
#define GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_START_FAILURE (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 8)
#define GST_VALIDATE_ISSUE_ID_FILE_PLAYBACK_ERROR (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 9)
#define GST_VALIDATE_ISSUE_ID_FILE_NO_STREAM_ID  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 10)
#define GST_VALIDATE_ISSUE_ID_FILE_TAG_DETECTION_INCORRECT  (((GstValidateIssueId) GST_VALIDATE_AREA_FILE_CHECK) << GST_VALIDATE_ISSUE_ID_SHIFT | 11)

#define GST_VALIDATE_ISSUE_ID_ALLOCATION_FAILURE (((GstValidateIssueId) GST_VALIDATE_AREA_RUN_ERROR) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_MISSING_PLUGIN     (((GstValidateIssueId) GST_VALIDATE_AREA_RUN_ERROR) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_WARNING_ON_BUS     (((GstValidateIssueId) GST_VALIDATE_AREA_RUN_ERROR) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)
#define GST_VALIDATE_ISSUE_ID_ERROR_ON_BUS       (((GstValidateIssueId) GST_VALIDATE_AREA_RUN_ERROR) << GST_VALIDATE_ISSUE_ID_SHIFT | 4)

#define GST_VALIDATE_ISSUE_ID_QUERY_POSITION_SUPERIOR_DURATION (((GstValidateIssueId) GST_VALIDATE_AREA_QUERY) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_QUERY_POSITION_OUT_OF_SEGMENT    (((GstValidateIssueId) GST_VALIDATE_AREA_QUERY) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)

#define GST_VALIDATE_ISSUE_ID_SCENARIO_NOT_ENDED               (((GstValidateIssueId) GST_VALIDATE_AREA_SCENARIO) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_SCENARIO_ACTION_EXECUTION_ERROR  (((GstValidateIssueId) GST_VALIDATE_AREA_SCENARIO) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_SCENARIO_ACTION_EXECUTION_ISSUE  (((GstValidateIssueId) GST_VALIDATE_AREA_SCENARIO) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)

#define GST_VALIDATE_ISSUE_ID_G_LOG_ISSUE  (((GstValidateIssueId) GST_VALIDATE_AREA_OTHER) << GST_VALIDATE_ISSUE_ID_SHIFT | 1)
#define GST_VALIDATE_ISSUE_ID_G_LOG_WARNING   (((GstValidateIssueId) GST_VALIDATE_AREA_OTHER) << GST_VALIDATE_ISSUE_ID_SHIFT | 2)
#define GST_VALIDATE_ISSUE_ID_G_LOG_CRITICAL  (((GstValidateIssueId) GST_VALIDATE_AREA_OTHER) << GST_VALIDATE_ISSUE_ID_SHIFT | 3)

#define GST_VALIDATE_ISSUE_ID_AREA(id) ((guintptr)(id >> GST_VALIDATE_ISSUE_ID_SHIFT))

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

  /* default_level: The default level of severity for this
  * issue. */
  GstValidateReportLevel default_level;

  /* repeat: whether the issue might be triggered
  * multiple times but only remembered once */
  gboolean repeat;
} GstValidateIssue;

#define GST_VALIDATE_ISSUE_AREA(i) (GST_VALIDATE_ISSUE_ID_AREA (gst_validate_issue_get_id (i)))

struct _GstValidateReport {
  gint    refcount;

  /* issue: The issue this report corresponds to (to get dsecription, summary,...) */
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
};

#define GST_VALIDATE_ISSUE_FORMAT G_GUINTPTR_FORMAT " (%s) : %s(%" G_GUINTPTR_FORMAT "): %s"
#define GST_VALIDATE_ISSUE_ARGS(i) gst_validate_issue_get_id (i), gst_validate_report_level_get_name (i->default_level), \
                             gst_validate_report_area_get_name (GST_VALIDATE_ISSUE_AREA (i)), GST_VALIDATE_ISSUE_AREA (i), \
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

GstValidateReport *gst_validate_report_new (GstValidateIssue * issue,
					    GstValidateReporter * reporter,
					    const gchar * message);
void               gst_validate_report_unref (GstValidateReport * report);
GstValidateReport *gst_validate_report_ref   (GstValidateReport * report);

GstValidateIssueId gst_validate_report_get_issue_id (GstValidateReport * report);

gboolean           gst_validate_report_check_abort (GstValidateReport * report);
void               gst_validate_report_printf (GstValidateReport * report);

const gchar *      gst_validate_report_level_get_name (GstValidateReportLevel level);
const gchar *      gst_validate_report_area_get_name (GstValidateReportArea area);
const gchar *      gst_validate_report_subarea_get_name (GstValidateReportArea area, gint subarea);

void               gst_validate_printf        (gpointer source,
                                               const gchar      * format,
                                               ...) G_GNUC_PRINTF (2, 3) G_GNUC_NO_INSTRUMENT;
void               gst_validate_printf_valist (gpointer source,
                                               const gchar      * format,
                                               va_list            args) G_GNUC_NO_INSTRUMENT;

G_END_DECLS

#endif /* __GST_VALIDATE_REPORT_H__ */

