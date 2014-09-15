/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-bin-monitor.c - Validate BinMonitor class
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
#include "gst-validate-bin-monitor.h"
#include "gst-validate-monitor-factory.h"

#define PRINT_POSITION_TIMEOUT 250

/**
 * SECTION:gst-validate-bin-monitor
 * @short_description: Class that wraps a #GstBin for Validate checks
 *
 * TODO
 */

enum
{
  PROP_0,
  PROP_HANDLES_STATE,
  PROP_LAST
};

#define gst_validate_bin_monitor_parent_class parent_class
G_DEFINE_TYPE (GstValidateBinMonitor, gst_validate_bin_monitor,
    GST_TYPE_VALIDATE_ELEMENT_MONITOR);

static void
gst_validate_bin_monitor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void
gst_validate_bin_monitor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void
gst_validate_bin_monitor_wrap_element (GstValidateBinMonitor * monitor,
    GstElement * element);
static gboolean gst_validate_bin_monitor_setup (GstValidateMonitor * monitor);

static void
_validate_bin_element_added (GstBin * bin, GstElement * pad,
    GstValidateBinMonitor * monitor);

static void
gst_validate_bin_set_media_descriptor (GstValidateMonitor * monitor,
    GstMediaDescriptor * media_descriptor)
{
  GList *tmp;

  GST_VALIDATE_MONITOR_LOCK (monitor);
  for (tmp = GST_VALIDATE_BIN_MONITOR_CAST (monitor)->element_monitors; tmp;
      tmp = tmp->next)
    gst_validate_monitor_set_media_descriptor (tmp->data, media_descriptor);
  GST_VALIDATE_MONITOR_UNLOCK (monitor);

  GST_VALIDATE_MONITOR_CLASS (parent_class)->set_media_descriptor (monitor,
      media_descriptor);
}

static void
gst_validate_bin_monitor_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_HANDLES_STATE:
      g_assert_not_reached ();
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_validate_bin_monitor_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstValidateBinMonitor *monitor;

  monitor = GST_VALIDATE_BIN_MONITOR_CAST (object);

  switch (prop_id) {
    case PROP_HANDLES_STATE:
      if (monitor->scenario == NULL)
        g_value_set_boolean (value, FALSE);
      else
        g_object_get_property (G_OBJECT (monitor->scenario), "handles-states",
            value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_validate_bin_monitor_dispose (GObject * object)
{
  GstValidateBinMonitor *monitor = GST_VALIDATE_BIN_MONITOR_CAST (object);
  GstElement *bin = GST_VALIDATE_ELEMENT_MONITOR_GET_ELEMENT (monitor);

  if (bin && monitor->element_added_id)
    g_signal_handler_disconnect (bin, monitor->element_added_id);

  if (monitor->scenario)
    g_object_unref (monitor->scenario);

  if (monitor->print_pos_srcid) {
    if (g_source_remove (monitor->print_pos_srcid))
      monitor->print_pos_srcid = 0;
  }

  g_list_free_full (monitor->element_monitors, g_object_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_validate_bin_monitor_class_init (GstValidateBinMonitorClass * klass)
{
  GObjectClass *gobject_class;
  GstValidateMonitorClass *validatemonitor_class;

  gobject_class = G_OBJECT_CLASS (klass);
  validatemonitor_class = GST_VALIDATE_MONITOR_CLASS_CAST (klass);

  gobject_class->get_property = gst_validate_bin_monitor_get_property;
  gobject_class->set_property = gst_validate_bin_monitor_set_property;
  gobject_class->dispose = gst_validate_bin_monitor_dispose;

  g_object_class_install_property (gobject_class, PROP_HANDLES_STATE,
      g_param_spec_boolean ("handles-states", "Handles state",
          "True if the application should not set handle the first state change "
          " False if it is application responsibility",
          FALSE, G_PARAM_READABLE));

  validatemonitor_class->setup = gst_validate_bin_monitor_setup;
  validatemonitor_class->set_media_descriptor =
      gst_validate_bin_set_media_descriptor;
}

static void
gst_validate_bin_monitor_init (GstValidateBinMonitor * bin_monitor)
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
  if (gst_element_query (pipeline, query)) {
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);

    gst_validate_printf (NULL,
        "<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
        " speed: %f />\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
        rate);
  }
  gst_query_unref (query);

  return TRUE;
}

static void
_bus_handler (GstBus * bus, GstMessage * message,
    GstValidateBinMonitor * monitor)
{
  GError *err;
  gchar *debug;

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
gst_validate_bin_monitor_create_scenarios (GstValidateBinMonitor * monitor)
{
  GstElement *bin = GST_ELEMENT (GST_VALIDATE_MONITOR_GET_OBJECT (monitor));

  /* scenarios currently only make sense for pipelines */
  if (GST_IS_PIPELINE (bin)) {
    const gchar *scenario_name;

    if ((scenario_name = g_getenv ("GST_VALIDATE_SCENARIO"))) {
      gchar **scenario_v = g_strsplit (scenario_name, "->", 2);

      if (scenario_v[1] && GST_VALIDATE_MONITOR_GET_OBJECT (monitor)) {
        if (!g_pattern_match_simple (scenario_v[1],
                GST_OBJECT_NAME (GST_VALIDATE_MONITOR_GET_OBJECT (monitor)))) {
          GST_INFO_OBJECT (monitor, "Not attaching to bin %" GST_PTR_FORMAT
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
}

/**
 * gst_validate_bin_monitor_new:
 * @bin: (transfer-none): a #GstBin to run Validate on
 */
GstValidateBinMonitor *
gst_validate_bin_monitor_new (GstBin * bin, GstValidateRunner * runner,
    GstValidateMonitor * parent)
{
  GstValidateBinMonitor *monitor =
      g_object_new (GST_TYPE_VALIDATE_BIN_MONITOR, "object",
      bin, "validate-runner", runner, "validate-parent", parent, NULL);

  if (GST_VALIDATE_MONITOR_GET_OBJECT (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }

  gst_validate_bin_monitor_create_scenarios (monitor);

  if (GST_IS_PIPELINE (bin)) {
    GstBus *bus;

    monitor->print_pos_srcid =
        g_timeout_add (PRINT_POSITION_TIMEOUT, (GSourceFunc) print_position,
        monitor);

    bus = gst_element_get_bus (GST_ELEMENT (bin));
    gst_bus_enable_sync_message_emission (bus);
    g_signal_connect (bus, "sync-message", (GCallback) _bus_handler, monitor);

    gst_object_unref (bus);
  }

  return monitor;
}

static gboolean
gst_validate_bin_monitor_setup (GstValidateMonitor * monitor)
{
  GstIterator *iterator;
  gboolean done;
  GstElement *element;
  GstValidateBinMonitor *bin_monitor = GST_VALIDATE_BIN_MONITOR_CAST (monitor);
  GstBin *bin = GST_VALIDATE_BIN_MONITOR_GET_BIN (bin_monitor);

  if (!GST_IS_BIN (bin)) {
    GST_WARNING_OBJECT (monitor, "Trying to create bin monitor with other "
        "type of object");
    return FALSE;
  }

  GST_DEBUG_OBJECT (bin_monitor, "Setting up monitor for bin %" GST_PTR_FORMAT,
      bin);

  bin_monitor->element_added_id =
      g_signal_connect (bin, "element-added",
      G_CALLBACK (_validate_bin_element_added), monitor);

  iterator = gst_bin_iterate_elements (bin);
  done = FALSE;
  while (!done) {
    GValue value = { 0, };

    switch (gst_iterator_next (iterator, &value)) {
      case GST_ITERATOR_OK:
        element = g_value_get_object (&value);
        gst_validate_bin_monitor_wrap_element (bin_monitor, element);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        /* TODO how to handle this? */
        gst_iterator_resync (iterator);
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iterator);

  return TRUE;
}

static void
gst_validate_bin_monitor_wrap_element (GstValidateBinMonitor * monitor,
    GstElement * element)
{
  GstValidateElementMonitor *element_monitor;
  GST_DEBUG_OBJECT (monitor, "Wrapping element %s", GST_ELEMENT_NAME (element));

  element_monitor =
      GST_VALIDATE_ELEMENT_MONITOR_CAST (gst_validate_monitor_factory_create
      (GST_OBJECT_CAST (element), GST_VALIDATE_MONITOR_GET_RUNNER (monitor),
          GST_VALIDATE_MONITOR_CAST (monitor)));
  g_return_if_fail (element_monitor != NULL);

  GST_VALIDATE_MONITOR_LOCK (monitor);
  monitor->element_monitors = g_list_prepend (monitor->element_monitors,
      element_monitor);
  GST_VALIDATE_MONITOR_UNLOCK (monitor);
}

static void
_validate_bin_element_added (GstBin * bin, GstElement * element,
    GstValidateBinMonitor * monitor)
{
  g_return_if_fail (GST_VALIDATE_ELEMENT_MONITOR_GET_ELEMENT (monitor) ==
      GST_ELEMENT_CAST (bin));
  gst_validate_bin_monitor_wrap_element (monitor, element);
}
