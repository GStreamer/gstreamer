/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-runner.c - QA Runner class
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

#include "gst-qa-runner.h"
#include "gst-qa-wrapper-factory.h"

/**
 * SECTION:gst-qa-runner
 * @short_description: Class that runs Gst QA tests for a pipeline
 *
 * TODO
 */

GST_DEBUG_CATEGORY_STATIC (gst_qa_runner_debug);
#define GST_CAT_DEFAULT gst_qa_runner_debug

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_qa_runner_debug, "qa_runner", 0, "QA Runner");
#define gst_qa_runner_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQaRunner, gst_qa_runner, G_TYPE_OBJECT, _do_init);

static void
gst_qa_runner_dispose (GObject * object)
{
  GstQaRunner *runner = GST_QA_RUNNER_CAST (object);
  if (runner->pipeline)
    gst_object_unref (runner->pipeline);

  if (runner->wrapper)
    g_object_unref (runner->wrapper);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_qa_runner_class_init (GstQaRunnerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_qa_runner_dispose;
}

static void
gst_qa_runner_init (GstQaRunner * runner)
{
  runner->setup = FALSE;
}

/**
 * gst_qa_runner_new:
 * @pipeline: (transfer-full): a #GstElement to run QA on
 */
GstQaRunner *
gst_qa_runner_new (GstElement * pipeline)
{
  GstQaRunner *runner = g_object_new (GST_TYPE_QA_RUNNER, NULL);

  g_return_val_if_fail (pipeline != NULL, NULL);

  runner->pipeline = pipeline;
  return runner;
}

gboolean
gst_qa_runner_setup (GstQaRunner * runner)
{
  if (runner->setup)
    return TRUE;

  GST_INFO_OBJECT (runner, "Starting QA Runner setup");
  runner->wrapper = gst_qa_wrapper_factory_create (runner->pipeline);
  if (runner->wrapper == NULL) {
    GST_WARNING_OBJECT (runner, "Setup failed");
    return FALSE;
  }

  runner->setup = TRUE;
  GST_DEBUG_OBJECT (runner, "Setup successful");
  return TRUE;
}
