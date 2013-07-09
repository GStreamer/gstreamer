/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-runner.h - QA Runner class
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

#ifndef __GST_QA_RUNNER_H__
#define __GST_QA_RUNNER_H__

#include <glib-object.h>
#include <gst/gst.h>

#include "gst-qa-element-monitor.h"

G_BEGIN_DECLS

#define GST_TYPE_QA_RUNNER			(gst_qa_runner_get_type ())
#define GST_IS_QA_RUNNER(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_RUNNER))
#define GST_IS_QA_RUNNER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_RUNNER))
#define GST_QA_RUNNER_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_RUNNER, GstQaRunnerClass))
#define GST_QA_RUNNER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_RUNNER, GstQaRunner))
#define GST_QA_RUNNER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_RUNNER, GstQaRunnerClass))
#define GST_QA_RUNNER_CAST(obj)                 ((GstQaRunner*)(obj))
#define GST_QA_RUNNER_CLASS_CAST(klass)         ((GstQaRunnerClass*)(klass))

typedef struct _GstQaRunner GstQaRunner;
typedef struct _GstQaRunnerClass GstQaRunnerClass;

/* TODO hide this to be opaque? */
/**
 * GstQaRunner:
 *
 * GStreamer QA Runner class.
 *
 * Class that manages a QA test run for some pipeline
 */
struct _GstQaRunner {
  GObject 	 object;

  gboolean       setup;

  /*< private >*/
  GstElement    *pipeline;
  GstQaElementMonitor *monitor;
};

/**
 * GstQaRunnerClass:
 * @parent_class: parent
 *
 * GStreamer QA Runner object class.
 */
struct _GstQaRunnerClass {
  GObjectClass	parent_class;
};

/* normal GObject stuff */
GType		gst_qa_runner_get_type		(void);

GstQaRunner *   gst_qa_runner_new               (GstElement * pipeline);
gboolean        gst_qa_runner_setup             (GstQaRunner * runner);

G_END_DECLS

#endif /* __GST_QA_RUNNER_H__ */

