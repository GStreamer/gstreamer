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
typedef struct _GstQaMonitor GstQaMonitor;

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
  GST_QA_REPORT_LEVEL_NUM_ENTRIES,
} GstQaReportLevel;

typedef enum {
  GST_QA_AREA_EVENT=0,
  GST_QA_AREA_BUFFER,
  GST_QA_AREA_QUERY,
  GST_QA_AREA_CAPS_NEGOTIATION,
  GST_QA_AREA_OTHER=100,
} GstQaReportArea;

typedef enum {
  GST_QA_AREA_EVENT_SEQNUM,
  GST_QA_AREA_EVENT_UNEXPECTED,
  GST_QA_AREA_EVENT_EXPECTED,

  GST_QA_AREA_EVENT_NUM_ENTRIES
} GstQaReportAreaEvent;

typedef enum {
  GST_QA_AREA_BUFFER_TIMESTAMP,
  GST_QA_AREA_BUFFER_DURATION,
  GST_QA_AREA_BUFFER_FLAGS,
  GST_QA_AREA_BUFFER_UNEXPECTED,

  GST_QA_AREA_BUFFER_NUM_ENTRIES
} GstQaReportAreaBuffer;

typedef enum {
  GST_QA_AREA_QUERY_UNEXPECTED,

  GST_QA_AREA_QUERY_NUM_ENTRIES
} GstQaReportAreaQuery;

typedef enum {
  GST_QA_AREA_CAPS_NEGOTIATION_MISSING_FIELD,
  GST_QA_AREA_CAPS_NEGOTIATION_BAD_FIELD_TYPE,

  GST_QA_AREA_CAPS_NEGOTIATION_NUM_ENTRIES
} GstQaReportAreaCapsNegotiation;

typedef struct {
  gint    refcount;

  GstQaReportLevel level;
  GstQaReportArea area;
  gint subarea;
  gchar *message;

  gchar *source_name;
  guint64 timestamp;
} GstQaReport;

#define GST_QA_ERROR_REPORT_PRINT_FORMAT GST_TIME_FORMAT " (%s): %s, %s(%d)) %s(%d): %s"
#define GST_QA_REPORT_PRINT_ARGS(r) GST_TIME_ARGS (r->timestamp), \
                                    gst_qa_report_level_get_name (r->level), \
                                    r->source_name, \
                                    gst_qa_report_area_get_name(r->area), r->area, \
                                    gst_qa_report_subarea_get_name(r->area, r->subarea), r->subarea, \
                                    r->message

void               gst_qa_report_init (void);
GstQaReport *      gst_qa_report_new (GstQaMonitor * monitor, GstQaReportLevel level,
                                      GstQaReportArea area,
                                      gint subarea, const gchar * message);
void               gst_qa_report_unref (GstQaReport * report);
GstQaReport *      gst_qa_report_ref   (GstQaReport * report);

void               gst_qa_report_printf (GstQaReport * report);

G_END_DECLS

#endif /* __GST_QA_REPORT_H__ */

