/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-monitor-report.h - QA Element report structures and functions
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

#ifndef __GST_QA_REPORT_H__
#define __GST_QA_REPORT_H__

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/* forward declaration */
typedef struct _GstQaReporter GstQaReporter;

GType           gst_qa_report_get_type (void);
#define GST_TYPE_QA_REPORT (gst_qa_report_get_type ())

typedef enum {
  GST_QA_FATAL_DEFAULT = 0,
  GST_QA_FATAL_ISSUES = 1 << 0,
  GST_QA_FATAL_WARNINGS = 1 << 1,
  GST_QA_FATAL_CRITICALS = 1 << 2
} GstQaDebugFlags;

typedef enum {
  GST_QA_REPORT_LEVEL_CRITICAL,
  GST_QA_REPORT_LEVEL_WARNING,
  GST_QA_REPORT_LEVEL_ISSUE,
  GST_QA_REPORT_LEVEL_IGNORE,
  GST_QA_REPORT_LEVEL_NUM_ENTRIES,
} GstQaReportLevel;

typedef enum {
  GST_QA_AREA_EVENT=1,
  GST_QA_AREA_BUFFER,
  GST_QA_AREA_QUERY,
  GST_QA_AREA_CAPS,
  GST_QA_AREA_SEEK,
  GST_QA_AREA_OTHER=100,
} GstQaReportArea;

typedef guint64 GstQaIssueId;
#define GST_QA_ISSUE_ID_UNKNOWN 0

#define GST_QA_ISSUE_ID_BUFFER_BEFORE_SEGMENT                    (((guint64) GST_QA_AREA_BUFFER) << 32 | 1)
#define GST_QA_ISSUE_ID_BUFFER_IS_OUT_OF_SEGMENT                 (((guint64) GST_QA_AREA_BUFFER) << 32 | 2)
#define GST_QA_ISSUE_ID_BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE   (((guint64) GST_QA_AREA_BUFFER) << 32 | 3)
#define GST_QA_ISSUE_ID_FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO    (((guint64) GST_QA_AREA_BUFFER) << 32 | 4)
#define GST_QA_ISSUE_ID_WRONG_FLOW_RETURN                        (((guint64) GST_QA_AREA_BUFFER) << 32 | 5)

#define GST_QA_ISSUE_ID_CAPS_IS_MISSING_FIELD         (((guint64) GST_QA_AREA_CAPS) << 32 | 1)
#define GST_QA_ISSUE_ID_CAPS_FIELD_HAS_BAD_TYPE       (((guint64) GST_QA_AREA_CAPS) << 32 | 2)
#define GST_QA_ISSUE_ID_CAPS_EXPECTED_FIELD_NOT_FOUND (((guint64) GST_QA_AREA_CAPS) << 32 | 3)
#define GST_QA_ISSUE_ID_GET_CAPS_NOT_PROXYING_FIELDS  (((guint64) GST_QA_AREA_CAPS) << 32 | 4)
#define GST_QA_ISSUE_ID_CAPS_FIELD_UNEXPECTED_VALUE   (((guint64) GST_QA_AREA_CAPS) << 32 | 5)

#define GST_QA_ISSUE_ID_EVENT_NEWSEGMENT_NOT_PUSHED           (((guint64) GST_QA_AREA_EVENT) << 32 | 1)
#define GST_QA_ISSUE_ID_SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME (((guint64) GST_QA_AREA_EVENT) << 32 | 2)
#define GST_QA_ISSUE_ID_EVENT_HAS_WRONG_SEQNUM                (((guint64) GST_QA_AREA_EVENT) << 32 | 3)
#define GST_QA_ISSUE_ID_EVENT_SERIALIZED_OUT_OF_ORDER         (((guint64) GST_QA_AREA_EVENT) << 32 | 4)
#define GST_QA_ISSUE_ID_EVENT_NEW_SEGMENT_MISMATCH            (((guint64) GST_QA_AREA_EVENT) << 32 | 5)
#define GST_QA_ISSUE_ID_EVENT_FLUSH_START_UNEXPECTED          (((guint64) GST_QA_AREA_EVENT) << 32 | 6)
#define GST_QA_ISSUE_ID_EVENT_FLUSH_STOP_UNEXPECTED           (((guint64) GST_QA_AREA_EVENT) << 32 | 7)

#define GST_QA_ISSUE_ID_EVENT_SEEK_NOT_HANDLED           (((guint64) GST_QA_AREA_SEEK) << 32 | 1)
#define GST_QA_ISSUE_ID_EVENT_SEEK_RESULT_POSITION_WRONG (((guint64) GST_QA_AREA_SEEK) << 32 | 2)

#define GST_QA_ISSUE_ID_AREA(id) ((guint32)(id >> 32))

typedef struct {
  GstQaIssueId issue_id;

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
  GstQaReportLevel default_level;

  /* repeat: whether the issue might be triggered
  * multiple times but only remembered once */
  gboolean repeat;
} GstQaIssue;

#define GST_QA_ISSUE_AREA(i) (GST_QA_ISSUE_ID_AREA (gst_qa_issue_get_id (i)))

typedef struct {
  gint    refcount;

  /* issue: The issue this report corresponds to (to get dsecription, summary,...) */
  GstQaIssue *issue;
 
  /* The reporter that reported the issue (to get names, info, ...) */
  GstQaReporter *reporter;
 
  /* timestamp: The time at which this issue happened since
   * the process start (to stay in sync with gst logging) */
  GstClockTime timestamp;
  
  /* message: issue-specific message. Gives more detail on the actual
   * issue. Can be NULL */
  gchar *message;
} GstQaReport;

#define GST_QA_ISSUE_FORMAT G_GUINT64_FORMAT " (%s) : %s(%u): %s"
#define GST_QA_ISSUE_ARGS(i) gst_qa_issue_get_id (i), gst_qa_report_level_get_name (i->default_level), \
                             gst_qa_report_area_get_name (GST_QA_ISSUE_AREA (i)), GST_QA_ISSUE_AREA (i), \
                             i->summary

#define GST_QA_ERROR_REPORT_PRINT_FORMAT GST_TIME_FORMAT " <%s>: %" GST_QA_ISSUE_FORMAT ": %s"
#define GST_QA_REPORT_PRINT_ARGS(r) GST_TIME_ARGS (r->timestamp), \
                                    gst_qa_reporter_get_name (r->reporter), \
                                    GST_QA_ISSUE_ARGS (r->issue), \
                                    r->message

void               gst_qa_report_init (void);
GstQaIssue *       gst_qa_issue_from_id (GstQaIssueId issue_id);
GstQaIssueId       gst_qa_issue_get_id (GstQaIssue * issue);

GstQaReport *      gst_qa_report_new (GstQaIssue * issue,
                                      GstQaReporter * reporter,
                                      const gchar * message);
void               gst_qa_report_unref (GstQaReport * report);
GstQaReport *      gst_qa_report_ref   (GstQaReport * report);

GstQaIssueId       gst_qa_report_get_issue_id (GstQaReport * report);

void               gst_qa_report_printf (GstQaReport * report);


const gchar *      gst_qa_report_level_get_name (GstQaReportLevel level);
const gchar *      gst_qa_report_area_get_name (GstQaReportArea area);
const gchar *      gst_qa_report_subarea_get_name (GstQaReportArea area, gint subarea);

G_END_DECLS

#endif /* __GST_QA_REPORT_H__ */

