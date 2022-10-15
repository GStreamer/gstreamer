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
_validate_bin_element_removed (GstBin * bin, GstElement * element,
    GstValidateBinMonitor * monitor);

static void
gst_validate_bin_set_media_descriptor (GstValidateMonitor * monitor,
    GstValidateMediaDescriptor * media_descriptor)
{
  GList *tmp;

  GST_VALIDATE_MONITOR_LOCK (monitor);
  for (tmp = GST_VALIDATE_BIN_MONITOR_CAST (monitor)->element_monitors; tmp;
      tmp = tmp->next) {
    GstValidateMonitor *sub_monitor = (GstValidateMonitor *) tmp->data;
    gst_validate_monitor_set_media_descriptor (sub_monitor, media_descriptor);
  }
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
purge_and_unref_reporter (gpointer data)
{
  GstValidateReporter *reporter = data;

  gst_validate_reporter_purge_reports (reporter);
  g_object_unref (reporter);
}

static void
gst_validate_bin_monitor_dispose (GObject * object)
{
  GstValidateBinMonitor *monitor = GST_VALIDATE_BIN_MONITOR_CAST (object);
  GstElement *bin =
      GST_ELEMENT (gst_validate_monitor_get_target (GST_VALIDATE_MONITOR_CAST
          (monitor)));

  if (bin) {
    if (monitor->element_added_id)
      g_signal_handler_disconnect (bin, monitor->element_added_id);
    if (monitor->element_removed_id)
      g_signal_handler_disconnect (bin, monitor->element_removed_id);
    gst_object_unref (bin);
  }

  if (monitor->scenario) {
    gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER
        (monitor->scenario));
    gst_clear_object (&monitor->scenario);
  }

  g_list_free_full (monitor->element_monitors, purge_and_unref_reporter);

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

/**
 * gst_validate_bin_monitor_new:
 * @bin: (transfer none): a #GstBin to run Validate on
 * @runner: (transfer none): a #GstValidateRunner
 * @parent: (nullable): The parent of the new monitor
 *
 * Returns: (transfer full): A #GstValidateBinMonitor
 */
GstValidateBinMonitor *
gst_validate_bin_monitor_new (GstBin * bin, GstValidateRunner * runner,
    GstValidateMonitor * parent)
{
  g_return_val_if_fail (GST_IS_BIN (bin), NULL);
  g_return_val_if_fail (runner != NULL, NULL);

  return
      g_object_new (GST_TYPE_VALIDATE_BIN_MONITOR, "object",
      bin, "validate-runner", runner, "validate-parent", parent, NULL);
}

static void
gst_validate_bin_child_added_overrides (GstValidateMonitor * monitor,
    GstElement * element)
{
  GList *iter;

  GST_VALIDATE_MONITOR_OVERRIDES_LOCK (monitor);
  for (iter = GST_VALIDATE_MONITOR_OVERRIDES (monitor).head; iter;
      iter = g_list_next (iter)) {
    GstValidateOverride *override = iter->data;

    gst_validate_override_element_added_handler (override, monitor, element);
  }
  GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK (monitor);
}

static gboolean
gst_validate_bin_monitor_setup (GstValidateMonitor * monitor)
{
  GstIterator *iterator;
  gboolean done;
  GstElement *element;
  GstValidateBinMonitor *bin_monitor = GST_VALIDATE_BIN_MONITOR_CAST (monitor);
  GstBin *bin = GST_BIN_CAST (gst_validate_monitor_get_target (monitor));

  if (!GST_IS_BIN (bin)) {
    GST_WARNING_OBJECT (monitor, "Trying to create bin monitor with other "
        "type of object");
    goto fail;
  }

  GST_DEBUG_OBJECT (bin_monitor, "Setting up monitor for bin %" GST_PTR_FORMAT,
      bin);

  if (g_object_get_data ((GObject *) bin, "validate-monitor")) {
    GST_DEBUG_OBJECT (bin_monitor,
        "Bin already has a validate-monitor associated");
    goto fail;
  }

  bin_monitor->element_added_id =
      g_signal_connect (bin, "element-added",
      G_CALLBACK (_validate_bin_element_added), monitor);

  bin_monitor->element_removed_id =
      g_signal_connect (bin, "element-removed",
      G_CALLBACK (_validate_bin_element_removed), monitor);

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
  gst_object_unref (bin);

  return GST_VALIDATE_MONITOR_CLASS (parent_class)->setup (monitor);

fail:
  if (bin)
    gst_object_unref (bin);
  return FALSE;
}

static void
gst_validate_bin_monitor_wrap_element (GstValidateBinMonitor * monitor,
    GstElement * element)
{
  GstValidateElementMonitor *element_monitor;
  GstValidateRunner *runner =
      gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (monitor));

  GST_DEBUG_OBJECT (monitor, "Wrapping element %s", GST_ELEMENT_NAME (element));

  element_monitor =
      GST_VALIDATE_ELEMENT_MONITOR_CAST (gst_validate_monitor_factory_create
      (GST_OBJECT_CAST (element), runner, GST_VALIDATE_MONITOR_CAST (monitor)));
  g_return_if_fail (element_monitor != NULL);

  GST_VALIDATE_MONITOR_CAST (element_monitor)->verbosity =
      GST_VALIDATE_MONITOR_CAST (monitor)->verbosity;
  gst_validate_bin_child_added_overrides (GST_VALIDATE_MONITOR (monitor),
      element);

  if (GST_VALIDATE_MONITOR_CAST (monitor)->verbosity &
      GST_VALIDATE_VERBOSITY_NEW_ELEMENTS)
    gst_validate_printf (NULL, "(element-added) %s added to %s\n",
        GST_ELEMENT_NAME (element),
        gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)));

  GST_VALIDATE_MONITOR_LOCK (monitor);
  monitor->element_monitors = g_list_prepend (monitor->element_monitors,
      element_monitor);
  GST_VALIDATE_MONITOR_UNLOCK (monitor);

  gst_object_unref (runner);
}

static void
_validate_bin_element_added (GstBin * bin, GstElement * element,
    GstValidateBinMonitor * monitor)
{
  GstObject *target =
      gst_validate_monitor_get_target (GST_VALIDATE_MONITOR (monitor));

  g_return_if_fail (GST_ELEMENT_CAST (target) == GST_ELEMENT_CAST (bin));

  gst_object_unref (target);
  gst_validate_bin_monitor_wrap_element (monitor, element);
}

static void
_validate_bin_element_removed (GstBin * bin, GstElement * element,
    GstValidateBinMonitor * monitor)
{
  if (GST_VALIDATE_MONITOR_CAST (monitor)->verbosity &
      GST_VALIDATE_VERBOSITY_NEW_ELEMENTS)
    gst_validate_printf (NULL, "(element-removed) %s removed from %s\n",
        GST_ELEMENT_NAME (element),
        gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)));
}

/**
 * gst_validate_bin_monitor_get_scenario:
 * @monitor: A #GstValidateBinMonitor
 *
 * Returns: (transfer full) (nullable): The #GstValidateScenario being executed
 * under @monitor watch
 *
 * Since: 1.20
 */
GstValidateScenario *
gst_validate_bin_monitor_get_scenario (GstValidateBinMonitor * monitor)
{
  if (monitor->scenario)
    return gst_object_ref (monitor->scenario);

  return NULL;
}
