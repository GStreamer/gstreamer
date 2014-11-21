/* GStreamer
 *
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
 *
 * gst-validate-pipeline-monitor.c - Validate PipelineMonitor class
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

#include "gst-validate-internal.h"
#include "gst-validate-pipeline-monitor.h"
#include "gst-validate-monitor-factory.h"

#define PRINT_POSITION_TIMEOUT 250

/**
 * SECTION:gst-validate-pipeline-monitor
 * @short_description: Class that wraps a #GstPipeline for Validate checks
 *
 * TODO
 */

enum
{
  PROP_LAST
};

#define gst_validate_pipeline_monitor_parent_class parent_class
G_DEFINE_TYPE (GstValidatePipelineMonitor, gst_validate_pipeline_monitor,
    GST_TYPE_VALIDATE_BIN_MONITOR);

static void
gst_validate_pipeline_monitor_dispose (GObject * object)
{
  GstValidatePipelineMonitor *monitor =
      GST_VALIDATE_PIPELINE_MONITOR_CAST (object);

  if (monitor->print_pos_srcid) {
    if (g_source_remove (monitor->print_pos_srcid))
      monitor->print_pos_srcid = 0;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_validate_pipeline_monitor_class_init (GstValidatePipelineMonitorClass *
    klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = gst_validate_pipeline_monitor_dispose;
}

static void
gst_validate_pipeline_monitor_init (GstValidatePipelineMonitor *
    pipeline_monitor)
{
}

static gboolean
print_position (GstValidateMonitor * monitor)
{
  GstQuery *query;
  gint64 position, duration;
  GstElement *pipeline =
      GST_ELEMENT (GST_VALIDATE_MONITOR_GET_OBJECT (monitor));

  gdouble rate = 1.0;
  GstFormat format = GST_FORMAT_TIME;

  if (!gst_element_query_position (pipeline, format, &position)) {
    GST_DEBUG_OBJECT (monitor, "Could not query position");

    return TRUE;
  }

  format = GST_FORMAT_TIME;
  if (!gst_element_query_duration (pipeline, format, &duration)) {
    GST_DEBUG_OBJECT (monitor, "Could not query duration");

    return TRUE;
  }

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (pipeline, query))
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);
  gst_query_unref (query);

  gst_validate_printf (NULL,
      "<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " speed: %f />\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
      rate);

  return TRUE;
}

static void
_bus_handler (GstBus * bus, GstMessage * message,
    GstValidatePipelineMonitor * monitor)
{
  GError *err = NULL;
  gchar *debug = NULL;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (message, &err, &debug);
      GST_VALIDATE_REPORT (monitor, ERROR_ON_BUS,
          "Got error: %s -- Debug message: %s", err->message, debug);
      g_error_free (err);
      g_free (debug);
      break;
    case GST_MESSAGE_WARNING:
      gst_message_parse_warning (message, &err, &debug);
      GST_VALIDATE_REPORT (monitor, WARNING_ON_BUS,
          "Got warning: %s -- Debug message: %s", err->message, debug);
      g_error_free (err);
      g_free (debug);
      break;
    case GST_MESSAGE_BUFFERING:
    {
      GstBufferingMode mode;
      gint percent;

      gst_message_parse_buffering (message, &percent);
      gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (monitor->buffering) {
          monitor->print_pos_srcid =
              g_timeout_add (PRINT_POSITION_TIMEOUT,
              (GSourceFunc) print_position, monitor);
          monitor->buffering = FALSE;
        }
      } else {
        /* buffering... */
        if (!monitor->buffering) {
          monitor->buffering = TRUE;
          if (monitor->print_pos_srcid
              && g_source_remove (monitor->print_pos_srcid))
            monitor->print_pos_srcid = 0;
        }
      }
      break;
    }
    default:
      break;
  }
}

static void
gst_validate_pipeline_monitor_create_scenarios (GstValidateBinMonitor * monitor)
{
  /* scenarios currently only make sense for pipelines */
  const gchar *scenario_name;

  if ((scenario_name = g_getenv ("GST_VALIDATE_SCENARIO"))) {
    gchar **scenario_v = g_strsplit (scenario_name, "->", 2);

    if (scenario_v[1] && GST_VALIDATE_MONITOR_GET_OBJECT (monitor)) {
      if (!g_pattern_match_simple (scenario_v[1],
              GST_OBJECT_NAME (GST_VALIDATE_MONITOR_GET_OBJECT (monitor)))) {
        GST_INFO_OBJECT (monitor, "Not attaching to pipeline %" GST_PTR_FORMAT
            " as not matching pattern %s",
            GST_VALIDATE_MONITOR_GET_OBJECT (monitor), scenario_v[1]);

        g_strfreev (scenario_v);
        return;
      }
    }
    monitor->scenario =
        gst_validate_scenario_factory_create (GST_VALIDATE_MONITOR_GET_RUNNER
        (monitor),
        GST_ELEMENT_CAST (GST_VALIDATE_MONITOR_GET_OBJECT (monitor)),
        scenario_v[0]);
    g_strfreev (scenario_v);
  }
}

/**
 * gst_validate_pipeline_monitor_new:
 * @pipeline: (transfer-none): a #GstPipeline to run Validate on
 */
GstValidatePipelineMonitor *
gst_validate_pipeline_monitor_new (GstPipeline * pipeline,
    GstValidateRunner * runner, GstValidateMonitor * parent)
{
  GstBus *bus;
  GstValidatePipelineMonitor *monitor =
      g_object_new (GST_TYPE_VALIDATE_PIPELINE_MONITOR, "object",
      pipeline, "validate-runner", runner, "validate-parent", parent, NULL);

  if (GST_VALIDATE_MONITOR_GET_OBJECT (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }

  gst_validate_pipeline_monitor_create_scenarios (GST_VALIDATE_BIN_MONITOR
      (monitor));


  monitor->print_pos_srcid =
      g_timeout_add (PRINT_POSITION_TIMEOUT, (GSourceFunc) print_position,
      monitor);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message", (GCallback) _bus_handler, monitor);

  gst_object_unref (bus);

  return monitor;
}
