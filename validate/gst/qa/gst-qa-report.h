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

typedef enum {
  GST_QA_ERROR_AREA_EVENT=0,
  GST_QA_ERROR_AREA_BUFFER,
  GST_QA_ERROR_AREA_QUERY,
  GST_QA_ERROR_AREA_OTHER=100,
} GstQaErrorArea;

typedef struct {
  GstQaErrorArea area;
  gchar *message;
  gchar *detail;
  GDateTime *timestamp;

  GstObject *source;
} GstQaErrorReport;

#define GST_QA_ERROR_REPORT_PRINT_FORMAT "%04d-%02d-%02dT%02d:%02d:%02d.%06d: %s, %s(%d)) %s (%s)"
#define GST_QA_REPORT_PRINT_ARGS(r) g_date_time_get_year (r->timestamp), \
                                    g_date_time_get_month (r->timestamp), \
                                    g_date_time_get_day_of_month (r->timestamp), \
                                    g_date_time_get_hour (r->timestamp), \
                                    g_date_time_get_minute (r->timestamp), \
                                    g_date_time_get_second (r->timestamp), \
                                    g_date_time_get_microsecond (r->timestamp), \
                                    r->source ? GST_OBJECT_NAME(r->source) : "null", \
                                    gst_qa_error_area_get_name(r->area), r->area, \
                                    r->message, r->detail

GstQaErrorReport * gst_qa_error_report_new (GstObject * source, GstQaErrorArea area, const gchar * message, const gchar * detail);
void               gst_qa_error_report_free (GstQaErrorReport * report);

void               gst_qa_error_report_printf (GstQaErrorReport * report);

G_END_DECLS

#endif /* __GST_QA_REPORT_H__ */

