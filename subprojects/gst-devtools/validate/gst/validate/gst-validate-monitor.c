/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor.c - Validate Monitor class
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

#include "gst-validate-enum-types.h"
#include "gst-validate-internal.h"
#include "gst-validate-monitor.h"
#include "gst-validate-override-registry.h"

/**
 * SECTION:gst-validate-monitor
 * @short_description: Base class that wraps a #GObject for Validate checks
 *
 * TODO
 */

enum
{
  PROP_0,
  PROP_OBJECT,
  PROP_PIPELINE,
  PROP_RUNNER,
  PROP_VALIDATE_PARENT,
  PROP_VERBOSITY,
  PROP_LAST
};

static gboolean gst_validate_monitor_do_setup (GstValidateMonitor * monitor);
static void
gst_validate_monitor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void
gst_validate_monitor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GObject *gst_validate_monitor_constructor (GType type,
    guint n_construct_params, GObjectConstructParam * construct_params);

static gboolean gst_validate_monitor_setup (GstValidateMonitor * monitor);

static GstValidateInterceptionReturn
gst_validate_monitor_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report);

#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, _reporter_iface_init)

static GstValidateReportingDetails
_get_reporting_level (GstValidateReporter * monitor)
{
  return GST_VALIDATE_MONITOR (monitor)->level;
}

/**
 * gst_validate_monitor_get_pipeline:
 * @monitor: The monitor to get the pipeline from
 *
 * Returns: (transfer full) (nullable): The pipeline in which @monitor
 * target is in.
 */
GstPipeline *
gst_validate_monitor_get_pipeline (GstValidateMonitor * monitor)
{
  return g_weak_ref_get (&monitor->pipeline);
}

/**
 * gst_validate_monitor_get_target:
 * @monitor: The monitor to get the target from
 *
 * Returns: (transfer full) (nullable): The target object
 */
GstObject *
gst_validate_monitor_get_target (GstValidateMonitor * monitor)
{
  return g_weak_ref_get (&monitor->target);
}

static GstPipeline *
_get_pipeline (GstValidateReporter * monitor)
{
  return g_weak_ref_get (&(GST_VALIDATE_MONITOR (monitor)->pipeline));
}

static void
_reporter_iface_init (GstValidateReporterInterface * iface)
{
  iface->intercept_report = gst_validate_monitor_intercept_report;
  iface->get_reporting_level = _get_reporting_level;
  iface->get_pipeline = _get_pipeline;
}

#define gst_validate_monitor_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstValidateMonitor, gst_validate_monitor,
    GST_TYPE_OBJECT, _do_init);

static void
gst_validate_monitor_dispose (GObject * object)
{
  GstValidateMonitor *monitor = GST_VALIDATE_MONITOR_CAST (object);

  g_mutex_clear (&monitor->mutex);
  g_mutex_clear (&monitor->overrides_mutex);
  g_queue_clear (&monitor->overrides);

  g_weak_ref_clear (&monitor->pipeline);
  g_weak_ref_clear (&monitor->target);

  if (monitor->media_descriptor)
    gst_object_unref (monitor->media_descriptor);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_validate_monitor_finalize (GObject * object)
{
  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (object), NULL);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_validate_monitor_class_init (GstValidateMonitorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_validate_monitor_get_property;
  gobject_class->set_property = gst_validate_monitor_set_property;
  gobject_class->dispose = gst_validate_monitor_dispose;
  gobject_class->finalize = gst_validate_monitor_finalize;
  gobject_class->constructor = gst_validate_monitor_constructor;

  klass->setup = gst_validate_monitor_do_setup;

  g_object_class_install_property (gobject_class, PROP_OBJECT,
      g_param_spec_object ("object", "Object", "The object to be monitored",
          G_TYPE_OBJECT, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PIPELINE,
      g_param_spec_object ("pipeline", "Pipeline", "The pipeline in which the"
          "monitored object is", GST_TYPE_PIPELINE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VALIDATE_PARENT,
      g_param_spec_object ("validate-parent", "VALIDATE parent monitor",
          "The Validate monitor that is the parent of this one",
          GST_TYPE_VALIDATE_MONITOR,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VERBOSITY,
      g_param_spec_flags ("verbosity", "Verbosity",
          "The verbosity of GstValidate on the monitor",
          GST_TYPE_VALIDATE_VERBOSITY_FLAGS,
          GST_VALIDATE_VERBOSITY_POSITION, G_PARAM_READWRITE));
}

static GObject *
gst_validate_monitor_constructor (GType type, guint n_construct_params,
    GObjectConstructParam * construct_params)
{
  GstObject *target;
  GstValidateMonitor *monitor =
      GST_VALIDATE_MONITOR_CAST (G_OBJECT_CLASS (parent_class)->constructor
      (type,
          n_construct_params,
          construct_params));

  if (monitor->parent) {
    GstPipeline *parent_pipeline =
        gst_validate_monitor_get_pipeline (monitor->parent);

    gst_validate_monitor_set_media_descriptor (monitor,
        monitor->parent->media_descriptor);

    if (parent_pipeline) {
      g_weak_ref_init (&monitor->pipeline, parent_pipeline);

      gst_object_unref (parent_pipeline);
    }
  }

  gst_validate_monitor_setup (monitor);
  gst_validate_override_registry_attach_overrides (monitor);

  target = gst_validate_monitor_get_target (monitor);
  g_object_set_data ((GObject *) target, "validate-monitor", monitor);
  gst_object_unref (target);

  return (GObject *) monitor;
}

static void
gst_validate_monitor_init (GstValidateMonitor * monitor)
{
  g_mutex_init (&monitor->mutex);

  g_mutex_init (&monitor->overrides_mutex);
  g_queue_init (&monitor->overrides);

  monitor->verbosity = GST_VALIDATE_VERBOSITY_POSITION;
}

static gboolean
gst_validate_monitor_do_setup (GstValidateMonitor * monitor)
{
  /* NOP */
  return TRUE;
}

static GstValidateReportingDetails
_get_report_level_for_pad (GstValidateRunner * runner, GstObject * pad)
{
  gchar *name;
  GstValidateReportingDetails level = GST_VALIDATE_SHOW_UNKNOWN;

  name = g_strdup_printf ("%s__%s", GST_DEBUG_PAD_NAME (pad));
  level = gst_validate_runner_get_reporting_level_for_name (runner, name);

  g_free (name);
  return level;
}

static void
_determine_reporting_level (GstValidateMonitor * monitor)
{
  GstValidateRunner *runner;
  GstObject *object, *parent;
  gchar *object_name;
  GstValidateReportingDetails level = GST_VALIDATE_SHOW_UNKNOWN;

  object = gst_validate_monitor_get_target (monitor);
  runner = gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (monitor));

  do {
    if (!GST_IS_OBJECT (object))
      break;

    /* Let's allow for singling out pads */
    if (GST_IS_PAD (object)) {
      level = _get_report_level_for_pad (runner, object);
      if (level != GST_VALIDATE_SHOW_UNKNOWN)
        break;
    }

    object_name = gst_object_get_name (object);
    level = gst_validate_runner_get_reporting_level_for_name (runner,
        object_name);
    parent = gst_object_get_parent (object);
    gst_object_unref (object);
    object = parent;
    g_free (object_name);
  } while (object && level == GST_VALIDATE_SHOW_UNKNOWN);

  if (object)
    gst_object_unref (object);

  if (runner)
    gst_object_unref (runner);

  monitor->level = level;
}

gboolean
gst_validate_monitor_setup (GstValidateMonitor * monitor)
{
  GList *config;

  GST_DEBUG_OBJECT (monitor, "Starting monitor setup");

  for (config = gst_validate_plugin_get_config (NULL); config;
      config = config->next) {
    const gchar *verbosity =
        gst_structure_get_string (GST_STRUCTURE (config->data),
        "verbosity");

    if (verbosity)
      gst_util_set_object_arg (G_OBJECT (monitor), "verbosity", verbosity);
  }

  /* For now we just need to do this at setup time */
  _determine_reporting_level (monitor);
  return GST_VALIDATE_MONITOR_GET_CLASS (monitor)->setup (monitor);
}

/**
 * gst_validate_monitor_get_element
 * @monitor: The monitor
 *
 * Returns: (transfer none) (nullable): The GstElement associated with @monitor
 */
GstElement *
gst_validate_monitor_get_element (GstValidateMonitor * monitor)
{
  GstValidateMonitorClass *klass = GST_VALIDATE_MONITOR_GET_CLASS (monitor);
  GstElement *element = NULL;

  if (klass->get_element)
    element = klass->get_element (monitor);

  return element;
}

/**
 * gst_validate_monitor_get_element_name
 * @monitor: The monitor
 *
 * Returns: (transfer full) (nullable): The name of the element associated with @monitor
 */
gchar *
gst_validate_monitor_get_element_name (GstValidateMonitor * monitor)
{
  gchar *res = NULL;
  GstElement *element;

  element = gst_validate_monitor_get_element (monitor);
  if (element) {
    res = g_strdup (GST_ELEMENT_NAME (element));
    gst_object_unref (element);
  }

  return res;
}

/* Check if any of our overrides wants to change the report severity */
static GstValidateInterceptionReturn
gst_validate_monitor_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report)
{
  GList *iter;
  GstValidateMonitor *monitor = GST_VALIDATE_MONITOR_CAST (reporter);

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (monitor);
  for (iter = monitor->overrides.head; iter; iter = g_list_next (iter)) {
    report->level =
        gst_validate_override_get_severity (iter->data,
        gst_validate_issue_get_id (report->issue), report->level);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (monitor);

  return GST_VALIDATE_REPORTER_REPORT;
}

void
gst_validate_monitor_attach_override (GstValidateMonitor * monitor,
    GstValidateOverride * override)
{
  GstValidateRunner *runner;
  GstValidateRunner *mrunner;

  if (!gst_validate_override_can_attach (override, monitor)) {
    GST_INFO_OBJECT (monitor, "Can not attach override %s",
        gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (override)));

    return;
  }

  runner = gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (override));
  mrunner = gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (monitor));
  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (monitor);
  if (runner) {
    g_assert (runner == mrunner);
  } else
    gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (override),
        mrunner);
  g_queue_push_tail (&monitor->overrides, override);
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (monitor);

  if (runner)
    gst_object_unref (runner);
  if (mrunner)
    gst_object_unref (mrunner);
  gst_validate_override_attached (override);
}

static void
gst_validate_monitor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstValidateMonitor *monitor;

  monitor = GST_VALIDATE_MONITOR_CAST (object);

  switch (prop_id) {
    case PROP_OBJECT:
    {
      GstObject *target;

      target = g_value_get_object (value);

      g_assert (gst_validate_monitor_get_target (monitor) == NULL);
      g_weak_ref_init (&monitor->target, target);

      if (GST_IS_OBJECT (target))
        gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (monitor),
            gst_object_get_name (target));

      break;
    }
    case PROP_PIPELINE:
      g_weak_ref_init (&monitor->pipeline, g_value_get_object (value));
      break;
    case PROP_RUNNER:
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (monitor),
          g_value_get_object (value));
      break;
    case PROP_VALIDATE_PARENT:
      monitor->parent = g_value_get_object (value);
      break;
    case PROP_VERBOSITY:
      monitor->verbosity = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_validate_monitor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstValidateMonitor *monitor;

  monitor = GST_VALIDATE_MONITOR_CAST (object);

  switch (prop_id) {
    case PROP_OBJECT:
      g_value_take_object (value, gst_validate_monitor_get_target (monitor));
      break;
    case PROP_PIPELINE:
      g_value_take_object (value, gst_validate_monitor_get_pipeline (monitor));
      break;
    case PROP_RUNNER:
      g_value_take_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (monitor)));
      break;
    case PROP_VALIDATE_PARENT:
      g_value_set_object (value, GST_VALIDATE_MONITOR_GET_PARENT (monitor));
      break;
    case PROP_VERBOSITY:
      g_value_set_flags (value, monitor->verbosity);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

void
gst_validate_monitor_set_media_descriptor (GstValidateMonitor * monitor,
    GstValidateMediaDescriptor * media_descriptor)
{
  GstValidateMonitorClass *klass = GST_VALIDATE_MONITOR_GET_CLASS (monitor);

  GST_DEBUG_OBJECT (monitor, "Set media desc: %" GST_PTR_FORMAT,
      media_descriptor);
  if (monitor->media_descriptor)
    gst_object_unref (monitor->media_descriptor);

  if (media_descriptor)
    gst_object_ref (media_descriptor);

  monitor->media_descriptor = media_descriptor;
  if (klass->set_media_descriptor)
    klass->set_media_descriptor (monitor, media_descriptor);
}

GstValidateMonitor *
gst_validate_get_monitor (GObject * object)
{
  return GST_VALIDATE_MONITOR (g_object_get_data (object, "validate-monitor"));
}
