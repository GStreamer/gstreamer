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
#include "gst-validate-pad-monitor.h"
#include "gst-validate-monitor-factory.h"

#define PRINT_POSITION_TIMEOUT 250

/**
 * SECTION:gst-validate-pipeline-monitor
 * @short_description: Class that wraps a #GstPipeline for Validate checks
 *
 * TODO
 */

typedef struct
{
  gint caps_struct_num;
  gint filter_caps_struct_num;
  GString *str;
  GstStructure *filter;
  gboolean found;
} StructureIncompatibleFieldsInfo;

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
  GstValidatePipelineMonitor *self = (GstValidatePipelineMonitor *) object;

  g_clear_object (&self->stream_collection);
  if (self->streams_selected) {
    g_list_free_full (self->streams_selected, gst_object_unref);
    self->streams_selected = NULL;
  }

  G_OBJECT_CLASS (gst_validate_pipeline_monitor_parent_class)->dispose (object);
}

static void
gst_validate_pipeline_monitor_class_init (GstValidatePipelineMonitorClass *
    klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_validate_pipeline_monitor_dispose;
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
  JsonBuilder *jbuilder;
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

  jbuilder = json_builder_new ();
  json_builder_begin_object (jbuilder);
  json_builder_set_member_name (jbuilder, "type");
  json_builder_add_string_value (jbuilder, "position");
  json_builder_set_member_name (jbuilder, "position");
  json_builder_add_int_value (jbuilder, position);
  json_builder_set_member_name (jbuilder, "duration");
  json_builder_add_int_value (jbuilder, duration);
  json_builder_set_member_name (jbuilder, "speed");
  json_builder_add_double_value (jbuilder, rate);
  json_builder_end_object (jbuilder);

  gst_validate_send (json_builder_get_root (jbuilder));
  g_object_unref (jbuilder);

  gst_validate_printf (NULL,
      "<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " speed: %f />\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
      rate);

  return TRUE;
}

static void
_check_pad_query_failures (GstPad * pad, GString * str,
    GstValidatePadMonitor ** last_query_caps_fail_monitor,
    GstValidatePadMonitor ** last_refused_caps_monitor)
{
  GstValidatePadMonitor *monitor;
  GstPad *ghost_target = NULL;

  if (GST_IS_GHOST_PAD (pad)) {
    ghost_target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));

    if (!ghost_target)
      return;

    pad = ghost_target;
  }

  monitor = g_object_get_data (G_OBJECT (pad), "validate-monitor");

  if (monitor->last_query_res && gst_caps_is_empty (monitor->last_query_res)) {
    gst_object_replace ((GstObject **) last_query_caps_fail_monitor,
        (GstObject *) monitor);
  }

  if (monitor->last_refused_caps)
    gst_object_replace ((GstObject **) last_refused_caps_monitor,
        (GstObject *) monitor);

  if (ghost_target)
    gst_object_unref (ghost_target);
}

static void
_gather_pad_negotiation_details (GstPad * pad, GString * str,
    GstValidatePadMonitor ** last_query_caps_fail_monitor,
    GstValidatePadMonitor ** last_refused_caps_monitor)
{
  GList *tmp;
  GstElement *next;
  GstPad *peer = gst_pad_get_peer (pad);

  _check_pad_query_failures (pad, str, last_query_caps_fail_monitor,
      last_refused_caps_monitor);

  if (!peer)
    return;

  _check_pad_query_failures (peer, str, last_query_caps_fail_monitor,
      last_refused_caps_monitor);

  next = GST_ELEMENT (gst_pad_get_parent (peer));
  GST_OBJECT_LOCK (next);
  for (tmp = next->srcpads; tmp; tmp = tmp->next)
    _gather_pad_negotiation_details (tmp->data, str,
        last_query_caps_fail_monitor, last_refused_caps_monitor);
  GST_OBJECT_UNLOCK (next);

  gst_object_unref (peer);
  gst_object_unref (next);
}

static void
_incompatible_fields_info_set_found (StructureIncompatibleFieldsInfo * info)
{
  if (info->found == FALSE) {
    g_string_append_printf (info->str, " for the following possible reasons:");
    info->found = TRUE;
  }
}

static gboolean
_find_structure_incompatible_fields (GQuark field_id, const GValue * value,
    StructureIncompatibleFieldsInfo * info)
{
  gchar *value_str, *filter_str;
  GValue intersect = { 0, };
  const GValue *filter_value = gst_structure_id_get_value (info->filter,
      field_id);

  if (!filter_value)
    return TRUE;

  value_str = gst_value_serialize (value);
  filter_str = gst_value_serialize (filter_value);

  if (!gst_value_can_intersect (value, filter_value)) {
    _incompatible_fields_info_set_found (info);
    g_string_append_printf (info->str,
        "\n    -> Field '%s' downstream value from structure %d '(%s)%s' can't intersect with"
        " filter value from structure number %d '(%s)%s' because of their types.",
        g_quark_to_string (field_id), info->caps_struct_num,
        G_VALUE_TYPE_NAME (value), value_str, info->filter_caps_struct_num,
        G_VALUE_TYPE_NAME (filter_value), filter_str);

    return TRUE;
  }

  if (gst_value_intersect (&intersect, value, filter_value)) {
    g_value_reset (&intersect);

    return TRUE;
  }

  _incompatible_fields_info_set_found (info);
  g_string_append_printf (info->str,
      "\n    -> Field '%s' downstream value from structure %d '(%s)%s' can't intersect with"
      " filter value from structure number %d '(%s)%s'",
      g_quark_to_string (field_id), info->caps_struct_num,
      G_VALUE_TYPE_NAME (value), value_str, info->filter_caps_struct_num,
      G_VALUE_TYPE_NAME (filter_value), filter_str);

  g_free (value_str);
  g_free (filter_str);

  return TRUE;
}

static void
_append_query_caps_failure_details (GstValidatePadMonitor * monitor,
    GString * str)
{
  gint i, j;
  gboolean found = FALSE;
  GstCaps *filter = gst_caps_copy (monitor->last_query_filter);
  GstCaps *possible_caps = gst_pad_query_caps (monitor->pad, NULL);
  const gchar *filter_name, *possible_name;
  GstStructure *filter_struct, *possible_struct;

  g_string_append_printf (str,
      "\n Caps negotiation failed starting from pad '%s'"
      " as the QUERY_CAPS returned EMPTY caps",
      gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)));

  for (i = 0; i < gst_caps_get_size (possible_caps); i++) {
    possible_struct = gst_caps_get_structure (possible_caps, i);
    possible_name = gst_structure_get_name (possible_struct);

    for (j = 0; j < gst_caps_get_size (filter); j++) {
      StructureIncompatibleFieldsInfo info = {
        .caps_struct_num = i,
        .filter_caps_struct_num = j,
        .str = str,
        .found = found
      };

      info.filter = filter_struct = gst_caps_get_structure (filter, j);
      filter_name = gst_structure_get_name (filter_struct);

      if (g_strcmp0 (possible_name, filter_name)) {
        _incompatible_fields_info_set_found (&info);
        g_string_append_printf (str,
            "\n    -> Downstream caps struct %d name '%s' differs from "
            "filter caps struct %d name '%s'",
            i, possible_name, j, filter_name);

        continue;
      }

      gst_structure_foreach (possible_struct,
          (GstStructureForeachFunc) _find_structure_incompatible_fields, &info);

      if (info.found)
        found = TRUE;
    }
  }

  if (!found) {
    gchar *filter_caps_str = gst_caps_to_string (filter);
    gchar *possible_caps_str = gst_caps_to_string (possible_caps);

    g_string_append_printf (str,
        ". The exact reason could not be determined but"
        " here are the gathered information:\n"
        " - %s last query caps filter: %s\n"
        " - %s possible caps (as returned by a query on it without filter): %s\n",
        gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)),
        filter_caps_str,
        gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)),
        possible_caps_str);
  }

  gst_caps_unref (possible_caps);
  gst_caps_unref (filter);

}

static gboolean
_append_accept_caps_failure_details (GstValidatePadMonitor * monitor,
    GString * str)
{
  gint i, j;
  GstCaps *refused_caps = gst_caps_copy (monitor->last_refused_caps);
  GstCaps *possible_caps = gst_pad_query_caps (monitor->pad, NULL);
  gchar *caps_str = gst_caps_to_string (monitor->last_refused_caps);
  StructureIncompatibleFieldsInfo info = {
    .str = str,
    .found = FALSE
  };

  g_string_append_printf (str,
      "\n Caps negotiation failed at pad '%s' as it refused caps: %s",
      gst_validate_reporter_get_name (GST_VALIDATE_REPORTER (monitor)),
      caps_str);
  g_free (caps_str);

  for (i = 0; i < gst_caps_get_size (refused_caps); i++) {
    GstStructure *refused_struct = gst_caps_get_structure (refused_caps, i);
    const gchar *filter_name;
    const gchar *refused_name = gst_structure_get_name (refused_struct);

    for (j = 0; j < gst_caps_get_size (possible_caps); j++) {
      info.caps_struct_num = i,
          info.filter_caps_struct_num = j,
          info.filter = gst_caps_get_structure (possible_caps, j);

      filter_name = gst_structure_get_name (info.filter);
      if (g_strcmp0 (refused_name, filter_name)) {
        g_string_append_printf (str,
            "\n    -> Downstream caps struct %d name '%s' differs from "
            "filter caps struct %d name '%s'", i, refused_name, j, filter_name);

        continue;
      }

      gst_structure_foreach (refused_struct,
          (GstStructureForeachFunc) _find_structure_incompatible_fields, &info);
    }
  }

  gst_caps_unref (possible_caps);

  return TRUE;
}

static gchar *
_generate_not_negotiated_error_report (GstMessage * msg)
{
  GString *str;
  GList *tmp;
  GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (msg));
  GstValidatePadMonitor *last_query_caps_fail_monitor = NULL,
      *last_refused_caps_monitor = NULL;

  str = g_string_new (NULL);
  g_string_append_printf (str, "Error message posted by: %s",
      GST_OBJECT_NAME (element));

  GST_OBJECT_LOCK (element);
  for (tmp = element->srcpads; tmp; tmp = tmp->next)
    _gather_pad_negotiation_details (tmp->data, str,
        &last_query_caps_fail_monitor, &last_refused_caps_monitor);
  GST_OBJECT_UNLOCK (element);

  if (last_query_caps_fail_monitor)
    _append_query_caps_failure_details (last_query_caps_fail_monitor, str);
  else if (last_refused_caps_monitor)
    _append_accept_caps_failure_details (last_refused_caps_monitor, str);
  else {
    GST_ERROR ("We should always be able to generate detailed report"
        " about why negotiation failed, please report a bug against"
        " gst-devtools:validate with this message and a way to reproduce.");
  }

  gst_object_replace ((GstObject **) & last_query_caps_fail_monitor, NULL);
  gst_object_replace ((GstObject **) & last_refused_caps_monitor, NULL);

  return g_string_free (str, FALSE);
}

static void
_bus_handler (GstBus * bus, GstMessage * message,
    GstValidatePipelineMonitor * monitor)
{
  GError *err = NULL;
  gchar *debug = NULL;
  const GstStructure *details = NULL;
  GstFlowReturn error_flow = GST_FLOW_OK;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (message, &err, &debug);
      gst_message_parse_error_details (message, &details);

      if (g_error_matches (err, GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN)) {
        GST_VALIDATE_REPORT (monitor, MISSING_PLUGIN,
            "Error: %s -- Debug message: %s", err->message, debug);
      } else if ((g_error_matches (err, GST_STREAM_ERROR,
                  GST_STREAM_ERROR_FAILED) && details
              && gst_structure_get_int (details, "flow-return", &error_flow)
              && error_flow == GST_FLOW_NOT_NEGOTIATED)
          || g_error_matches (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FORMAT)) {
        gchar *report = _generate_not_negotiated_error_report (message);

        GST_VALIDATE_REPORT (monitor, NOT_NEGOTIATED, "%s", report);
        g_free (report);
      } else {
        GST_VALIDATE_REPORT (monitor, ERROR_ON_BUS,
            "Got error: %s -- Debug message: %s", err->message, debug);
      }

      GST_VALIDATE_MONITOR_LOCK (monitor);
      monitor->got_error = TRUE;
      GST_VALIDATE_MONITOR_UNLOCK (monitor);
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
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_VALIDATE_MONITOR (monitor)->target) {
        GstState oldstate, newstate, pending;

        gst_message_parse_state_changed (message, &oldstate, &newstate,
            &pending);

        if (oldstate == GST_STATE_READY && newstate == GST_STATE_PAUSED) {
          monitor->print_pos_srcid =
              g_timeout_add (PRINT_POSITION_TIMEOUT,
              (GSourceFunc) print_position, monitor);
        } else if (oldstate >= GST_STATE_PAUSED && newstate <= GST_STATE_READY) {
          if (monitor->print_pos_srcid
              && g_source_remove (monitor->print_pos_srcid))
            monitor->print_pos_srcid = 0;
          monitor->got_error = FALSE;
        }
      }

      break;
    }
    case GST_MESSAGE_BUFFERING:
    {
      JsonBuilder *jbuilder = json_builder_new ();
      GstBufferingMode mode;
      gint percent;

      gst_message_parse_buffering (message, &percent);
      gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);

      json_builder_begin_object (jbuilder);
      json_builder_set_member_name (jbuilder, "type");
      json_builder_add_string_value (jbuilder, "buffering");
      json_builder_set_member_name (jbuilder, "state");
      if (percent == 100) {
        /* a 100% message means buffering is done */
        gst_validate_printf (NULL, "\nDone buffering\n");
        json_builder_add_string_value (jbuilder, "done");
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
          gst_validate_printf (NULL, "\nStart buffering\n");
          json_builder_add_string_value (jbuilder, "started");
          if (monitor->print_pos_srcid
              && g_source_remove (monitor->print_pos_srcid)) {
            monitor->print_pos_srcid = 0;
          }
        } else {
          json_builder_add_string_value (jbuilder, "progress");
        }
        gst_validate_printf (NULL, "%s %d%%  \r", "Buffering...", percent);
      }
      json_builder_set_member_name (jbuilder, "position");
      json_builder_add_int_value (jbuilder, percent);
      json_builder_end_object (jbuilder);

      gst_validate_send (json_builder_get_root (jbuilder));
      g_object_unref (jbuilder);
      break;
    }
    case GST_MESSAGE_STREAM_COLLECTION:
    {
      GstStreamCollection *collection = NULL;
      gst_message_parse_stream_collection (message, &collection);
      gst_object_replace ((GstObject **) & monitor->stream_collection,
          (GstObject *) collection);
      gst_object_unref (collection);
      break;
    }
    case GST_MESSAGE_STREAMS_SELECTED:
    {
      guint i;

      if (monitor->streams_selected) {
        g_list_free_full (monitor->streams_selected, gst_object_unref);
        monitor->streams_selected = NULL;
      }

      for (i = 0; i < gst_message_streams_selected_get_size (message); i++) {
        GstStream *stream =
            gst_message_streams_selected_get_stream (message, i);

        monitor->streams_selected =
            g_list_append (monitor->streams_selected, stream);
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
  const gchar *scenarios_names;
  gchar **scenarios;

  if ((scenarios_names = g_getenv ("GST_VALIDATE_SCENARIO"))) {
    gint i;

    scenarios = g_strsplit (scenarios_names, G_SEARCHPATH_SEPARATOR_S, 0);
    for (i = 0; scenarios[i]; i++) {
      gchar **scenario_v = g_strsplit (scenarios[i], "->", 2);

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

    g_strfreev (scenarios);
  }
}

/**
 * gst_validate_pipeline_monitor_new:
 * @pipeline: (transfer none): a #GstPipeline to run Validate on
 */
GstValidatePipelineMonitor *
gst_validate_pipeline_monitor_new (GstPipeline * pipeline,
    GstValidateRunner * runner, GstValidateMonitor * parent)
{
  GstBus *bus;
  GstValidatePipelineMonitor *monitor =
      g_object_new (GST_TYPE_VALIDATE_PIPELINE_MONITOR, "object",
      pipeline, "validate-runner", runner, "validate-parent", parent,
      "pipeline", pipeline, NULL);

  if (GST_VALIDATE_MONITOR_GET_OBJECT (monitor) == NULL) {
    g_object_unref (monitor);
    return NULL;
  }

  gst_validate_pipeline_monitor_create_scenarios (GST_VALIDATE_BIN_MONITOR
      (monitor));

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message", (GCallback) _bus_handler, monitor);

  gst_object_unref (bus);

  if (g_strcmp0 (G_OBJECT_TYPE_NAME (pipeline), "GstPlayBin") == 0)
    monitor->is_playbin = TRUE;
  else if (g_strcmp0 (G_OBJECT_TYPE_NAME (pipeline), "GstPlayBin3") == 0)
    monitor->is_playbin3 = TRUE;

  return monitor;
}
