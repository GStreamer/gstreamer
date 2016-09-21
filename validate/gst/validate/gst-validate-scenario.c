/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-scenario.c - Validate Scenario class
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
/**
 * SECTION:gst-validate-scenario
 * @short_description: A GstValidateScenario represents a set of actions to be executed on a pipeline.
 *
 * A #GstValidateScenario represents the scenario that will be executed on a #GstPipeline.
 * It is basically an ordered list of #GstValidateAction that will be executed during the
 * execution of the pipeline.
 *
 * Possible configurations (see #GST_VALIDATE_CONFIG):
 *  * scenario-action-execution-interval: Sets the interval in
 *    milliseconds (1/1000ths of a second), between which actions
 *    will be executed, setting it to 0 means "execute in idle".
 *    The default value is 10ms.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gio/gio.h>
#include <string.h>
#include <errno.h>

#include "gst-validate-internal.h"
#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"
#include "gst-validate-utils.h"
#include "validate.h"
#include <gst/validate/gst-validate-override.h>
#include <gst/validate/gst-validate-override-registry.h>
#include <gst/validate/gst-validate-pipeline-monitor.h>

#define GST_VALIDATE_SCENARIO_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_VALIDATE_SCENARIO, GstValidateScenarioPrivate))

#define GST_VALIDATE_SCENARIO_SUFFIX ".scenario"
#define GST_VALIDATE_SCENARIO_DIRECTORY "scenarios"

#define DEFAULT_SEEK_TOLERANCE (1 * GST_MSECOND)        /* tolerance seek interval
                                                           TODO make it overridable  */

GST_DEBUG_CATEGORY_STATIC (gst_validate_scenario_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_scenario_debug

#define REGISTER_ACTION_TYPE(_tname, _function, _params, _desc, _is_config) G_STMT_START { \
  gst_validate_register_action_type ((_tname), "core", (_function), (_params), (_desc), (_is_config)); \
} G_STMT_END

#define ACTION_EXPECTED_STREAM_QUARK g_quark_from_static_string ("ACTION_EXPECTED_STREAM_QUARK")

#define SCENARIO_LOCK(scenario) (g_mutex_lock(&scenario->priv->lock))
#define SCENARIO_UNLOCK(scenario) (g_mutex_unlock(&scenario->priv->lock))
enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_HANDLES_STATE,
  PROP_EXECUTE_ON_IDLE,
  PROP_LAST
};

enum
{
  DONE,
  LAST_SIGNAL
};

static guint scenario_signals[LAST_SIGNAL] = { 0 };

static GList *action_types = NULL;
static void gst_validate_scenario_dispose (GObject * object);
static void gst_validate_scenario_finalize (GObject * object);
static GstValidateActionType *_find_action_type (const gchar * type_name);

/* GstValidateScenario is not really thread safe and
 * everything should be done from the thread GstValidate
 * was inited from, unless stated otherwise.
 */
struct _GstValidateScenarioPrivate
{
  GstBus *bus;
  GstValidateRunner *runner;
  gboolean execute_on_idle;

  GMutex lock;

  GList *actions;
  GList *interlaced_actions;    /* MT safe. Protected with SCENARIO_LOCK */
  GList *on_addition_actions;   /* MT safe. Protected with SCENARIO_LOCK */

  /*  List of action that need parsing when reaching ASYNC_DONE
   *  most probably to be able to query duration */
  GList *needs_parsing;

  GstEvent *last_seek;
  GstSeekFlags seek_flags;
  GstClockTime segment_start;
  GstClockTime segment_stop;
  GstClockTime seek_pos_tol;

  /* If we seeked in paused the position should be exactly what
   * the seek value was (if accurate) */
  gboolean seeked_in_pause;

  guint num_actions;

  gboolean handles_state;

  guint execute_actions_source_id;      /* MT safe. Protect with SCENARIO_LOCK */
  guint wait_id;
  guint signal_handler_id;
  guint action_execution_interval;

  /* Name of message the wait action is waiting for */
  const gchar *message_type;

  gboolean buffering;

  gboolean got_eos;
  gboolean changing_state;
  gboolean needs_async_done;
  GstState target_state;

  GList *overrides;

  gchar *pipeline_name;

  /* 'switch-track action' currently waiting for
   * GST_MESSAGE_STREAMS_SELECTED to be completed. */
  GstValidateAction *pending_switch_track;
};

typedef struct KeyFileGroupName
{
  GKeyFile *kf;
  gchar *group_name;
} KeyFileGroupName;

static GstValidateInterceptionReturn
gst_validate_scenario_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report)
{
  GList *tmp;

  for (tmp = GST_VALIDATE_SCENARIO (reporter)->priv->overrides; tmp;
      tmp = tmp->next) {
    report->level =
        gst_validate_override_get_severity (tmp->data,
        gst_validate_issue_get_id (report->issue), report->level);
  }

  return GST_VALIDATE_REPORTER_REPORT;
}

static GstPipeline *
_get_pipeline (GstValidateReporter * scenario)
{
  return gst_object_ref (GST_VALIDATE_SCENARIO (scenario)->pipeline);
}

static void
_reporter_iface_init (GstValidateReporterInterface * iface)
{
  iface->intercept_report = gst_validate_scenario_intercept_report;
  iface->get_pipeline = _get_pipeline;
}

G_DEFINE_TYPE_WITH_CODE (GstValidateScenario, gst_validate_scenario,
    GST_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER,
        _reporter_iface_init));

/* GstValidateAction implementation */
GType _gst_validate_action_type = 0;

struct _GstValidateActionPrivate
{
  GstStructure *main_structure;
  GstValidateExecuteActionReturn state; /* Actually ActionState */
  gboolean printed;
  gboolean executing_last_subaction;
  gboolean optional;

  GstClockTime execution_time;
  GstClockTime timeout;

  GWeakRef scenario;
};

static JsonNode *
gst_validate_action_serialize (GstValidateAction * action)
{
  JsonNode *node = json_node_alloc ();
  JsonObject *jreport = json_object_new ();
  gchar *action_args = gst_structure_to_string (action->structure);

  json_object_set_string_member (jreport, "type", "action");
  json_object_set_string_member (jreport, "action-type", action->type);
  json_object_set_int_member (jreport, "playback-time",
      (gint64) action->playback_time);
  json_object_set_string_member (jreport, "args", action_args);
  g_free (action_args);

  node = json_node_init_object (node, jreport);

  return node;
}

GType
gst_validate_action_get_type (void)
{
  if (_gst_validate_action_type == 0) {
    _gst_validate_action_type =
        g_boxed_type_register_static (g_intern_static_string
        ("GstValidateAction"), (GBoxedCopyFunc) gst_mini_object_ref,
        (GBoxedFreeFunc) gst_mini_object_unref);

    json_boxed_register_serialize_func (_gst_validate_action_type,
        JSON_NODE_OBJECT,
        (JsonBoxedSerializeFunc) gst_validate_action_serialize);
  }

  return _gst_validate_action_type;
}

static GstValidateAction *gst_validate_action_new (GstValidateScenario *
    scenario, GstValidateActionType * type);
static gboolean execute_next_action (GstValidateScenario * scenario);

static GstValidateAction *
_action_copy (GstValidateAction * act)
{
  GstValidateAction *copy = gst_validate_action_new (act->scenario,
      _find_action_type (act->type));

  if (act->structure) {
    copy->structure = gst_structure_copy (act->structure);
    copy->type = gst_structure_get_name (copy->structure);
    if (!(act->name = gst_structure_get_string (copy->structure, "name")))
      act->name = "";
  }

  if (act->priv->main_structure)
    copy->priv->main_structure = gst_structure_copy (act->priv->main_structure);

  copy->action_number = act->action_number;
  copy->playback_time = act->playback_time;
  copy->priv->timeout = act->priv->timeout;

  return copy;
}

static void
_action_free (GstValidateAction * action)
{
  if (action->structure)
    gst_structure_free (action->structure);

  if (action->priv->main_structure)
    gst_structure_free (action->priv->main_structure);

  if (action->scenario)
    g_object_remove_weak_pointer (G_OBJECT (action->scenario),
        ((gpointer *) & action->scenario));

  g_weak_ref_clear (&action->priv->scenario);

  g_slice_free (GstValidateActionPrivate, action->priv);
  g_slice_free (GstValidateAction, action);
}

static void
gst_validate_action_init (GstValidateAction * action)
{
  gst_mini_object_init (((GstMiniObject *) action), 0,
      _gst_validate_action_type, (GstMiniObjectCopyFunction) _action_copy, NULL,
      (GstMiniObjectFreeFunction) _action_free);

  action->priv = g_slice_new0 (GstValidateActionPrivate);

  g_weak_ref_init (&action->priv->scenario, NULL);
}

static void
gst_validate_action_unref (GstValidateAction * action)
{
  gst_mini_object_unref (GST_MINI_OBJECT (action));
}

static GstValidateAction *
gst_validate_action_new (GstValidateScenario * scenario,
    GstValidateActionType * action_type)
{
  GstValidateAction *action = g_slice_new0 (GstValidateAction);

  gst_validate_action_init (action);
  action->playback_time = GST_CLOCK_TIME_NONE;
  action->priv->timeout = GST_CLOCK_TIME_NONE;
  action->type = action_type->name;
  action->repeat = -1;

  g_weak_ref_set (&action->priv->scenario, scenario);
  action->scenario = scenario;
  if (scenario)
    g_object_add_weak_pointer (G_OBJECT (scenario),
        ((gpointer *) & action->scenario));

  return action;
}

gboolean
_action_check_and_set_printed (GstValidateAction * action)
{
  if (action->priv->printed == FALSE) {
    gst_validate_send (json_boxed_serialize (GST_MINI_OBJECT_TYPE
            (action), action));

    action->priv->printed = TRUE;

    return FALSE;
  }

  return TRUE;
}

gboolean
gst_validate_action_is_subaction (GstValidateAction * action)
{
  return !gst_structure_is_equal (action->structure,
      action->priv->main_structure);
}

/* GstValidateActionType implementation */
GType _gst_validate_action_type_type;
GST_DEFINE_MINI_OBJECT_TYPE (GstValidateActionType, gst_validate_action_type);
static GstValidateActionType *gst_validate_action_type_new (void);

static void
_action_type_free (GstValidateActionType * type)
{
  g_free (type->parameters);
  g_free (type->description);
  g_free (type->name);
  g_free (type->implementer_namespace);

  if (type->overriden_type)
    gst_mini_object_unref (GST_MINI_OBJECT (type->overriden_type));
}

static void
gst_validate_action_type_init (GstValidateActionType * type)
{
  gst_mini_object_init ((GstMiniObject *) type, 0,
      _gst_validate_action_type_type, NULL, NULL,
      (GstMiniObjectFreeFunction) _action_type_free);
}

GstValidateActionType *
gst_validate_action_type_new (void)
{
  GstValidateActionType *type = g_slice_new0 (GstValidateActionType);

  gst_validate_action_type_init (type);

  /* action types are never freed */
  GST_MINI_OBJECT_FLAG_SET (type, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  return type;
}

static GstValidateActionType *
_find_action_type (const gchar * type_name)
{
  GList *tmp;

  for (tmp = action_types; tmp; tmp = tmp->next) {
    if (g_strcmp0 (((GstValidateActionType *) tmp->data)->name, type_name) == 0)
      return tmp->data;
  }

  return NULL;
}

static gboolean
_set_variable_func (const gchar * name, double *value, gpointer user_data)
{
  GstValidateScenario *scenario = GST_VALIDATE_SCENARIO (user_data);

  if (!g_strcmp0 (name, "duration")) {
    gint64 duration;

    if (!gst_element_query_duration (scenario->pipeline,
            GST_FORMAT_TIME, &duration)) {
      GstValidateMonitor *monitor =
          (GstValidateMonitor *) (g_object_get_data ((GObject *)
              scenario->pipeline, "validate-monitor"));
      GST_WARNING_OBJECT (scenario,
          "Could not query duration. Trying to get duration from media-info");
      if (monitor && monitor->media_descriptor)
        duration =
            gst_validate_media_descriptor_get_duration
            (monitor->media_descriptor);
      else {
        GST_ERROR_OBJECT (scenario, "Media-info not set");
        return FALSE;
      }
    }

    if (!GST_CLOCK_TIME_IS_VALID (duration))
      *value = G_MAXDOUBLE;
    else
      *value = ((double) duration / GST_SECOND);

    return TRUE;
  } else if (!g_strcmp0 (name, "position")) {
    gint64 position;

    if (!gst_element_query_position (scenario->pipeline,
            GST_FORMAT_TIME, &position)) {
      GST_WARNING_OBJECT (scenario, "Could not query position");
      return FALSE;
    }

    if (!GST_CLOCK_TIME_IS_VALID (position))
      *value = G_MAXDOUBLE;
    else
      *value = ((double) position / GST_SECOND);


    return TRUE;
  }

  return FALSE;
}

/* Check that @list doesn't contain any non-optional actions */
static gboolean
actions_list_is_done (GList * list)
{
  GList *l;

  for (l = list; l != NULL; l = g_list_next (l)) {
    GstValidateAction *action = l->data;

    if (!action->priv->optional)
      return FALSE;
  }

  return TRUE;
}

static void
_check_scenario_is_done (GstValidateScenario * scenario)
{
  SCENARIO_LOCK (scenario);
  if (actions_list_is_done (scenario->priv->actions) &&
      actions_list_is_done (scenario->priv->interlaced_actions) &&
      actions_list_is_done (scenario->priv->on_addition_actions)) {
    SCENARIO_UNLOCK (scenario);

    g_signal_emit (scenario, scenario_signals[DONE], 0);
  } else {
    SCENARIO_UNLOCK (scenario);
  }
}

/**
 * gst_validate_action_get_clocktime:
 * @scenario: The #GstValidateScenario from which to get a time
 *            for a parameter of an action
 * @action: The action from which to retrieve the time for @name
 *          parameter.
 * @name: The name of the parameter for which to retrieve a time
 * @retval: (out): The return value for the wanted time
 *
 *
 * Get a time value for the @name parameter of an action. This
 * method should be called to retrieved and compute a timed value of a given
 * action. It will first try to retrieve the value as a double,
 * then get it as a string and execute any formula taking into account
 * the 'position' and 'duration' variables. And it will always convert that
 * value to a GstClockTime.
 *
 * Returns: %TRUE if the time value could be retrieved/computed or %FALSE otherwise
 */
gboolean
gst_validate_action_get_clocktime (GstValidateScenario * scenario,
    GstValidateAction * action, const gchar * name, GstClockTime * retval)
{
  if (!gst_validate_utils_get_clocktime (action->structure, name, retval)) {
    gdouble val;
    gchar *error = NULL;
    const gchar *strval;

    if (!(strval = gst_structure_get_string (action->structure, name))) {
      GST_INFO_OBJECT (scenario, "Could not find %s", name);
      return -1;
    }

    val = gst_validate_utils_parse_expression (strval, _set_variable_func,
        scenario, &error);

    if (error) {
      GST_WARNING ("Error while parsing %s: %s", strval, error);
      g_free (error);

      return FALSE;
    } else if (val == -1.0) {
      *retval = GST_CLOCK_TIME_NONE;
    } else {
      *retval = val * GST_SECOND;
      *retval = GST_ROUND_UP_4 (*retval);
    }

    return TRUE;
  }

  return TRUE;
}

/**
 * gst_validate_scenario_execute_seek:
 * @scenario: The #GstValidateScenario for which to execute a seek action
 * @action: The seek action to execute
 * @rate: The playback rate of the seek
 * @format: The #GstFormat of the seek
 * @flags: The #GstSeekFlags of the seek
 * @start_type: The #GstSeekType of the start value of the seek
 * @start: The start time of the seek
 * @stop_type: The #GstSeekType of the stop value of the seek
 * @stop: The stop time of the seek
 *
 * Executes a seek event on the scenario' pipeline. You should always use
 * that method when you want to execute a seek inside a new action types
 * so that the scenario state is updated taking into account that seek.
 *
 * For more information you should have a look at #gst_event_new_seek
 *
 * Returns: %TRUE if the seek could be executed, %FALSE otherwise
 */
gboolean
gst_validate_scenario_execute_seek (GstValidateScenario * scenario,
    GstValidateAction * action, gdouble rate, GstFormat format,
    GstSeekFlags flags, GstSeekType start_type, GstClockTime start,
    GstSeekType stop_type, GstClockTime stop)
{
  GstValidateExecuteActionReturn ret = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  GstValidateScenarioPrivate *priv = scenario->priv;

  GstEvent *seek = gst_event_new_seek (rate, format, flags, start_type, start,
      stop_type, stop);

  gst_event_ref (seek);
  if (gst_element_send_event (scenario->pipeline, seek)) {
    gst_event_replace (&priv->last_seek, seek);
    priv->seek_flags = flags;
  } else {
    GST_VALIDATE_REPORT (scenario, EVENT_SEEK_NOT_HANDLED,
        "Could not execute seek: '(position %" GST_TIME_FORMAT
        "), %s (num %u, missing repeat: %i), seeking to: %" GST_TIME_FORMAT
        " stop: %" GST_TIME_FORMAT " Rate %lf'",
        GST_TIME_ARGS (action->playback_time), action->name,
        action->action_number, action->repeat, GST_TIME_ARGS (start),
        GST_TIME_ARGS (stop), rate);
    ret = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }
  gst_event_unref (seek);

  return ret;
}

static gint
_execute_seek (GstValidateScenario * scenario, GstValidateAction * action)
{
  const char *str_format, *str_flags, *str_start_type, *str_stop_type;

  gdouble rate = 1.0;
  GstFormat format = GST_FORMAT_TIME;
  GstSeekFlags flags = 0;
  GstSeekType start_type = GST_SEEK_TYPE_SET;
  GstClockTime start;
  GstSeekType stop_type = GST_SEEK_TYPE_SET;
  GstClockTime stop = GST_CLOCK_TIME_NONE;

  if (!gst_validate_action_get_clocktime (scenario, action, "start", &start))
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;

  gst_structure_get_double (action->structure, "rate", &rate);
  if ((str_format = gst_structure_get_string (action->structure, "format")))
    gst_validate_utils_enum_from_str (GST_TYPE_FORMAT, str_format, &format);

  if ((str_start_type =
          gst_structure_get_string (action->structure, "start_type")))
    gst_validate_utils_enum_from_str (GST_TYPE_SEEK_TYPE, str_start_type,
        &start_type);

  if ((str_stop_type =
          gst_structure_get_string (action->structure, "stop_type")))
    gst_validate_utils_enum_from_str (GST_TYPE_SEEK_TYPE, str_stop_type,
        &stop_type);

  if ((str_flags = gst_structure_get_string (action->structure, "flags")))
    flags = gst_validate_utils_flags_from_str (GST_TYPE_SEEK_FLAGS, str_flags);

  gst_validate_action_get_clocktime (scenario, action, "stop", &stop);

  return gst_validate_scenario_execute_seek (scenario, action, rate, format,
      flags, start_type, start, stop_type, stop);
}

static gboolean
_pause_action_restore_playing (GstValidateScenario * scenario)
{
  GstElement *pipeline = scenario->pipeline;

  gst_validate_printf (scenario, "Back to playing\n");

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to playing");
    scenario->priv->target_state = GST_STATE_PLAYING;
  }

  return FALSE;
}

static GstValidateExecuteActionReturn
_execute_set_state (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstState state;
  const gchar *str_state;

  GstStateChangeReturn ret;

  g_return_val_if_fail ((str_state =
          gst_structure_get_string (action->structure, "state")), FALSE);

  g_return_val_if_fail (gst_validate_utils_enum_from_str (GST_TYPE_STATE,
          str_state, &state), FALSE);

  scenario->priv->target_state = state;
  scenario->priv->changing_state = TRUE;
  scenario->priv->seeked_in_pause = FALSE;

  ret = gst_element_set_state (scenario->pipeline, state);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    scenario->priv->changing_state = FALSE;
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to %s", str_state);

    /* Nothing async on failure, action will be removed automatically */
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  } else if (ret == GST_STATE_CHANGE_ASYNC) {

    scenario->priv->needs_async_done = TRUE;
    return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  }

  scenario->priv->changing_state = FALSE;

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static gboolean
_execute_pause (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstClockTime duration = 0;
  GstStateChangeReturn ret;

  gst_structure_get (action->structure, "duration", G_TYPE_UINT64, &duration,
      NULL);
  gst_structure_set (action->structure, "state", G_TYPE_STRING, "paused", NULL);

  GST_INFO_OBJECT (scenario, "Pausing for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));

  ret = _execute_set_state (scenario, action);

  if (ret && duration)
    g_timeout_add (GST_TIME_AS_MSECONDS (duration),
        (GSourceFunc) _pause_action_restore_playing, scenario);

  return ret;
}

static gboolean
_execute_play (GstValidateScenario * scenario, GstValidateAction * action)
{
  GST_DEBUG ("Playing back");

  gst_structure_set (action->structure, "state", G_TYPE_STRING,
      "playing", NULL);


  return _execute_set_state (scenario, action);
}

static gboolean
_action_sets_state (GstValidateAction * action)
{
  if (action == NULL)
    return FALSE;

  if (g_strcmp0 (action->type, "set-state") == 0)
    return TRUE;

  if (g_strcmp0 (action->type, "play") == 0)
    return TRUE;

  if (g_strcmp0 (action->type, "pause") == 0)
    return TRUE;

  return FALSE;

}

static gboolean
_execute_stop (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstBus *bus = gst_element_get_bus (scenario->pipeline);

  SCENARIO_LOCK (scenario);
  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }
  SCENARIO_UNLOCK (scenario);

  gst_bus_post (bus,
      gst_message_new_request_state (GST_OBJECT_CAST (scenario),
          GST_STATE_NULL));
  gst_object_unref (bus);

  return TRUE;
}

static gboolean
_execute_eos (GstValidateScenario * scenario, GstValidateAction * action)
{
  GST_DEBUG ("Sending eos to pipeline at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (action->playback_time));

  return gst_element_send_event (scenario->pipeline, gst_event_new_eos ());
}

static int
find_input_selector (GValue * velement, const gchar * type)
{
  GstElement *element = g_value_get_object (velement);
  int result = !0;

  if (G_OBJECT_TYPE (element) == g_type_from_name ("GstInputSelector")) {
    GstPad *srcpad = gst_element_get_static_pad (element, "src");

    if (srcpad) {
      GstCaps *caps = gst_pad_query_caps (srcpad, NULL);

      if (caps) {
        const char *mime =
            gst_structure_get_name (gst_caps_get_structure (caps, 0));
        gboolean found = FALSE;

        if (g_strcmp0 (type, "audio") == 0)
          found = g_str_has_prefix (mime, "audio/");
        else if (g_strcmp0 (type, "video") == 0)
          found = g_str_has_prefix (mime, "video/")
              && !g_str_has_prefix (mime, "video/x-dvd-subpicture");
        else if (g_strcmp0 (type, "text") == 0)
          found = g_str_has_prefix (mime, "text/")
              || g_str_has_prefix (mime, "subtitle/")
              || g_str_has_prefix (mime, "video/x-dvd-subpicture");

        if (found)
          result = 0;
      }

      gst_caps_unref (caps);
      gst_object_unref (srcpad);
    }
  }
  return result;
}

static GstElement *
find_input_selector_with_type (GstBin * bin, const gchar * type)
{
  GValue result = { 0, };
  GstElement *input_selector = NULL;
  GstIterator *iterator = gst_bin_iterate_recurse (bin);

  if (gst_iterator_find_custom (iterator,
          (GCompareFunc) find_input_selector, &result, (gpointer) type)) {
    input_selector = g_value_get_object (&result);
  }
  gst_iterator_free (iterator);

  return input_selector;
}

static GstPad *
find_nth_sink_pad (GstElement * element, int index)
{
  GstIterator *iterator;
  gboolean done = FALSE;
  GstPad *pad = NULL;
  int dec_index = index;
  GValue data = { 0, };

  iterator = gst_element_iterate_sink_pads (element);
  while (!done) {
    switch (gst_iterator_next (iterator, &data)) {
      case GST_ITERATOR_OK:
        if (!dec_index--) {
          done = TRUE;
          pad = g_value_get_object (&data);
          break;
        }
        g_value_reset (&data);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        dec_index = index;
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
  return pad;
}

static int
find_sink_pad_index (GstElement * element, GstPad * pad)
{
  GstIterator *iterator;
  gboolean done = FALSE;
  int index = 0;
  GValue data = { 0, };

  iterator = gst_element_iterate_sink_pads (element);
  while (!done) {
    switch (gst_iterator_next (iterator, &data)) {
      case GST_ITERATOR_OK:
        if (pad == g_value_get_object (&data)) {
          done = TRUE;
        } else {
          index++;
        }
        g_value_reset (&data);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iterator);
        index = 0;
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
  return index;
}

static GstPadProbeReturn
_check_select_pad_done (GstPad * pad, GstPadProbeInfo * info,
    GstValidateAction * action)
{
  if (GST_BUFFER_FLAG_IS_SET (info->data, GST_BUFFER_FLAG_DISCONT)) {
    gst_validate_action_set_done (action);

    return GST_PAD_PROBE_REMOVE;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
execute_switch_track_default (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  guint index;
  gboolean relative = FALSE;
  const gchar *type, *str_index;
  GstElement *input_selector;

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  /* First find an input selector that has the right type */
  input_selector =
      find_input_selector_with_type (GST_BIN (scenario->pipeline), type);
  if (input_selector) {
    GstState state, next;
    GstPad *pad, *cpad, *srcpad;

    GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;

    if ((str_index = gst_structure_get_string (action->structure, "index"))) {
      if (!gst_structure_get_uint (action->structure, "index", &index)) {
        GST_WARNING ("No index given, defaulting to +1");
        index = 1;
        relative = TRUE;
      }
    } else {
      relative = strchr ("+-", str_index[0]) != NULL;
      index = g_ascii_strtoll (str_index, NULL, 10);
    }

    if (relative) {             /* We are changing track relatively to current track */
      int npads;

      g_object_get (input_selector, "active-pad", &pad, "n-pads", &npads, NULL);
      if (pad) {
        int current_index = find_sink_pad_index (input_selector, pad);

        index = (current_index + index) % npads;
        gst_object_unref (pad);
      }
    }

    pad = find_nth_sink_pad (input_selector, index);
    g_object_get (input_selector, "active-pad", &cpad, NULL);
    if (gst_element_get_state (scenario->pipeline, &state, &next, 0) &&
        state == GST_STATE_PLAYING && next == GST_STATE_VOID_PENDING) {
      srcpad = gst_element_get_static_pad (input_selector, "src");

      gst_pad_add_probe (srcpad,
          GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
          (GstPadProbeCallback) _check_select_pad_done, action, NULL);
      res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
      gst_object_unref (srcpad);
    }

    g_object_set (input_selector, "active-pad", pad, NULL);
    gst_object_unref (pad);
    gst_object_unref (cpad);
    gst_object_unref (input_selector);

    return res;
  }

  /* No selector found -> Failed */
  return GST_VALIDATE_EXECUTE_ACTION_ERROR;
}

static GstPadProbeReturn
_check_pad_event_selection_done (GstPad * pad, GstPadProbeInfo * info,
    GstValidateAction * action)
{
  if (GST_EVENT_TYPE (info->data) == GST_EVENT_STREAM_START) {
    gst_validate_action_set_done (action);
    return GST_PAD_PROBE_REMOVE;
  }
  return GST_PAD_PROBE_OK;
}

static gboolean
execute_switch_track_pb (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gint index, n;
  const gchar *type, *str_index;

  gint flags, current, tflag;
  gchar *tmp, *current_txt;

  gint res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gboolean relative = FALSE, disabling = FALSE;

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  tflag =
      gst_validate_utils_flags_from_str (g_type_from_name ("GstPlayFlags"),
      type);
  current_txt = g_strdup_printf ("current-%s", type);

  tmp = g_strdup_printf ("n-%s", type);
  g_object_get (scenario->pipeline, "flags", &flags, tmp, &n,
      current_txt, &current, NULL);

  /* Don't try to use -1 */
  if (current == -1)
    current = 0;

  g_free (tmp);

  if (gst_structure_has_field (action->structure, "disable")) {
    disabling = TRUE;
    flags &= ~tflag;
    index = -1;
  } else if (!(str_index =
          gst_structure_get_string (action->structure, "index"))) {
    if (!gst_structure_get_int (action->structure, "index", &index)) {
      GST_WARNING ("No index given, defaulting to +1");
      index = 1;
      relative = TRUE;
    }
  } else {
    relative = strchr ("+-", str_index[0]) != NULL;
    index = g_ascii_strtoll (str_index, NULL, 10);
  }

  if (relative) {               /* We are changing track relatively to current track */
    index = (current + index) % n;
  }

  if (!disabling) {
    GstState state, next;
    GstPad *oldpad, *newpad;
    tmp = g_strdup_printf ("get-%s-pad", type);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, current,
        &oldpad);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, index, &newpad);

    gst_validate_printf (action, "Switching to track number: %i,"
        " (from %s:%s to %s:%s)\n", index, GST_DEBUG_PAD_NAME (oldpad),
        GST_DEBUG_PAD_NAME (newpad));
    flags |= tflag;
    g_free (tmp);

    if (gst_element_get_state (scenario->pipeline, &state, &next, 0) &&
        state == GST_STATE_PLAYING && next == GST_STATE_VOID_PENDING) {
      GstPad *srcpad = NULL;
      GstElement *combiner = NULL;
      if (newpad == oldpad) {
        srcpad = gst_pad_get_peer (oldpad);
      } else if (newpad) {
        combiner = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (newpad)));
        if (combiner) {
          srcpad = gst_element_get_static_pad (combiner, "src");
          gst_object_unref (combiner);
        }
      }

      if (srcpad) {
        gst_pad_add_probe (srcpad,
            GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
            (GstPadProbeCallback) _check_pad_event_selection_done, action,
            NULL);
        gst_object_unref (srcpad);

        res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
      } else
        res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
    }

    if (oldpad)
      gst_object_unref (oldpad);
    gst_object_unref (newpad);
  } else {
    gst_validate_printf (action, "Disabling track type %s", type);
  }

  g_object_set (scenario->pipeline, "flags", flags, current_txt, index, NULL);
  g_free (current_txt);

  return res;
}

static GstStreamType
stream_type_from_string (const gchar * type)
{
  if (!g_strcmp0 (type, "video"))
    return GST_STREAM_TYPE_VIDEO;
  else if (!g_strcmp0 (type, "text"))
    return GST_STREAM_TYPE_TEXT;

  /* default */
  return GST_STREAM_TYPE_AUDIO;
}

/* Return a list of stream ID all the currently selected streams but the ones
 * of type @type */
static GList *
disable_stream (GstValidatePipelineMonitor * monitor, GstStreamType type)
{
  GList *streams = NULL, *l;

  for (l = monitor->streams_selected; l; l = g_list_next (l)) {
    GstStream *s = l->data;

    if (gst_stream_get_stream_type (s) != type) {
      streams = g_list_append (streams, (gpointer) s->stream_id);
    }
  }

  return streams;
}

static GList *
switch_stream (GstValidatePipelineMonitor * monitor, GstValidateAction * action,
    GstStreamType type, gint index, gboolean relative)
{
  guint nb_streams;
  guint i, n = 0, current = 0;
  GList *result = NULL, *l;
  GstStream *streams[256], *s, *current_stream = NULL;

  /* Keep all streams which are not @type */
  for (l = monitor->streams_selected; l; l = g_list_next (l)) {
    s = l->data;

    if (gst_stream_get_stream_type (s) != type) {
      result = g_list_append (result, (gpointer) s->stream_id);
    } else if (!current_stream) {
      /* Assume the stream we want to switch from is the first one */
      current_stream = s;
    }
  }

  /* Calculate the number of @type streams */
  nb_streams = gst_stream_collection_get_size (monitor->stream_collection);
  for (i = 0; i < nb_streams; i++) {
    s = gst_stream_collection_get_stream (monitor->stream_collection, i);

    if (gst_stream_get_stream_type (s) == type) {
      streams[n] = s;

      if (current_stream
          && !g_strcmp0 (s->stream_id, current_stream->stream_id))
        current = n;

      n++;
    }
  }

  if (relative) {               /* We are changing track relatively to current track */
    index = (current + index) % n;
  }

  /* Add the new stream we want to switch to */
  s = streams[index];

  gst_validate_printf (action, "Switching from stream %s to %s",
      current_stream ? current_stream->stream_id : "", s->stream_id);

  return g_list_append (result, (gpointer) s->stream_id);
}

static gboolean
execute_switch_track_pb3 (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  gint index;
  GstStreamType stype;
  const gchar *type, *str_index;
  GList *new_streams = NULL;
  GstValidatePipelineMonitor *monitor =
      (GstValidatePipelineMonitor *) (g_object_get_data ((GObject *)
          scenario->pipeline, "validate-monitor"));

  if (!monitor->stream_collection) {
    GST_ERROR ("No stream collection message received on the bus");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  if (!monitor->streams_selected) {
    GST_ERROR ("No streams selected message received on the bus");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  type = gst_structure_get_string (action->structure, "type");
  stype = stream_type_from_string (type);

  if (gst_structure_has_field (action->structure, "disable")) {
    gst_validate_printf (action, "Disabling track type %s", type);
    new_streams = disable_stream (monitor, stype);
  } else {
    gboolean relative = FALSE;

    if (!(str_index = gst_structure_get_string (action->structure, "index"))) {
      if (!gst_structure_get_int (action->structure, "index", &index)) {
        GST_WARNING ("No index given, defaulting to +1");
        index = 1;
        relative = TRUE;
      }
    } else {
      relative = strchr ("+-", str_index[0]) != NULL;
      index = g_ascii_strtoll (str_index, NULL, 10);
    }

    new_streams = switch_stream (monitor, action, stype, index, relative);
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (action),
      ACTION_EXPECTED_STREAM_QUARK, g_list_copy (new_streams),
      (GDestroyNotify) g_list_free);

  priv->pending_switch_track = action;

  if (!gst_element_send_event (scenario->pipeline,
          gst_event_new_select_streams (new_streams))) {
    GST_ERROR ("select-streams event not handled");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static gboolean
_execute_switch_track (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidatePipelineMonitor *monitor =
      (GstValidatePipelineMonitor *) (g_object_get_data ((GObject *)
          scenario->pipeline, "validate-monitor"));

  if (monitor->is_playbin)
    return execute_switch_track_pb (scenario, action);
  else if (monitor->is_playbin3)
    return execute_switch_track_pb3 (scenario, action);

  return execute_switch_track_default (scenario, action);
}

static gboolean
_set_rank (GstValidateScenario * scenario, GstValidateAction * action)
{
  guint rank;
  GstPluginFeature *feature;
  const gchar *feature_name;

  if (!(feature_name =
          gst_structure_get_string (action->structure, "feature-name"))) {
    GST_ERROR ("Could not find the name of the feature to tweak");

    return FALSE;
  }

  if (!(gst_structure_get_uint (action->structure, "rank", &rank) ||
          gst_structure_get_int (action->structure, "rank", (gint *) & rank))) {
    GST_ERROR ("Could not get rank to set on %s", feature_name);

    return FALSE;
  }

  feature = gst_registry_lookup_feature (gst_registry_get (), feature_name);
  if (!feature) {
    GST_ERROR ("Could not find feaure %s", feature_name);

    return FALSE;
  }

  gst_plugin_feature_set_rank (feature, rank);
  gst_object_unref (feature);

  return TRUE;
}

static inline gboolean
_add_execute_actions_gsource (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  SCENARIO_LOCK (scenario);
  if (priv->execute_actions_source_id == 0 && priv->wait_id == 0
      && priv->signal_handler_id == 0 && priv->message_type == NULL) {
    if (!scenario->priv->action_execution_interval)
      priv->execute_actions_source_id =
          g_idle_add ((GSourceFunc) execute_next_action, scenario);
    else
      priv->execute_actions_source_id =
          g_timeout_add (scenario->priv->action_execution_interval,
          (GSourceFunc) execute_next_action, scenario);
    SCENARIO_UNLOCK (scenario);

    GST_DEBUG_OBJECT (scenario, "Start checking position again");
    return TRUE;
  }
  SCENARIO_UNLOCK (scenario);

  GST_LOG_OBJECT (scenario, "No need to start a new gsource");
  return FALSE;
}

static gboolean
_get_position (GstValidateScenario * scenario,
    GstValidateAction * act, GstClockTime * position)
{
  gboolean has_pos = FALSE, has_dur = FALSE;
  GstClockTime duration = -1;

  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = scenario->pipeline;

  has_pos = gst_element_query_position (pipeline, GST_FORMAT_TIME,
      (gint64 *) position)
      && GST_CLOCK_TIME_IS_VALID (*position);
  has_dur =
      gst_element_query_duration (pipeline, GST_FORMAT_TIME,
      (gint64 *) & duration)
      && GST_CLOCK_TIME_IS_VALID (duration);

  if (!has_pos && GST_STATE (pipeline) >= GST_STATE_PAUSED &&
      act && GST_CLOCK_TIME_IS_VALID (act->playback_time)) {
    GST_INFO_OBJECT (scenario, "Unknown position: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (*position));

    return FALSE;
  }

  if (has_pos && has_dur && !priv->got_eos) {
    if (*position > duration) {
      _add_execute_actions_gsource (scenario);

      GST_VALIDATE_REPORT (scenario,
          QUERY_POSITION_SUPERIOR_DURATION,
          "Reported position %" GST_TIME_FORMAT " > reported duration %"
          GST_TIME_FORMAT, GST_TIME_ARGS (*position), GST_TIME_ARGS (duration));

      return TRUE;
    }
  }

  return TRUE;
}

static gboolean
_check_position (GstValidateScenario * scenario, GstValidateAction * act,
    GstClockTime * position)
{
  GstQuery *query;
  gdouble rate;

  GstClockTime start_with_tolerance, stop_with_tolerance;
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (scenario->pipeline == NULL) {
    GST_INFO_OBJECT (scenario, "No pipeline set anymore");

    return TRUE;
  }

  if (!_get_position (scenario, act, position))
    return FALSE;

  GST_DEBUG_OBJECT (scenario, "Current position: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (*position));

  /* Check if playback is within seek segment */
  start_with_tolerance = (priv->segment_start <
      priv->seek_pos_tol) ? 0 : priv->segment_start - priv->seek_pos_tol;
  stop_with_tolerance =
      GST_CLOCK_TIME_IS_VALID (priv->segment_stop) ? priv->segment_stop +
      priv->seek_pos_tol : -1;

  if ((GST_CLOCK_TIME_IS_VALID (stop_with_tolerance)
          && *position > stop_with_tolerance)
      || (priv->seek_flags & GST_SEEK_FLAG_ACCURATE
          && *position < start_with_tolerance)) {

    GST_VALIDATE_REPORT (scenario, QUERY_POSITION_OUT_OF_SEGMENT,
        "Current position %" GST_TIME_FORMAT " not in the expected range [%"
        GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, GST_TIME_ARGS (*position),
        GST_TIME_ARGS (start_with_tolerance),
        GST_TIME_ARGS (stop_with_tolerance));
  }

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (GST_ELEMENT (scenario->pipeline), query))
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);
  gst_query_unref (query);

  if (priv->seeked_in_pause && priv->seek_flags & GST_SEEK_FLAG_ACCURATE) {
    if ((rate > 0 && (*position >= priv->segment_start + priv->seek_pos_tol ||
                *position < ((priv->segment_start <
                        priv->seek_pos_tol) ? 0 : priv->segment_start -
                    priv->seek_pos_tol)))
        || (rate < 0 && (*position > priv->segment_start + priv->seek_pos_tol
                || *position < ((priv->segment_start <
                        priv->seek_pos_tol) ? 0 : priv->segment_start -
                    priv->seek_pos_tol)))) {
      priv->seeked_in_pause = FALSE;
      GST_VALIDATE_REPORT (scenario, EVENT_SEEK_RESULT_POSITION_WRONG,
          "Reported position after accurate seek in PAUSED state should be exactly"
          " what the user asked for. Position %" GST_TIME_FORMAT
          " is not not the expected one:  %" GST_TIME_FORMAT,
          GST_TIME_ARGS (*position), GST_TIME_ARGS (priv->segment_start));
    }
  }

  return TRUE;
}

static gboolean
_should_execute_action (GstValidateScenario * scenario, GstValidateAction * act,
    GstClockTime position, gdouble rate)
{

  if (!act) {
    GST_DEBUG_OBJECT (scenario, "No action to execute");

    return FALSE;
  } else if (scenario->pipeline == NULL) {

    if (!(GST_VALIDATE_ACTION_GET_TYPE (act)->flags &
            GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE)) {
      GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
          "Trying to execute an %s action after the pipeline has been destroyed"
          " but the type has not been marked as "
          "GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE", act->type);

      return FALSE;
    } else if (GST_CLOCK_TIME_IS_VALID (act->playback_time)) {
      GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
          "Trying to execute action %s with playback time %" GST_TIME_FORMAT
          " after the pipeline has been destroyed. It is impossible"
          " to execute an action with a playback time specified "
          " after the pipeline has been destroyed",
          act->type, GST_TIME_ARGS (act->playback_time));

      return FALSE;
    }

    GST_DEBUG_OBJECT (scenario, "No pipeline, go and execute action!");

    return TRUE;
  } else if (scenario->priv->got_eos) {
    GST_DEBUG_OBJECT (scenario, "Just got EOS go and execute next action!");
    scenario->priv->got_eos = FALSE;
  } else if (GST_STATE (scenario->pipeline) < GST_STATE_PAUSED) {
    GST_DEBUG_OBJECT (scenario, "Pipeline not even in paused, "
        "just executing actions");

    return TRUE;
  } else if (act->playback_time == GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (scenario, "No timing info, executing action");

    return TRUE;
  } else if ((rate > 0 && (GstClockTime) position < act->playback_time)) {
    GST_DEBUG_OBJECT (scenario, "positive rate and position %" GST_TIME_FORMAT
        " < playback_time %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
        GST_TIME_ARGS (act->playback_time));

    return FALSE;
  } else if (rate < 0 && (GstClockTime) position > act->playback_time) {
    GST_DEBUG_OBJECT (scenario, "negative rate and position %" GST_TIME_FORMAT
        " < playback_time %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
        GST_TIME_ARGS (act->playback_time));

    return FALSE;
  }

  return TRUE;
}

GstValidateExecuteActionReturn
gst_validate_execute_action (GstValidateActionType * action_type,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res;

  g_return_val_if_fail (g_strcmp0 (action_type->name, action->type) == 0,
      GST_VALIDATE_EXECUTE_ACTION_ERROR);

  if (action_type->prepare) {
    if (action_type->prepare (action) == FALSE) {
      GST_ERROR_OBJECT (action->scenario, "Action %" GST_PTR_FORMAT
          " could not be prepared", action->structure);

      return GST_VALIDATE_EXECUTE_ACTION_ERROR;
    }
  }

  gst_validate_print_action (action, NULL);

  action->priv->execution_time = gst_util_get_timestamp ();
  res = action_type->execute (action->scenario, action);

  if (!gst_structure_has_field (action->structure, "sub-action")) {
    gst_structure_free (action->structure);

    action->priv->printed = FALSE;
    action->structure = gst_structure_copy (action->priv->main_structure);

    if (res == GST_VALIDATE_EXECUTE_ACTION_ASYNC)
      action->priv->executing_last_subaction = TRUE;
  }

  return res;
}

static gboolean
_set_action_playback_time (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  if (!gst_validate_action_get_clocktime (scenario, action,
          "playback-time", &action->playback_time)) {
    gchar *str = gst_structure_to_string (action->structure);

    g_error ("Could not parse playback-time on structure: %s", str);
    g_free (str);

    return FALSE;
  }

  gst_structure_set (action->structure, "playback-time", GST_TYPE_CLOCK_TIME,
      action->playback_time, NULL);

  return TRUE;
}

static GstValidateExecuteActionReturn
_fill_action (GstValidateScenario * scenario, GstValidateAction * action,
    GstStructure * structure, gboolean add_to_lists)
{
  gdouble playback_time;
  gboolean is_config = FALSE;
  GstValidateActionType *action_type;
  const gchar *str_playback_time = NULL;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gboolean optional;

  action->type = gst_structure_get_name (structure);
  action_type = _find_action_type (action->type);

  if (!action_type) {
    GST_ERROR_OBJECT (scenario, "Action type %s no found",
        gst_structure_get_name (structure));

    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  if (gst_structure_get_double (structure, "playback-time", &playback_time) ||
      gst_structure_get_double (structure, "playback_time", &playback_time)) {
    action->playback_time = playback_time * GST_SECOND;
  } else if ((str_playback_time =
          gst_structure_get_string (structure, "playback-time")) ||
      (str_playback_time =
          gst_structure_get_string (structure, "playback_time"))) {

    if (add_to_lists)
      priv->needs_parsing = g_list_append (priv->needs_parsing, action);
    else if (!_set_action_playback_time (scenario, action))
      return GST_VALIDATE_EXECUTE_ACTION_ERROR;

  } else
    GST_INFO_OBJECT (scenario,
        "No playback time for action %" GST_PTR_FORMAT, structure);

  if (!gst_validate_utils_get_clocktime (structure,
          "timeout", &action->priv->timeout)) {
    GST_INFO_OBJECT (scenario,
        "No timeout time for action %" GST_PTR_FORMAT, structure);
  }

  if (!(action->name = gst_structure_get_string (structure, "name")))
    action->name = "";

  action->structure = gst_structure_copy (structure);

  if (!action->priv->main_structure)
    action->priv->main_structure = gst_structure_copy (structure);

  if (gst_structure_get_boolean (structure, "optional", &optional)) {
    if ((action_type->flags & GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL) == 0) {
      GST_ERROR_OBJECT (scenario, "Action type %s can't be optional",
          gst_structure_get_name (structure));
      return GST_VALIDATE_EXECUTE_ACTION_ERROR;
    }
    action->priv->optional = optional;
  }

  if (IS_CONFIG_ACTION_TYPE (action_type->flags) ||
      (gst_structure_get_boolean (action->structure, "as-config",
              &is_config) && is_config == TRUE)) {

    gst_validate_print_action (action, NULL);
    res = action_type->execute (scenario, action);

    return res;
  }

  if (!add_to_lists)
    return res;

  if (str_playback_time == NULL) {
    GstValidateActionType *type = _find_action_type (action->type);
    gboolean can_execute_on_addition =
        type->flags & GST_VALIDATE_ACTION_TYPE_CAN_EXECUTE_ON_ADDITION
        && !GST_CLOCK_TIME_IS_VALID (action->playback_time);

    if (priv->needs_parsing)
      can_execute_on_addition = FALSE;

    if (can_execute_on_addition) {
      GList *tmp;

      for (tmp = priv->actions; tmp; tmp = tmp->next) {
        if (GST_CLOCK_TIME_IS_VALID (((GstValidateAction *) tmp->
                    data)->playback_time)) {
          can_execute_on_addition = FALSE;
          break;
        }
      }

    }

    if (can_execute_on_addition) {
      SCENARIO_LOCK (scenario);
      priv->on_addition_actions = g_list_append (priv->on_addition_actions,
          action);
      SCENARIO_UNLOCK (scenario);

    } else {
      priv->actions = g_list_append (priv->actions, action);
    }
  }

  return res;
}

static GstValidateExecuteActionReturn
_execute_sub_action_action (GstValidateAction * action)
{
  const gchar *subaction_str;
  GstStructure *subaction_struct = NULL;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;

  if (action->priv->executing_last_subaction) {
    action->priv->executing_last_subaction = FALSE;

    goto done;
  }

  subaction_str = gst_structure_get_string (action->structure, "sub-action");
  if (subaction_str) {
    subaction_struct = gst_structure_from_string (subaction_str, NULL);

    if (subaction_struct == NULL) {
      GST_VALIDATE_REPORT (action->scenario, SCENARIO_FILE_MALFORMED,
          "Sub action %s could not be parsed", subaction_str);

      res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
      goto done;
    }

  } else {
    gst_structure_get (action->structure, "sub-action", GST_TYPE_STRUCTURE,
        &subaction_struct, NULL);
  }

  if (subaction_struct) {
    if (action->structure) {
      GST_INFO_OBJECT (action->scenario, "Clearing old action structure");
      gst_structure_free (action->structure);
    }

    res = _fill_action (action->scenario, action, subaction_struct, FALSE);
    if (res == GST_VALIDATE_EXECUTE_ACTION_ERROR) {
      GST_VALIDATE_REPORT (action->scenario, SCENARIO_ACTION_EXECUTION_ERROR,
          "Sub action %" GST_PTR_FORMAT " could not be filled",
          subaction_struct);

      goto done;
    }

    if (!GST_CLOCK_TIME_IS_VALID (action->playback_time)) {
      GstValidateActionType *action_type = _find_action_type (action->type);

      action->priv->printed = FALSE;
      res = gst_validate_execute_action (action_type, action);

      goto done;
    }

  }

done:
  if (subaction_struct)
    gst_structure_free (subaction_struct);
  return res;
}


/* This is the main action execution function
 * it checks whether it is time to run the next action
 * and if it is the case executes it.
 *
 * If the 'execute-on-idle' property is not TRUE,
 * the function will recurse while the actions are run
 * synchronously
 */
static gboolean
execute_next_action (GstValidateScenario * scenario)
{
  GList *tmp;
  gdouble rate = 1.0;
  GstClockTime position = -1;
  GstValidateAction *act = NULL;
  GstValidateActionType *type;

  GstValidateScenarioPrivate *priv = scenario->priv;

  if (priv->buffering) {
    GST_DEBUG_OBJECT (scenario, "Buffering not executing any action");

    return G_SOURCE_CONTINUE;
  }

  if (priv->changing_state || priv->needs_async_done) {
    GST_DEBUG_OBJECT (scenario, "Changing state, not executing any action");
    return G_SOURCE_CONTINUE;
  }

  if (scenario->priv->actions)
    act = scenario->priv->actions->data;

  if (act) {
    if (act->priv->state == GST_VALIDATE_EXECUTE_ACTION_OK && act->repeat <= 0) {
      tmp = priv->actions;
      priv->actions = g_list_remove_link (priv->actions, tmp);

      GST_INFO_OBJECT (scenario, "Action %" GST_PTR_FORMAT " is DONE now"
          " executing next", act->structure);

      gst_validate_action_unref (act);
      g_list_free (tmp);

      if (scenario->priv->actions) {
        act = scenario->priv->actions->data;
      } else {
        _check_scenario_is_done (scenario);
        act = NULL;
      }
    } else if (act->priv->state == GST_VALIDATE_EXECUTE_ACTION_ASYNC) {
      if (GST_CLOCK_TIME_IS_VALID (act->priv->timeout)) {
        GstClockTime etime =
            gst_util_get_timestamp () - act->priv->execution_time;

        if (etime > act->priv->timeout) {
          gchar *str = gst_structure_to_string (act->structure);

          GST_VALIDATE_REPORT (scenario,
              SCENARIO_ACTION_EXECUTION_ERROR,
              "Action %s timed out after: %" GST_TIME_FORMAT, str,
              GST_TIME_ARGS (etime));

          g_free (str);
        }
      }
      GST_LOG_OBJECT (scenario, "Action %" GST_PTR_FORMAT " still running",
          act->structure);

      return G_SOURCE_CONTINUE;
    }
  }

  if (!_check_position (scenario, act, &position))
    return G_SOURCE_CONTINUE;

  if (!_should_execute_action (scenario, act, position, rate)) {
    _add_execute_actions_gsource (scenario);

    return G_SOURCE_CONTINUE;
  }

  type = _find_action_type (act->type);

  if (act->repeat == -1 &&
      !gst_structure_get_int (act->structure, "repeat", &act->repeat)) {
    gchar *error = NULL;
    const gchar *repeat_expr = gst_structure_get_string (act->structure,
        "repeat");

    if (repeat_expr) {
      act->repeat =
          gst_validate_utils_parse_expression (repeat_expr,
          _set_variable_func, scenario, &error);
    }
  }

  GST_DEBUG_OBJECT (scenario, "Executing %" GST_PTR_FORMAT
      " at %" GST_TIME_FORMAT, act->structure, GST_TIME_ARGS (position));
  priv->seeked_in_pause = FALSE;

  act->priv->state = gst_validate_execute_action (type, act);
  if (act->priv->state == GST_VALIDATE_EXECUTE_ACTION_ERROR) {
    gchar *str = gst_structure_to_string (act->structure);

    GST_VALIDATE_REPORT (scenario,
        SCENARIO_ACTION_EXECUTION_ERROR, "Could not execute %s", str);

    g_free (str);
  }

  if (act->repeat > 0 && !gst_validate_action_is_subaction (act)) {
    act->repeat--;
  }

  if (act->priv->state == GST_VALIDATE_EXECUTE_ACTION_OK) {
    act->priv->state = _execute_sub_action_action (act);
  }

  if (act->priv->state != GST_VALIDATE_EXECUTE_ACTION_ASYNC) {
    tmp = priv->actions;
    priv->actions = g_list_remove_link (priv->actions, tmp);

    if (act->priv->state != GST_VALIDATE_EXECUTE_ACTION_INTERLACED)
      gst_validate_action_unref (act);
    else {
      SCENARIO_LOCK (scenario);
      priv->interlaced_actions = g_list_append (priv->interlaced_actions, act);
      SCENARIO_UNLOCK (scenario);
    }

    if (priv->actions == NULL)
      _check_scenario_is_done (scenario);

    g_list_free (tmp);

    /* Recurse to the next action if it is possible
     * to execute right away */
    if (!scenario->priv->execute_on_idle) {
      GST_DEBUG_OBJECT (scenario, "linking next action execution");

      return execute_next_action (scenario);
    } else {
      _add_execute_actions_gsource (scenario);
      GST_DEBUG_OBJECT (scenario, "Executing only on idle, waiting for"
          " next dispatch");

      return G_SOURCE_CONTINUE;
    }
  } else {
    GST_DEBUG_OBJECT (scenario, "Remove source, waiting for action"
        " to be done.");

    SCENARIO_LOCK (scenario);
    priv->execute_actions_source_id = 0;
    SCENARIO_UNLOCK (scenario);

    return G_SOURCE_CONTINUE;
  }

  return G_SOURCE_CONTINUE;
}

static gboolean
stop_waiting (GstValidateAction * action)
{
  GstValidateScenario *scenario = action->scenario;

  gst_validate_printf (action->scenario, "Stop waiting\n");

  SCENARIO_LOCK (scenario);
  scenario->priv->wait_id = 0;
  SCENARIO_UNLOCK (scenario);

  gst_validate_action_set_done (action);
  _add_execute_actions_gsource (scenario);


  return G_SOURCE_REMOVE;
}

static GstElement *_get_target_element (GstValidateScenario * scenario,
    GstValidateAction * action);

static void
stop_waiting_signal (GstBin * bin, GstElement * element,
    GstValidateAction * action)
{
  GstValidateScenario *scenario = action->scenario;
  GstValidateScenarioPrivate *priv = scenario->priv;

  gst_validate_printf (scenario, "Stop waiting for signal\n");

  g_signal_handler_disconnect (bin, priv->signal_handler_id);

  priv->signal_handler_id = 0;
  gst_validate_action_set_done (action);
  _add_execute_actions_gsource (scenario);
}

static GstValidateExecuteActionReturn
_execute_timed_wait (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstClockTime duration;

  gdouble wait_multiplier = 1;
  const gchar *str_wait_multiplier =
      g_getenv ("GST_VALIDATE_SCENARIO_WAIT_MULTIPLIER");

  if (str_wait_multiplier) {
    errno = 0;
    wait_multiplier = g_ascii_strtod (str_wait_multiplier, NULL);

    if (errno) {
      GST_ERROR ("Could not use the WAIT MULTIPLIER");

      wait_multiplier = 1;
    }

    if (wait_multiplier == 0) {
      GST_INFO_OBJECT (scenario, "I have been told not to wait...");
      return GST_VALIDATE_EXECUTE_ACTION_OK;
    }
  }

  if (!gst_validate_action_get_clocktime (scenario, action,
          "duration", &duration)) {
    GST_DEBUG_OBJECT (scenario, "Duration could not be parsed");

    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  duration *= wait_multiplier;

  SCENARIO_LOCK (scenario);
  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }
  SCENARIO_UNLOCK (scenario);

  SCENARIO_LOCK (scenario);
  priv->wait_id = g_timeout_add (duration / G_USEC_PER_SEC,
      (GSourceFunc) stop_waiting, action);
  SCENARIO_UNLOCK (scenario);

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static GstValidateExecuteActionReturn
_execute_wait_for_signal (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const gchar *signal_name = gst_structure_get_string
      (action->structure, "signal-name");
  GstElement *target;

  if (signal_name == NULL) {
    GST_ERROR ("No signal-name given for wait action");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR;
  }

  if (scenario->pipeline == NULL) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Can't execute a 'wait for signal' action after the pipeline "
        " has been destroyed.");

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  target = _get_target_element (scenario, action);
  if (target == NULL) {
    return FALSE;
  }

  gst_validate_printf (action, "Waiting for '%s' signal\n", signal_name);

  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }

  priv->signal_handler_id =
      g_signal_connect (target, signal_name, (GCallback) stop_waiting_signal,
      action);

  gst_object_unref (target);

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static gboolean
_execute_wait_for_message (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const gchar *message_type = gst_structure_get_string
      (action->structure, "message-type");

  if (scenario->pipeline == NULL) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Can't execute a 'wait for message' action after the pipeline "
        " has been destroyed.");

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  gst_validate_printf (action, "Waiting for '%s' message\n", message_type);

  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }

  priv->message_type = g_strdup (message_type);

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static GstValidateExecuteActionReturn
_execute_wait (GstValidateScenario * scenario, GstValidateAction * action)
{
  if (gst_structure_has_field (action->structure, "signal-name")) {
    return _execute_wait_for_signal (scenario, action);
  } else if (gst_structure_has_field (action->structure, "message-type")) {
    return _execute_wait_for_message (scenario, action);
  } else {
    return _execute_timed_wait (scenario, action);
  }

  return FALSE;
}

static gboolean
_execute_dot_pipeline (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gchar *dotname;
  gint details = GST_DEBUG_GRAPH_SHOW_ALL;

  const gchar *name = gst_structure_get_string (action->structure, "name");

  gst_structure_get_int (action->structure, "details", &details);
  if (name)
    dotname = g_strdup_printf ("validate.action.%s", name);
  else
    dotname = g_strdup ("validate.action.unnamed");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (scenario->pipeline),
      details, dotname);

  g_free (dotname);

  return TRUE;
}

static GstElement *
_get_target_element (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *name;
  GstElement *target;

  name = gst_structure_get_string (action->structure, "target-element-name");
  if (name == NULL) {
    return NULL;
  }

  if (strcmp (GST_OBJECT_NAME (scenario->pipeline), name) == 0) {
    target = gst_object_ref (scenario->pipeline);
  } else {
    target = gst_bin_get_by_name (GST_BIN (scenario->pipeline), name);
  }

  if (target == NULL) {
    GST_ERROR ("Target element with given name (%s) not found", name);
  }
  return target;
}

static gint
cmp_klass_name (gconstpointer a, gconstpointer b)
{
  const GValue *v = a;
  const GValue *param = b;
  GstElement *element = g_value_get_object (v);
  const gchar *klass = g_value_get_string (param);

  if (gst_validate_element_has_klass (element, klass))
    return 0;

  return 1;
}

/**
 * _get_target_elements_by_klass:
 * @scenario: a #GstValidateScenario
 * @action: a #GstValidateAction
 *
 * Returns all the elements in the pipeline whose the GST_ELEMENT_METADATA_KLASS
 * matches the 'target-element-klass' of @action.
 *
 * Returns: (transfer full) (element-type GstElement): a list of #GstElement
 */
static GList *
_get_target_elements_by_klass (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GList *result = NULL;
  GstIterator *it, *filtered;
  const gchar *klass;
  GValue v = G_VALUE_INIT, param = G_VALUE_INIT;
  gboolean done = FALSE;

  klass = gst_structure_get_string (action->structure, "target-element-klass");
  if (klass == NULL)
    return NULL;

  if (gst_validate_element_has_klass (scenario->pipeline, klass))
    result = g_list_prepend (result, gst_object_ref (scenario->pipeline));

  it = gst_bin_iterate_recurse (GST_BIN (scenario->pipeline));

  g_value_init (&param, G_TYPE_STRING);
  g_value_set_string (&param, klass);

  filtered = gst_iterator_filter (it, cmp_klass_name, &param);

  while (!done) {
    switch (gst_iterator_next (filtered, &v)) {
      case GST_ITERATOR_OK:{
        GstElement *child = g_value_get_object (&v);

        if (g_list_find (result, child) == NULL)
          result = g_list_prepend (result, gst_object_ref (child));
        g_value_reset (&v);
      }
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
    }
  }

  g_value_reset (&v);
  g_value_reset (&param);
  gst_iterator_free (filtered);

  return result;
}

static gboolean
_object_set_property (GObject * object, const gchar * property,
    const GValue * value, gboolean optional)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (object);
  GParamSpec *paramspec;

  paramspec = g_object_class_find_property (klass, property);
  if (paramspec == NULL) {
    if (optional)
      return TRUE;
    GST_ERROR ("Target doesn't have property %s", property);
    return FALSE;
  }

  g_object_set_property (object, property, value);

  return TRUE;
}

static gboolean
_execute_set_property (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstElement *target;
  GList *targets = NULL, *l;
  const gchar *property;
  const GValue *property_value;
  gboolean ret = TRUE;

  /* set-property can be applied on either:
   * - a single element having target-element-name as name
   * - all the elements having target-element-klass as klass
   */
  if (gst_structure_get_string (action->structure, "target-element-name")) {
    target = _get_target_element (scenario, action);
    if (target == NULL) {
      return FALSE;
    }
    targets = g_list_append (targets, target);
  } else if (gst_structure_get_string (action->structure,
          "target-element-klass")) {
    targets = _get_target_elements_by_klass (scenario, action);
  } else {
    g_assert_not_reached ();
  }

  property = gst_structure_get_string (action->structure, "property-name");
  property_value = gst_structure_get_value (action->structure,
      "property-value");

  for (l = targets; l != NULL; l = g_list_next (l)) {
    if (!_object_set_property (G_OBJECT (l->data), property, property_value,
            action->priv->optional))
      ret = FALSE;
  }

  g_list_free_full (targets, gst_object_unref);
  return ret;
}

static gboolean
_execute_set_debug_threshold (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gchar *str = NULL;
  gboolean reset = TRUE;
  const gchar *threshold_str;

  threshold_str =
      gst_structure_get_string (action->structure, "debug-threshold");
  if (threshold_str == NULL) {
    gint threshold;

    if (gst_structure_get_int (action->structure, "debug-threshold",
            &threshold))
      threshold_str = str = g_strdup_printf ("%i", threshold);
    else
      return FALSE;
  }

  gst_structure_get_boolean (action->structure, "reset", &reset);

  gst_debug_set_threshold_from_string (threshold_str, reset);

  if (str)
    g_free (str);

  return TRUE;
}

static gboolean
_execute_emit_signal (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstElement *target;
  const gchar *signal_name;

  target = _get_target_element (scenario, action);
  if (target == NULL) {
    return FALSE;
  }

  signal_name = gst_structure_get_string (action->structure, "signal-name");

  /* Right now we don't support arguments to signals as there weren't any use
   * cases to cover yet but it should be possible to do so */
  g_signal_emit_by_name (target, signal_name, NULL);

  gst_object_unref (target);
  return TRUE;
}

static GstValidateExecuteActionReturn
_execute_disable_plugin (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstPlugin *plugin;
  const gchar *plugin_name;

  plugin_name = gst_structure_get_string (action->structure, "plugin-name");

  plugin = gst_registry_find_plugin (gst_registry_get (), plugin_name);

  if (plugin == NULL) {
    GST_VALIDATE_REPORT (action->scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not find plugin to disable: %s", plugin_name);

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  gst_registry_remove_plugin (gst_registry_get (), plugin);

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static void
gst_validate_scenario_update_segment_from_seek (GstValidateScenario * scenario,
    GstEvent * seek)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  gint64 start, stop;
  GstSeekType start_type, stop_type;

  gst_event_parse_seek (seek, NULL, NULL, NULL, &start_type, &start,
      &stop_type, &stop);

  if (start_type == GST_SEEK_TYPE_SET) {
    priv->segment_start = start;
  } else if (start_type == GST_SEEK_TYPE_END) {
    /* TODO fill me */
  }

  if (stop_type == GST_SEEK_TYPE_SET) {
    priv->segment_stop = stop;
  } else if (start_type == GST_SEEK_TYPE_END) {
    /* TODO fill me */
  }
}

static gint
_compare_actions (GstValidateAction * a, GstValidateAction * b)
{
  if (a->action_number < b->action_number)
    return -1;
  else if (a->action_number == b->action_number)
    return 0;

  return 1;
}

static gboolean
gst_validate_action_default_prepare_func (GstValidateAction * action)
{
  gulong i;
  GstClockTime time;
  const gchar *vars[] = { "duration", "start", "stop" };

  for (i = 0; i < G_N_ELEMENTS (vars); i++) {
    gint res =
        gst_validate_action_get_clocktime (action->scenario, action, vars[i],
        &time);
    if (res == FALSE) {
      GST_ERROR_OBJECT (action->scenario, "Could not get clocktime for"
          " variable %s", vars[i]);

      return FALSE;
    } else if (res == -1) {
      continue;
    }

    gst_structure_set (action->structure, vars[i], GST_TYPE_CLOCK_TIME,
        time, NULL);
  }

  return TRUE;
}

static void
_check_waiting_for_message (GstValidateScenario * scenario,
    GstMessage * message)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (!g_strcmp0 (priv->message_type,
          gst_message_type_get_name (GST_MESSAGE_TYPE (message)))) {
    GstValidateAction *action = scenario->priv->actions->data;

    g_free ((gpointer) priv->message_type);
    priv->message_type = NULL;

    gst_validate_printf (scenario, "Stop waiting for message\n");

    gst_validate_action_set_done (action);
    _add_execute_actions_gsource (scenario);
  }
}

static gboolean
streams_list_contain (GList * streams, const gchar * stream_id)
{
  GList *l;

  for (l = streams; l; l = g_list_next (l)) {
    GstStream *s = l->data;

    if (!g_strcmp0 (s->stream_id, stream_id))
      return TRUE;
  }

  return FALSE;
}

static gboolean
message_cb (GstBus * bus, GstMessage * message, GstValidateScenario * scenario)
{
  gboolean is_error = FALSE;
  GstValidateScenarioPrivate *priv = scenario->priv;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ASYNC_DONE:
      if (priv->last_seek) {
        gst_validate_scenario_update_segment_from_seek (scenario,
            priv->last_seek);

        if (priv->target_state == GST_STATE_PAUSED)
          priv->seeked_in_pause = TRUE;

        gst_event_replace (&priv->last_seek, NULL);
        gst_validate_action_set_done (priv->actions->data);
      } else if (scenario->priv->needs_async_done) {
        scenario->priv->needs_async_done = FALSE;
        if (priv->actions && _action_sets_state (priv->actions->data)
            && !priv->changing_state)
          gst_validate_action_set_done (priv->actions->data);

      }

      if (priv->needs_parsing) {
        GList *tmp;

        for (tmp = priv->needs_parsing; tmp; tmp = tmp->next) {
          GstValidateAction *action = tmp->data;

          if (!_set_action_playback_time (scenario, action))
            return FALSE;

          priv->actions = g_list_insert_sorted (priv->actions, action,
              (GCompareFunc) _compare_actions);
        }

        g_list_free (priv->needs_parsing);
        priv->needs_parsing = NULL;
      }
      _add_execute_actions_gsource (scenario);
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (scenario->pipeline)) {
        GstState nstate, pstate;

        gst_message_parse_state_changed (message, &pstate, &nstate, NULL);

        if (scenario->priv->changing_state &&
            scenario->priv->target_state == nstate) {
          scenario->priv->changing_state = FALSE;

          if (priv->actions && _action_sets_state (priv->actions->data) &&
              !priv->needs_async_done)
            gst_validate_action_set_done (priv->actions->data);
        }

        if (pstate == GST_STATE_READY && nstate == GST_STATE_PAUSED)
          _add_execute_actions_gsource (scenario);
      }
      break;
    }
    case GST_MESSAGE_ERROR:
      is_error = TRUE;

      /* Passtrough */
    case GST_MESSAGE_EOS:
    {
      GstValidateAction *stop_action;
      GstValidateActionType *stop_action_type;
      GstStructure *s;

      if (!is_error) {
        priv->got_eos = TRUE;
        if (priv->message_type) {

          if (priv->actions->next) {
            GST_DEBUG_OBJECT (scenario,
                "Waiting for a message and got a next action"
                " to execute, letting it a chance!");
            goto done;
          } else {
            /* Clear current message wait if waiting for EOS */
            _check_waiting_for_message (scenario, message);
          }
        }
      }

      SCENARIO_LOCK (scenario);
      if (scenario->priv->actions || scenario->priv->interlaced_actions ||
          scenario->priv->on_addition_actions) {
        guint nb_actions = 0;
        gchar *actions = g_strdup (""), *tmpconcat;
        GList *tmp;
        GList *all_actions =
            g_list_concat (g_list_concat (scenario->priv->actions,
                scenario->priv->interlaced_actions),
            scenario->priv->on_addition_actions);

        for (tmp = all_actions; tmp; tmp = tmp->next) {
          gchar *action_string;
          GstValidateAction *action = ((GstValidateAction *) tmp->data);
          GstValidateActionType *type = _find_action_type (action->type);

          tmpconcat = actions;

          if (type->flags & GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL ||
              action->priv->state == GST_VALIDATE_EXECUTE_ACTION_OK ||
              action->priv->optional) {
            gst_validate_action_unref (action);

            continue;
          }

          nb_actions++;

          action_string = gst_structure_to_string (action->structure);
          actions =
              g_strdup_printf ("%s\n%*s%s", actions, 20, "", action_string);
          gst_validate_action_unref (action);
          g_free (tmpconcat);
          g_free (action_string);
        }
        g_list_free (all_actions);
        scenario->priv->actions = NULL;
        scenario->priv->interlaced_actions = NULL;
        scenario->priv->on_addition_actions = NULL;

        if (nb_actions > 0)
          GST_VALIDATE_REPORT (scenario, SCENARIO_NOT_ENDED,
              "%i actions were not executed: %s", nb_actions, actions);
        g_free (actions);
      }
      SCENARIO_UNLOCK (scenario);

      GST_DEBUG_OBJECT (scenario, "Got EOS; generate 'stop' action");

      stop_action_type = _find_action_type ("stop");
      stop_action = gst_validate_action_new (scenario, stop_action_type);
      s = gst_structure_from_string ("stop;", NULL);
      _fill_action (scenario, stop_action, s, FALSE);
      gst_structure_free (s);
      gst_validate_execute_action (stop_action_type, stop_action);
      gst_mini_object_unref (GST_MINI_OBJECT (stop_action));

      break;
    }
    case GST_MESSAGE_BUFFERING:
    {
      gint percent;

      gst_message_parse_buffering (message, &percent);

      if (percent == 100)
        priv->buffering = FALSE;
      else
        priv->buffering = TRUE;

      g_print ("%s %d%%  \r", "Buffering...", percent);
      break;
    }
    case GST_MESSAGE_STREAMS_SELECTED:
    {
      guint i;
      GList *streams_selected = NULL;

      for (i = 0; i < gst_message_streams_selected_get_size (message); i++) {
        GstStream *stream =
            gst_message_streams_selected_get_stream (message, i);

        streams_selected = g_list_append (streams_selected, stream);
      }

      /* Is there a pending switch-track action waiting for the new streams to
       * be selected? */
      if (priv->pending_switch_track) {
        GList *expected, *l;

        expected =
            gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST
            (priv->pending_switch_track), ACTION_EXPECTED_STREAM_QUARK);

        if (g_list_length (expected) != g_list_length (streams_selected)) {
          GST_VALIDATE_REPORT (priv->pending_switch_track->scenario,
              SCENARIO_ACTION_EXECUTION_ERROR,
              "Was expecting %d selected streams but got %d",
              g_list_length (expected), g_list_length (streams_selected));
          goto action_done;
        }

        for (l = expected; l; l = g_list_next (l)) {
          const gchar *stream_id = l->data;

          if (!streams_list_contain (streams_selected, stream_id)) {
            GST_VALIDATE_REPORT (priv->pending_switch_track->scenario,
                SCENARIO_ACTION_EXECUTION_ERROR,
                "Stream %s has not be activated", stream_id);
            goto action_done;
          }
        }

      action_done:
        gst_validate_action_set_done (priv->pending_switch_track);
        priv->pending_switch_track = NULL;
      }

      g_list_free_full (streams_selected, gst_object_unref);
      break;
    }

    default:
      break;
  }

done:
  /* Check if we got the message expected by a wait action */
  if (priv->message_type)
    _check_waiting_for_message (scenario, message);

  return TRUE;
}

static void
_pipeline_freed_cb (GstValidateScenario * scenario,
    GObject * where_the_object_was)
{
  /* Because g_object_weak_ref() is used, this MUST be on the
   * main thread. */
  g_assert (g_main_context_acquire (g_main_context_default ()));
  g_main_context_release (g_main_context_default ());

  scenario->pipeline = NULL;

  GST_DEBUG_OBJECT (scenario, "pipeline was freed");
}

static gboolean
_load_scenario_file (GstValidateScenario * scenario,
    const gchar * scenario_file, gboolean * is_config)
{
  gboolean ret = TRUE;
  GList *structures, *tmp;
  GstValidateScenarioPrivate *priv = scenario->priv;

  *is_config = FALSE;

  structures = gst_validate_utils_structs_parse_from_filename (scenario_file);
  if (structures == NULL)
    goto failed;

  for (tmp = structures; tmp; tmp = tmp->next) {
    GstValidateAction *action;
    GstValidateActionType *action_type;
    const gchar *type;

    GstStructure *structure = tmp->data;


    type = gst_structure_get_name (structure);
    if (!g_strcmp0 (type, "description")) {
      const gchar *pipeline_name;

      gst_structure_get_boolean (structure, "is-config", is_config);
      gst_structure_get_boolean (structure, "handles-states",
          &priv->handles_state);

      pipeline_name = gst_structure_get_string (structure, "pipeline-name");
      if (pipeline_name) {
        g_free (priv->pipeline_name);
        priv->pipeline_name = g_strdup (pipeline_name);
      }

      continue;
    } else if (!(action_type = _find_action_type (type))) {
      if (gst_structure_has_field (structure, "optional-action-type")) {
        GST_INFO_OBJECT (scenario,
            "Action type not found %s but marked as not mandatory", type);
        continue;
      }

      GST_ERROR_OBJECT (scenario, "We do not handle action types %s", type);
      goto failed;
    }

    if (action_type->parameters) {
      guint i;

      for (i = 0; action_type->parameters[i].name; i++) {
        if (action_type->parameters[i].mandatory &&
            gst_structure_has_field (structure,
                action_type->parameters[i].name) == FALSE) {
          GST_ERROR_OBJECT (scenario,
              "Mandatory field '%s' not present in structure: %" GST_PTR_FORMAT,
              action_type->parameters[i].name, structure);
          goto failed;
        }
      }
    }

    action = gst_validate_action_new (scenario, action_type);
    if (_fill_action (scenario, action,
            structure, TRUE) == GST_VALIDATE_EXECUTE_ACTION_ERROR)
      goto failed;

    action->action_number = priv->num_actions++;
  }

done:
  g_list_free_full (structures, (GDestroyNotify) gst_structure_free);

  return ret;

failed:
  ret = FALSE;

  goto done;
}

static gboolean
gst_validate_scenario_load (GstValidateScenario * scenario,
    const gchar * scenario_name)
{
  gchar **scenarios = NULL;
  guint i;
  gboolean found_actions = FALSE, is_config, ret = TRUE;
  const gchar *scenarios_path = g_getenv ("GST_VALIDATE_SCENARIOS_PATH");

  gchar **env_scenariodir =
      scenarios_path ? g_strsplit (scenarios_path, ":", 0) : NULL;

  if (!scenario_name)
    goto invalid_name;

  scenarios = g_strsplit (scenario_name, ":", -1);

  for (i = 0; scenarios[i]; i++) {
    gchar *lfilename = NULL, *tldir = NULL;

    /* First check if the scenario name is not a full path to the
     * actual scenario */
    if (g_file_test (scenarios[i], G_FILE_TEST_IS_REGULAR)) {
      GST_DEBUG_OBJECT (scenario, "Scenario: %s is a full path to a scenario "
          "trying to load it", scenarios[i]);
      if ((ret = _load_scenario_file (scenario, scenarios[i], &is_config)))
        goto check_scenario;
    }

    lfilename =
        g_strdup_printf ("%s" GST_VALIDATE_SCENARIO_SUFFIX, scenarios[i]);

    tldir = g_build_filename ("data", "scenarios", lfilename, NULL);

    if ((ret = _load_scenario_file (scenario, tldir, &is_config)))
      goto check_scenario;

    g_free (tldir);

    if (env_scenariodir) {
      guint i;

      for (i = 0; env_scenariodir[i]; i++) {
        tldir = g_build_filename (env_scenariodir[i], lfilename, NULL);
        if ((ret = _load_scenario_file (scenario, tldir, &is_config)))
          goto check_scenario;
        g_free (tldir);
      }
    }

    /* Try from local profiles */
    tldir =
        g_build_filename (g_get_user_data_dir (),
        "gstreamer-" GST_API_VERSION, "validate",
        GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);

    if (!(ret = _load_scenario_file (scenario, tldir, &is_config))) {
      g_free (tldir);
      /* Try from system-wide profiles */
      tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
          "validate", GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);

      if (!(ret = _load_scenario_file (scenario, tldir, &is_config))) {
        goto error;
      }
    }
    /* else check scenario */
  check_scenario:
    if (tldir)
      g_free (tldir);
    if (lfilename)
      g_free (lfilename);

    if (!is_config) {
      if (found_actions == TRUE)
        goto one_actions_scenario_max;
      else
        found_actions = TRUE;
    }
  }

done:

  if (env_scenariodir)
    g_strfreev (env_scenariodir);

  g_strfreev (scenarios);

  if (ret == FALSE)
    g_error ("Could not set scenario %s => EXIT\n", scenario_name);

  return ret;

invalid_name:
  {
    GST_ERROR ("Invalid name for scenario '%s'", scenario_name);
  error:
    ret = FALSE;
    goto done;
  }
one_actions_scenario_max:
  {
    GST_ERROR ("You can set at most only one action scenario. "
        "You can have several config scenarios though (a config scenario's "
        "file must have is-config=true, and all its actions must be executable "
        "at parsing time).");
    ret = FALSE;
    goto done;

  }
}


static void
gst_validate_scenario_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstValidateScenario *self = GST_VALIDATE_SCENARIO (object);

  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    case PROP_HANDLES_STATE:
      g_assert_not_reached ();
      break;
    case PROP_EXECUTE_ON_IDLE:
      self->priv->execute_on_idle = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_validate_scenario_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstValidateScenario *self = GST_VALIDATE_SCENARIO (object);

  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
      break;
    case PROP_HANDLES_STATE:
      g_value_set_boolean (value, self->priv->handles_state);
      break;
    case PROP_EXECUTE_ON_IDLE:
      g_value_set_boolean (value, self->priv->execute_on_idle);
      break;
    default:
      break;
  }
}

static void
gst_validate_scenario_class_init (GstValidateScenarioClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstValidateScenarioPrivate));

  object_class->dispose = gst_validate_scenario_dispose;
  object_class->finalize = gst_validate_scenario_finalize;

  object_class->get_property = gst_validate_scenario_get_property;
  object_class->set_property = gst_validate_scenario_set_property;

  g_object_class_install_property (object_class, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_HANDLES_STATE,
      g_param_spec_boolean ("handles-states", "Handles state",
          "True if the application should not set handle the first state change "
          " False if it is application responsibility",
          FALSE, G_PARAM_READABLE));

  g_object_class_install_property (object_class,
      PROP_EXECUTE_ON_IDLE,
      g_param_spec_boolean ("execute-on-idle",
          "Force waiting between actions",
          "Always execute actions on idle and do not chain them"
          " to execute as fast as possible. That is usefull if action execution"
          " can lead to the addition of source on the same main loop."
          " It allows those other GSources to have a chance to be dispatch between"
          " validate actions execution", FALSE, G_PARAM_READWRITE));

  /**
   * GstValidateScenario::done:
   * @scenario: The scenario runing
   *
   * Emitted once all actions have been executed
   */
  scenario_signals[DONE] =
      g_signal_new ("done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
gst_validate_scenario_init (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv =
      GST_VALIDATE_SCENARIO_GET_PRIVATE (scenario);

  priv->seek_pos_tol = DEFAULT_SEEK_TOLERANCE;
  priv->segment_start = 0;
  priv->segment_stop = GST_CLOCK_TIME_NONE;
  priv->action_execution_interval = 10;

  g_mutex_init (&priv->lock);
}

static void
gst_validate_scenario_dispose (GObject * object)
{
  GstValidateScenarioPrivate *priv = GST_VALIDATE_SCENARIO (object)->priv;

  if (priv->last_seek)
    gst_event_unref (priv->last_seek);
  if (GST_VALIDATE_SCENARIO (object)->pipeline)
    g_object_weak_unref (G_OBJECT (GST_VALIDATE_SCENARIO (object)->pipeline),
        (GWeakNotify) _pipeline_freed_cb, object);

  if (priv->bus) {
    gst_bus_remove_signal_watch (priv->bus);
    gst_object_unref (priv->bus);
    priv->bus = NULL;
  }

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->dispose (object);
}

static void
gst_validate_scenario_finalize (GObject * object)
{
  GstValidateScenarioPrivate *priv = GST_VALIDATE_SCENARIO (object)->priv;

  /* Because g_object_add_weak_pointer() is used, this MUST be on the
   * main thread. */
  g_assert (g_main_context_acquire (g_main_context_default ()));
  g_main_context_release (g_main_context_default ());

  g_list_free_full (priv->actions, (GDestroyNotify) gst_mini_object_unref);
  g_list_free_full (priv->interlaced_actions,
      (GDestroyNotify) gst_mini_object_unref);
  g_list_free_full (priv->on_addition_actions,
      (GDestroyNotify) gst_mini_object_unref);
  g_list_free_full (priv->needs_parsing,
      (GDestroyNotify) gst_mini_object_unref);
  g_free (priv->pipeline_name);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->finalize (object);
}

static void _element_added_cb (GstBin * bin, GstElement * element,
    GstValidateScenario * scenario);

static void
iterate_children (GstValidateScenario * scenario, GstBin * bin)
{
  GstIterator *it;
  GValue v = G_VALUE_INIT;
  gboolean done = FALSE;
  GHashTable *called;           /* set of GstElement on which we already called _element_added_cb() */

  called = g_hash_table_new (NULL, NULL);
  it = gst_bin_iterate_elements (bin);

  while (!done) {
    switch (gst_iterator_next (it, &v)) {
      case GST_ITERATOR_OK:{
        GstElement *child = g_value_get_object (&v);

        if (g_hash_table_lookup (called, child) == NULL) {
          _element_added_cb (bin, child, scenario);
          g_hash_table_add (called, child);
        }
        g_value_reset (&v);
      }
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
      case GST_ITERATOR_ERROR:
      case GST_ITERATOR_DONE:
        done = TRUE;
    }
  }
  g_value_reset (&v);
  gst_iterator_free (it);
  g_hash_table_unref (called);
}

static gboolean
should_execute_action (GstElement * element, GstValidateAction * action)
{
  const gchar *tmp;

  tmp = gst_structure_get_string (action->structure, "target-element-name");
  if (tmp != NULL && !strcmp (tmp, GST_ELEMENT_NAME (element)))
    return TRUE;

  tmp = gst_structure_get_string (action->structure, "target-element-klass");
  if (tmp != NULL && gst_validate_element_has_klass (element, tmp))
    return TRUE;

  return FALSE;
}

static void
_element_added_cb (GstBin * bin, GstElement * element,
    GstValidateScenario * scenario)
{
  GList *tmp;

  GstValidateScenarioPrivate *priv = scenario->priv;

  /* Check if it's an element we track for a set-property action */
  SCENARIO_LOCK (scenario);
  tmp = priv->on_addition_actions;
  while (tmp) {
    GstValidateAction *action = (GstValidateAction *) tmp->data;

    if (action->playback_time != GST_CLOCK_TIME_NONE)
      break;
    if (g_strcmp0 (action->type, "set-property"))
      break;

    GST_DEBUG_OBJECT (bin, "Checking action #%d %p (%s)", action->action_number,
        action, action->type);
    if (should_execute_action (element, action)) {
      GstValidateActionType *action_type;
      action_type = _find_action_type (action->type);
      GST_DEBUG_OBJECT (element, "Executing set-property action");
      if (action_type->execute (scenario, action)) {
        priv->on_addition_actions =
            g_list_remove_link (priv->on_addition_actions, tmp);
        gst_mini_object_unref (GST_MINI_OBJECT (action));
        g_list_free (tmp);
        tmp = priv->on_addition_actions;
      } else
        tmp = tmp->next;
    } else
      tmp = tmp->next;
  }
  SCENARIO_UNLOCK (scenario);

  _check_scenario_is_done (scenario);

  /* If it's a bin, listen to the child */
  if (GST_IS_BIN (element)) {
    g_signal_connect (element, "element-added", (GCallback) _element_added_cb,
        scenario);
    iterate_children (scenario, GST_BIN (element));
  }
}

/**
 * gst_validate_scenario_factory_create:
 * @runner: The #GstValidateRunner to use to report issues
 * @pipeline: The pipeline to run the scenario on
 * @scenario_name: The name (or path) of the scenario to run
 *
 * Returns: (transfer full): A #GstValidateScenario or NULL
 */
GstValidateScenario *
gst_validate_scenario_factory_create (GstValidateRunner *
    runner, GstElement * pipeline, const gchar * scenario_name)
{
  GList *config;
  GstValidateScenario *scenario =
      g_object_new (GST_TYPE_VALIDATE_SCENARIO, "validate-runner",
      runner, NULL);

  GST_LOG ("Creating scenario %s", scenario_name);
  if (!gst_validate_scenario_load (scenario, scenario_name)) {
    g_object_unref (scenario);

    return NULL;
  }

  if (scenario->priv->pipeline_name &&
      !g_pattern_match_simple (scenario->priv->pipeline_name,
          GST_OBJECT_NAME (pipeline))) {
    GST_INFO ("Scenario %s only applies on pipeline %s not %s",
        scenario_name, scenario->priv->pipeline_name,
        GST_OBJECT_NAME (pipeline));

    gst_object_unref (scenario);

    return NULL;
  }

  scenario->pipeline = pipeline;
  g_object_weak_ref (G_OBJECT (pipeline),
      (GWeakNotify) _pipeline_freed_cb, scenario);
  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (scenario),
      g_strdup (scenario_name));

  g_signal_connect (pipeline, "element-added", (GCallback) _element_added_cb,
      scenario);

  iterate_children (scenario, GST_BIN (pipeline));

  scenario->priv->bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (scenario->priv->bus);
  g_signal_connect (scenario->priv->bus, "message", (GCallback) message_cb,
      scenario);

  for (config = gst_validate_plugin_get_config (NULL); config;
      config = config->next) {
    gint interval;

    if (gst_structure_get_uint (config->data,
            "scenario-action-execution-interval",
            &scenario->priv->action_execution_interval)) {
      GST_DEBUG_OBJECT (scenario, "Setting action execution interval to %d",
          scenario->priv->action_execution_interval);
      break;
    } else if (gst_structure_get_int (config->data,
            "scenario-action-execution-interval", &interval)) {
      if (interval > 0) {
        scenario->priv->action_execution_interval = (guint) interval;
        GST_DEBUG_OBJECT (scenario, "Setting action execution interval to %d",
            scenario->priv->action_execution_interval);

        break;
      } else {
        GST_WARNING_OBJECT (scenario, "Interval is negative: %d", interval);
      }
    }
  }

  if (scenario->priv->handles_state) {
    GST_INFO_OBJECT (scenario, "Scenario handles state,"
        " Starting the get position source");
    _add_execute_actions_gsource (scenario);
  }

  gst_validate_printf (NULL,
      "\n=========================================\n"
      "Running scenario %s on pipeline %s"
      "\n=========================================\n", scenario_name,
      GST_OBJECT_NAME (pipeline));

  scenario->priv->overrides =
      gst_validate_override_registry_get_override_for_names
      (gst_validate_override_registry_get (), "scenarios", NULL);

  return scenario;
}

static gboolean
_add_description (GQuark field_id, const GValue * value, KeyFileGroupName * kfg)
{
  gchar *tmp = gst_value_serialize (value);

  g_key_file_set_string (kfg->kf, kfg->group_name,
      g_quark_to_string (field_id), g_strcompress (tmp));

  g_free (tmp);

  return TRUE;
}


static gboolean
_parse_scenario (GFile * f, GKeyFile * kf)
{
  gboolean ret = FALSE;
  gchar *fname = g_file_get_basename (f);

  if (g_str_has_suffix (fname, GST_VALIDATE_SCENARIO_SUFFIX)) {
    gboolean needs_clock_sync = FALSE;
    GstStructure *desc = NULL;

    gchar **name = g_strsplit (fname, GST_VALIDATE_SCENARIO_SUFFIX, 0);
    GList *tmp, *structures = gst_validate_structs_parse_from_gfile (f);

    for (tmp = structures; tmp; tmp = tmp->next) {
      GstValidateActionType *type =
          _find_action_type (gst_structure_get_name (tmp->data));

      if (gst_structure_has_name (tmp->data, "description"))
        desc = gst_structure_copy (tmp->data);
      else if (type && type->flags & GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK)
        needs_clock_sync = TRUE;
    }

    if (needs_clock_sync) {
      if (desc)
        gst_structure_set (desc, "need-clock-sync", G_TYPE_BOOLEAN, TRUE, NULL);
      else
        desc = gst_structure_from_string ("description, need-clock-sync=true;",
            NULL);
    }

    if (desc) {
      KeyFileGroupName kfg;

      kfg.group_name = name[0];
      kfg.kf = kf;

      gst_structure_foreach (desc,
          (GstStructureForeachFunc) _add_description, &kfg);
      gst_structure_free (desc);
    } else {
      g_key_file_set_string (kf, name[0], "noinfo", "nothing");
    }
    g_list_free_full (structures, (GDestroyNotify) gst_structure_free);
    g_strfreev (name);

    ret = TRUE;
  }

  g_free (fname);
  return ret;
}

static void
_list_scenarios_in_dir (GFile * dir, GKeyFile * kf)
{
  GFileEnumerator *fenum;
  GFileInfo *info;

  fenum = g_file_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_NAME,
      G_FILE_QUERY_INFO_NONE, NULL, NULL);

  if (fenum == NULL)
    return;

  for (info = g_file_enumerator_next_file (fenum, NULL, NULL);
      info; info = g_file_enumerator_next_file (fenum, NULL, NULL)) {
    GFile *f = g_file_enumerator_get_child (fenum, info);

    _parse_scenario (f, kf);
    gst_object_unref (f);
  }

  gst_object_unref (fenum);
}

gboolean
gst_validate_list_scenarios (gchar ** scenarios, gint num_scenarios,
    gchar * output_file)
{
  gchar *result;
  gsize datalength;

  GError *err = NULL;
  GKeyFile *kf = NULL;
  gint res = 0;
  const gchar *envvar;
  gchar **env_scenariodir = NULL;
  gchar *tldir = g_build_filename (g_get_user_data_dir (),
      "gstreamer-" GST_API_VERSION, "validate", GST_VALIDATE_SCENARIO_DIRECTORY,
      NULL);
  GFile *dir = g_file_new_for_path (tldir);

  kf = g_key_file_new ();
  if (num_scenarios > 0) {
    gint i;
    GFile *file;

    for (i = 0; i < num_scenarios; i++) {
      file = g_file_new_for_path (scenarios[i]);
      if (!_parse_scenario (file, kf)) {
        GST_ERROR ("Could not parser scenario: %s", scenarios[i]);

        gst_object_unref (file);
        res = 1;
      }
    }

    goto done;
  }

  envvar = g_getenv ("GST_VALIDATE_SCENARIOS_PATH");
  if (envvar)
    env_scenariodir = g_strsplit (envvar, ":", 0);

  _list_scenarios_in_dir (dir, kf);
  g_object_unref (dir);
  g_free (tldir);

  tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
      "validate", GST_VALIDATE_SCENARIO_DIRECTORY, NULL);
  dir = g_file_new_for_path (tldir);
  _list_scenarios_in_dir (dir, kf);
  g_object_unref (dir);
  g_free (tldir);

  if (env_scenariodir) {
    guint i;

    for (i = 0; env_scenariodir[i]; i++) {
      dir = g_file_new_for_path (env_scenariodir[i]);
      _list_scenarios_in_dir (dir, kf);
      g_object_unref (dir);
    }
  }

  /* Hack to make it work uninstalled */
  dir = g_file_new_for_path ("data/scenarios");
  _list_scenarios_in_dir (dir, kf);
  g_object_unref (dir);

done:
  result = g_key_file_to_data (kf, &datalength, &err);
  g_print ("All scenarios available:\n%s", result);

  if (output_file && !err)
    g_file_set_contents (output_file, result, datalength, &err);

  if (env_scenariodir)
    g_strfreev (env_scenariodir);

  if (err) {
    GST_WARNING ("Got error '%s' listing scenarios", err->message);
    g_clear_error (&err);

    res = FALSE;
  }

  g_key_file_free (kf);

  return res;
}

static gboolean
_action_set_done (GstValidateAction * action)
{
  if (action->scenario == NULL)
    return G_SOURCE_REMOVE;

  action->priv->execution_time = GST_CLOCK_TIME_NONE;
  action->priv->state = _execute_sub_action_action (action);
  if (action->priv->state != GST_VALIDATE_EXECUTE_ACTION_ASYNC) {
    GST_DEBUG_OBJECT (action->scenario, "Sub action executed ASYNC");

    execute_next_action (action->scenario);
  }

  return G_SOURCE_REMOVE;
}

/* gst_validate_action_set_done:
 * @action: The action that is done executing
 *
 * Sets @action as "done" meaning that the next action can
 * now be executed.
 */
void
gst_validate_action_set_done (GstValidateAction * action)
{

  if (action->priv->state == GST_VALIDATE_EXECUTE_ACTION_INTERLACED) {
    GstValidateScenario *scenario = g_weak_ref_get (&action->priv->scenario);
    GList *item = NULL;

    if (scenario) {
      SCENARIO_LOCK (scenario);
      item = g_list_find (scenario->priv->interlaced_actions, action);
      scenario->priv->interlaced_actions =
          g_list_delete_link (scenario->priv->interlaced_actions, item);
      SCENARIO_UNLOCK (scenario);
      g_object_unref (scenario);
    }

    if (item)
      gst_validate_action_unref (action);
  }

  g_main_context_invoke_full (NULL, G_PRIORITY_DEFAULT_IDLE,
      (GSourceFunc) _action_set_done,
      gst_mini_object_ref (GST_MINI_OBJECT (action)),
      (GDestroyNotify) gst_validate_action_unref);
}

/**
 * gst_validate_action_get_scenario:
 * @action: The action from which to retrieve the scenario
 *
 * Retrieve the scenario from which @action is executed.
 *
 * Returns: (transfer full): The scenario from which the action is being executed.
 */
GstValidateScenario *
gst_validate_action_get_scenario (GstValidateAction * action)
{
  return g_weak_ref_get (&action->priv->scenario);
}

/**
 * gst_validate_register_action_type:
 * @type_name: The name of the new action type to add
 * @implementer_namespace: The namespace of the implementer of the action type.
 *                         That should always be the name of the GstPlugin as
 *                         retrieved with #gst_plugin_get_name when the action type
 *                         is register inside a plugin.
 * @function: (scope notified): The function to be called to execute the action
 * @parameters: (allow-none) (array zero-terminated=1) (element-type GstValidate.ActionParameter): The #GstValidateActionParameter usable as parameter of the type
 * @description: A description of the new type
 * @flags: The #GstValidateActionTypeFlags to set on the new action type
 *
 * Register a new action type to the action type system. If the action type already
 * exists, it will be overriden by that new definition
 *
 * Returns: (transfer none): The newly created action type or the already registered action type
 * if it had a higher rank
 */
GstValidateActionType *
gst_validate_register_action_type (const gchar * type_name,
    const gchar * implementer_namespace,
    GstValidateExecuteAction function,
    GstValidateActionParameter * parameters,
    const gchar * description, GstValidateActionTypeFlags flags)
{
  GstValidateActionType *type = gst_validate_register_action_type_dynamic (NULL,
      type_name, GST_RANK_NONE, function, parameters, description,
      flags);

  g_free (type->implementer_namespace);
  type->implementer_namespace = g_strdup (implementer_namespace);

  return type;
}

static void
_free_action_types (GList * action_types)
{
  g_list_free_full (action_types, (GDestroyNotify) gst_mini_object_unref);
}

/**
 * gst_validate_register_action_type_dynamic:
 * @plugin: (allow-none): The #GstPlugin that register the action type,
 *                        or NULL for a static element.
 * @rank: The ranking of that implementation of the action type called
 *        @type_name. If an action type has been registered with the same
 *        name with a higher rank, the new implementation will not be used,
 *        and the already registered action type is returned.
 *        If the already registered implementation has a lower rank, the
 *        new implementation will be used and returned.
 * @type_name: The name of the new action type to add
 * @function: (scope notified): The function to be called to execute the action
 * @parameters: (allow-none) (array zero-terminated=1) (element-type GstValidate.ActionParameter): The #GstValidateActionParameter usable as parameter of the type
 * @description: A description of the new type
 * @flags: The #GstValidateActionTypeFlags to be set on the new action type
 *
 * Returns: (transfer none): The newly created action type or the already registered action type
 * if it had a higher rank
 */
GstValidateActionType *
gst_validate_register_action_type_dynamic (GstPlugin * plugin,
    const gchar * type_name, GstRank rank,
    GstValidateExecuteAction function, GstValidateActionParameter * parameters,
    const gchar * description, GstValidateActionTypeFlags flags)
{
  GstValidateActionType *tmptype;
  GstValidateActionType *type = gst_validate_action_type_new ();
  gboolean is_config = IS_CONFIG_ACTION_TYPE (flags);
  gint n_params = is_config ? 0 : 2;

  if (parameters) {
    for (n_params = 0; parameters[n_params].name != NULL; n_params++);
    n_params += 1;
  }

  if (n_params) {
    type->parameters = g_new0 (GstValidateActionParameter, n_params);
  }

  if (parameters) {
    memcpy (type->parameters, parameters,
        sizeof (GstValidateActionParameter) * (n_params));
  }

  type->prepare = gst_validate_action_default_prepare_func;
  type->execute = function;
  type->name = g_strdup (type_name);
  if (plugin)
    type->implementer_namespace = g_strdup (gst_plugin_get_name (plugin));
  else
    type->implementer_namespace = g_strdup ("none");

  type->description = g_strdup (description);
  type->flags = flags;
  type->rank = rank;

  if ((tmptype = _find_action_type (type_name))) {
    if (tmptype->rank <= rank) {
      action_types = g_list_remove (action_types, tmptype);
      type->overriden_type = tmptype;
    } else {
      gst_mini_object_unref (GST_MINI_OBJECT (type));

      type = tmptype;
    }
  }

  if (type != tmptype)
    action_types = g_list_append (action_types, type);

  if (plugin) {
    GList *plugin_action_types = g_object_steal_data (G_OBJECT (plugin),
        "GstValidatePluginActionTypes");

    plugin_action_types = g_list_prepend (plugin_action_types,
        gst_mini_object_ref (GST_MINI_OBJECT (type)));

    g_object_set_data_full (G_OBJECT (plugin), "GstValidatePluginActionTypes",
        plugin_action_types, (GDestroyNotify) _free_action_types);
  }

  return type;
}

GstValidateActionType *
gst_validate_get_action_type (const gchar * type_name)
{
  GstValidateActionType *type = _find_action_type (type_name);

  if (type)
    return
        GST_VALIDATE_ACTION_TYPE (gst_mini_object_ref (GST_MINI_OBJECT (type)));

  return NULL;
}

static GList *
gst_validate_list_action_types (void)
{
  return action_types;
}

/**
 * gst_validate_print_action_types:
 * @wanted_types: (array length=num_wanted_types): (optional):  List of types to be printed
 * @num_wanted_types: (optional): Length of @wanted_types
 *
 * Prints the action types details wanted in @wanted_types
 *
 * Returns: True if all types could be printed
 */
gboolean
gst_validate_print_action_types (const gchar ** wanted_types,
    gint num_wanted_types)
{
  GList *tmp;
  gint nfound = 0;

  for (tmp = gst_validate_list_action_types (); tmp; tmp = tmp->next) {
    GstValidateActionType *atype = tmp->data;
    gboolean print = FALSE;

    if (num_wanted_types) {
      gint n;

      for (n = 0; n < num_wanted_types; n++) {
        if (g_strcmp0 (atype->name, wanted_types[n]) == 0 ||
            g_strcmp0 (atype->implementer_namespace, wanted_types[n]) == 0) {
          nfound++;
          print = TRUE;

          break;
        }
      }
    } else {
      print = TRUE;
    }

    if (print && num_wanted_types) {
      gst_validate_printf (tmp->data, "\n");
    } else if (print) {
      gchar *desc =
          g_regex_replace (newline_regex, atype->description, -1, 0, "\n      ",
          0,
          NULL);

      gst_validate_printf (NULL, "\n%s: %s:\n      %s\n",
          atype->implementer_namespace, atype->name, desc);
      g_free (desc);
    }
  }

  if (num_wanted_types && num_wanted_types > nfound) {
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_validate_scenario_get_actions:
 * @scenario: The scenario to retrieve remaining actions for
 *
 * Get remaining actions from @scenario.
 *
 * Returns: (transfer full) (element-type GstValidateAction): A list of #GstValidateAction.
 */
GList *
gst_validate_scenario_get_actions (GstValidateScenario * scenario)
{
  GList *ret;
  gboolean main_context_acquired;

  main_context_acquired = g_main_context_acquire (g_main_context_default ());
  g_return_val_if_fail (main_context_acquired, NULL);

  ret = g_list_copy_deep (scenario->priv->actions,
      (GCopyFunc) gst_mini_object_ref, NULL);

  g_main_context_release (g_main_context_default ());

  return ret;
}

/**
 * gst_validate_scenario_get_target_state:
 * @scenario: The scenario to retrieve the current target state for
 *
 * Get current target state from @scenario.
 *
 * Returns: Current target state.
 */
GstState
gst_validate_scenario_get_target_state (GstValidateScenario * scenario)
{
  return scenario->priv->target_state;
}

void
init_scenarios (void)
{
  GST_DEBUG_CATEGORY_INIT (gst_validate_scenario_debug, "gstvalidatescenario",
      GST_DEBUG_FG_YELLOW, "Gst validate scenarios");

  _gst_validate_action_type = gst_validate_action_get_type ();
  _gst_validate_action_type_type = gst_validate_action_type_get_type ();

  /*  *INDENT-OFF* */
  REGISTER_ACTION_TYPE ("description", NULL,
      ((GstValidateActionParameter [])  {
      {
        .name = "summary",
        .description = "Whether the scenario is a config only scenario (ie. explain what it does)",
        .mandatory = FALSE,
        .types = "sting",
        .possible_variables = NULL,
        .def = "'Nothing'"},
      {
        .name = "is-config",
        .description = "Whether the scenario is a config only scenario",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {
        .name = "handles-states",
        .description = "Whether the scenario handles pipeline state changes from the beginning\n"
        "in that case the application should not set the state of the pipeline to anything\n"
        "and the scenario action will be executed from the beginning",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"},
      {
        .name = "seek",
        .description = "Whether the scenario executes seek action or not",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {
        .name = "reverse-playback",
        .description = "Whether the scenario plays the stream backward",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {
        .name = "need-clock-sync",
        .description = "Whether the scenario needs the execution to be syncronized with the pipeline\n"
                       "clock. Letting the user know if it can be used with a 'fakesink sync=false' sink",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {
        .name = "min-media-duration",
        .description = "Lets the user know the minimum duration of the stream for the scenario\n"
                       "to be usable",
        .mandatory = FALSE,
        .types = "double",
        .possible_variables = NULL,
        .def = "0.0"
      },
      {
        .name = "min-audio-track",
        .description = "Lets the user know the minimum number of audio tracks the stream needs to contain\n"
                       "for the scenario to be usable",
        .mandatory = FALSE,
        .types = "int",
        .possible_variables = NULL,
        .def = "0"
      },
      {
       .name = "min-video-track",
       .description = "Lets the user know the minimum number of video tracks the stream needs to contain\n"
                      "for the scenario to be usable",
       .mandatory = FALSE,
       .types = "int",
       .possible_variables = NULL,
       .def = "0"
      },
      {
        .name = "duration",
        .description = "Lets the user know the time the scenario needs to be fully executed",
        .mandatory = FALSE,
        .types = "double, int",
        .possible_variables = NULL,
        .def = "infinite (GST_CLOCK_TIME_NONE)"
      },
      {
        .name = "pipeline-name",
        .description = "The name of the GstPipeline on which the scenario should be executed.\n"
          "It has the same effect as setting the pipeline using pipeline_name->scenario_name.",
        .mandatory = FALSE,
        .types = "string",
        .possible_variables = NULL,
        .def = "NULL"
      },
      {NULL}
      }),
      "Allows to describe the scenario in various ways",
      GST_VALIDATE_ACTION_TYPE_CONFIG);

  REGISTER_ACTION_TYPE ("seek", _execute_seek,
      ((GstValidateActionParameter [])  {
        {
          .name = "start",
          .description = "The starting value of the seek",
          .mandatory = TRUE,
          .types = "double or string",
          .possible_variables = "position: The current position in the stream\n"
            "duration: The duration of the stream",
           NULL
        },
        {
          .name = "flags",
          .description = "The GstSeekFlags to use",
          .mandatory = TRUE,
          .types = "string describing the GstSeekFlags to set",
          NULL,
        },
        {
          .name = "rate",
          .description = "The rate value of the seek",
          .mandatory = FALSE,
          .types = "double",
          .possible_variables = NULL,
          .def = "1.0"
        },
        {
          .name = "start_type",
          .description = "The GstSeekType to use for the start of the seek, in:\n"
          "  [none, set, end]",
          .mandatory = FALSE,
          .types = "string",
        .possible_variables = NULL,
          .def = "set"
        },
        {
          .name = "stop_type",
          .description = "The GstSeekType to use for the stop of the seek, in:\n"
                         "  [none, set, end]",
          .mandatory = FALSE,
          .types = "string",
          .possible_variables = NULL,
          .def = "set"
        },
        {"stop", "The stop value of the seek", FALSE, "double or ",
          "position: The current position in the stream\n"
            "duration: The duration of the stream"
            "GST_CLOCK_TIME_NONE",
        },
        {NULL}
      }),
      "Seeks into the stream, example of a seek happening when the stream reaches 5 seconds\n"
      "or 1 eighth of its duration and seeks at 10sec or 2 eighth of its duration:\n"
      "  seek, playback-time=\"min(5.0, (duration/8))\", start=\"min(10, 2*(duration/8))\", flags=accurate+flush",
      GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK
  );

  REGISTER_ACTION_TYPE ("pause", _execute_pause,
      ((GstValidateActionParameter []) {
        {
          .name = "duration",
          .description = "The duration during which the stream will be paused",
          .mandatory = FALSE,
          .types = "double",
          .possible_variables = NULL,
          .def = "0.0",
        },
        {NULL}
      }),
      "Sets pipeline to PAUSED. You can add a 'duration'\n"
      "parametter so the pipeline goes back to playing after that duration\n"
      "(in second)",
      GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK & GST_VALIDATE_ACTION_TYPE_ASYNC);

  REGISTER_ACTION_TYPE ("play", _execute_play, NULL,
      "Sets the pipeline state to PLAYING", GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("stop", _execute_stop, NULL,
      "Stops the execution of the scenario. It will post a 'request-state'"
      " message on the bus with NULL as a requested state "
      " and the application is responsible for stopping itself."
      " if you override that action type, make sure to link up.",
      GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL);

  REGISTER_ACTION_TYPE ("eos", _execute_eos, NULL,
      "Sends an EOS event to the pipeline",
      GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL);

  REGISTER_ACTION_TYPE ("switch-track", _execute_switch_track,
      ((GstValidateActionParameter []) {
        {
          .name = "type",
          .description = "Selects which track type to change (can be 'audio', 'video',"
                          " or 'text').",
          .mandatory = FALSE,
          .types = "string",
          .possible_variables = NULL,
          .def = "audio",
        },
        {
          .name = "index",
          .description = "Selects which track of this type to use: it can be either a number,\n"
                         "which will be the Nth track of the given type, or a number with a '+' or\n"
                         "'-' prefix, which means a relative change (eg, '+1' means 'next track',\n"
                         "'-1' means 'previous track')",
          .mandatory = FALSE,
          .types = "string: to switch track relatively\n"
                   "int: To use the actual index to use",
          .possible_variables = NULL,
          .def = "+1",
        },
        {NULL}
      }),
      "The 'switch-track' command can be used to switch tracks.\n"
      , GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("wait", _execute_wait,
      ((GstValidateActionParameter []) {
        {
          .name = "duration",
          .description = "the duration while no other action will be executed",
          .mandatory = FALSE,
          NULL},
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to wait @signal-name on.",
          .mandatory = FALSE,
          .types = "string"
        },
        {
          .name = "signal-name",
          .description = "The name of the signal to wait for on @target-element-name",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "message-type",
          .description = "The name of the message type to wait for (on @target-element-name"
            " if specified)",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {NULL}
      }),
      "Waits for signal 'signal-name', message 'message-type', or during 'duration' seconds",
      GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE);

  REGISTER_ACTION_TYPE ("dot-pipeline", _execute_dot_pipeline, NULL,
      "Dots the pipeline (the 'name' property will be used in the dot filename).\n"
      "For more information have a look at the GST_DEBUG_BIN_TO_DOT_FILE documentation.\n"
      "Note that the GST_DEBUG_DUMP_DOT_DIR env variable needs to be set\n",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("set-feature-rank", _set_rank,
      ((GstValidateActionParameter []) {
        {
          .name = "feature-name",
          .description = "The name of a GstFeature",
          .mandatory = TRUE,
          .types = "string",
          NULL},
        {
          .name = "rank",
          .description = "The GstRank to set on @feature-name",
          .mandatory = TRUE,
          .types = "string, int",
          NULL},
        {NULL}
      }),
      "Changes the ranking of a particular plugin feature",
      GST_VALIDATE_ACTION_TYPE_CONFIG);

  REGISTER_ACTION_TYPE ("set-state", _execute_set_state,
      ((GstValidateActionParameter []) {
        {
          .name = "state",
          .description = "A GstState as a string, should be in: \n"
                         "    * ['null', 'ready', 'paused', 'playing']",
          .mandatory = TRUE,
          .types = "string",
        },
        {NULL}
      }),
      "Changes the state of the pipeline to any GstState",
      GST_VALIDATE_ACTION_TYPE_ASYNC & GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK);

  REGISTER_ACTION_TYPE ("set-property", _execute_set_property,
      ((GstValidateActionParameter []) {
        /* Either 'target-element-name' or 'target-element-klass' needs to be
         * defined */
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to set a property on",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-klass",
          .description = "The klass of the GstElements to set a property on",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "property-name",
          .description = "The name of the property to set on @target-element-name",
          .mandatory = TRUE,
          .types = "string",
          NULL
        },
        {
          .name = "property-value",
          .description = "The value of @property-name to be set on the element",
          .mandatory = TRUE,
          .types = "The same type of @property-name",
          NULL
        },
        {NULL}
      }),
      "Sets a property of any element in the pipeline",
      GST_VALIDATE_ACTION_TYPE_CAN_EXECUTE_ON_ADDITION |
          GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL);

  REGISTER_ACTION_TYPE ("set-debug-threshold",
      _execute_set_debug_threshold,
      ((GstValidateActionParameter [])
        {
          {
            .name = "debug-threshold",
            .description = "String defining debug threshold\n"
                           "See gst_debug_set_threshold_from_string",
            .mandatory = TRUE,
            .types = "string"},
          {NULL}
        }),
      "Sets the debug level to be used, same format as\n"
      "setting the GST_DEBUG env variable",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("emit-signal", _execute_emit_signal,
      ((GstValidateActionParameter [])
      {
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to emit a signal on",
          .mandatory = TRUE,
          .types = "string"
        },
        {
          .name = "signal-name",
          .description = "The name of the signal to emit on @target-element-name",
          .mandatory = TRUE,
          .types = "string",
          NULL
        },
        {NULL}
      }),
      "Emits a signal to an element in the pipeline",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("disable-plugin", _execute_disable_plugin,
      ((GstValidateActionParameter [])
      {
        {
          .name = "plugin-name",
          .description = "The name of the GstPlugin to disable",
          .mandatory = TRUE,
          .types = "string"
        },
        {
          .name = "as-config",
          .description = "Execute action as a config action (meaning when loading the scenario)",
          .mandatory = FALSE,
          .types = "boolean",
          .def = "false"
        },
        {NULL}
      }),
      "Disables a GstPlugin",
      GST_VALIDATE_ACTION_TYPE_NONE);
  /*  *INDENT-ON* */

}

void
gst_validate_scenario_deinit (void)
{
  _free_action_types (action_types);
  action_types = NULL;
}
