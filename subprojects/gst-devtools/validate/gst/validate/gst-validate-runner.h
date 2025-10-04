/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-runner.h - Validate Runner class
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

#ifndef __GST_VALIDATE_RUNNER_H__
#define __GST_VALIDATE_RUNNER_H__

#include <glib-object.h>
#include <gst/gst.h>

#include <gst/gsttracer.h>

typedef struct _GstValidateRunner GstValidateRunner;
typedef struct _GstValidateRunnerClass GstValidateRunnerClass;

#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-enums.h>

G_BEGIN_DECLS

#ifndef __GI_SCANNER__
#define GST_TYPE_VALIDATE_RUNNER			(gst_validate_runner_get_type ())
#define GST_IS_VALIDATE_RUNNER(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_RUNNER))
#define GST_IS_VALIDATE_RUNNER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_RUNNER))
#define GST_VALIDATE_RUNNER_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_RUNNER, GstValidateRunnerClass))
#define GST_VALIDATE_RUNNER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_RUNNER, GstValidateRunner))
#define GST_VALIDATE_RUNNER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_RUNNER, GstValidateRunnerClass))
#define GST_VALIDATE_RUNNER_CAST(obj)                 ((GstValidateRunner*)(obj))
#define GST_VALIDATE_RUNNER_CLASS_CAST(klass)         ((GstValidateRunnerClass*)(klass))
#endif

typedef struct _GstValidateRunnerPrivate GstValidateRunnerPrivate;

/**
 * GstValidateRunner:
 *
 * GStreamer Validate Runner class.
 *
 * Class that manages a Validate test run for some pipeline
 */
struct _GstValidateRunner {
  GstTracer 	 object;

  /* <private> */
  GstValidateRunnerPrivate *priv;

  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstValidateRunnerClass:
 * @parent_class: parent
 *
 * GStreamer Validate Runner object class.
 */
struct _GstValidateRunnerClass {
  GstTracerClass	parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* normal GObject stuff */
GST_VALIDATE_API
GType		gst_validate_runner_get_type		(void);

GST_VALIDATE_API
GstValidateRunner *   gst_validate_runner_new               (void) G_GNUC_WARN_UNUSED_RESULT;

GST_VALIDATE_API
void            gst_validate_runner_add_report  (GstValidateRunner * runner, GstValidateReport * report);

GST_VALIDATE_API
guint           gst_validate_runner_get_reports_count (GstValidateRunner * runner);
GST_VALIDATE_API
GList *         gst_validate_runner_get_reports (GstValidateRunner * runner) G_GNUC_WARN_UNUSED_RESULT;

GST_VALIDATE_API
int             gst_validate_runner_printf (GstValidateRunner * runner);
GST_VALIDATE_API
int             gst_validate_runner_exit (GstValidateRunner * runner, gboolean print_result);

GST_VALIDATE_API
GstValidateReportingDetails gst_validate_runner_get_default_reporting_level (GstValidateRunner *runner);
GST_VALIDATE_API
GstValidateReportingDetails gst_validate_runner_get_reporting_level_for_name (GstValidateRunner *runner,
                                                                            const gchar *name);

G_END_DECLS

#endif /* __GST_VALIDATE_RUNNER_H__ */

