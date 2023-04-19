/* GStreamer

 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2018-2022 Igalia S.L
 *  Author: Thibault Saunier <tsaunier@igalia.com>

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
 * @title: GstValidateScenario
 * @short_description: A GstValidateScenario represents a set of actions to be executed on a pipeline.
 *
 * A #GstValidateScenario represents the scenario that will be executed on a #GstPipeline.
 * It is basically an ordered list of #GstValidateAction that will be executed during the
 * execution of the pipeline.
 *
 * Possible configurations (see [GST_VALIDATE_CONFIG](gst-validate-environment-variables.md)):
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
#include <math.h>

#include <gst/check/gsttestclock.h>
#include "gst-validate-internal.h"
#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"
#include "gst-validate-utils.h"
#include "gst-validate-internal.h"
#include "validate.h"
#include <gst/controller/controller.h>
#include <gst/validate/gst-validate-override.h>
#include <gst/validate/gst-validate-override-registry.h>
#include <gst/validate/gst-validate-pipeline-monitor.h>

#define GST_VALIDATE_SCENARIO_DIRECTORY "scenarios"

#define DEFAULT_SEEK_TOLERANCE (1 * GST_MSECOND)        /* tolerance seek interval
                                                           TODO make it overridable  */

GST_DEBUG_CATEGORY_STATIC (gst_validate_scenario_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_scenario_debug

#define REGISTER_ACTION_TYPE(_tname, _function, _params, _desc, _is_config) G_STMT_START { \
  type = gst_validate_register_action_type ((_tname), "core", (_function), (_params), (_desc), (_is_config)); \
} G_STMT_END

#define ACTION_EXPECTED_STREAM_QUARK g_quark_from_static_string ("ACTION_EXPECTED_STREAM_QUARK")

#define SCENARIO_LOCK(scenario) G_STMT_START {				\
    GST_LOG_OBJECT (scenario, "About to lock %p", &scenario->priv->lock); \
    g_mutex_lock(&scenario->priv->lock);				\
    GST_LOG_OBJECT (scenario, "Acquired lock %p", &scenario->priv->lock); \
  } G_STMT_END

#define SCENARIO_UNLOCK(scenario) G_STMT_START {			\
    GST_LOG_OBJECT (scenario, "About to unlock %p", &scenario->priv->lock); \
    g_mutex_unlock(&scenario->priv->lock);				\
    GST_LOG_OBJECT (scenario, "unlocked %p", &scenario->priv->lock);	\
  } G_STMT_END

#define DECLARE_AND_GET_PIPELINE(s,a) \
  GstElement * pipeline = gst_validate_scenario_get_pipeline (s); \
  if (pipeline == NULL) { \
    GST_VALIDATE_REPORT_ACTION (s, a, SCENARIO_ACTION_EXECUTION_ERROR, \
            "Can't execute a '%s' action after the pipeline " \
            "has been destroyed.", a->type); \
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED; \
  }

#ifdef G_HAVE_ISO_VARARGS
#define REPORT_UNLESS(condition, errpoint, ...)                                \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR,              \
                                 __VA_ARGS__);                                 \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#elif defined(G_HAVE_GNUC_VARARGS)
#define REPORT_UNLESS(condition, errpoint, args...)                            \
  G_STMT_START {                                                               \
    if (!(condition)) {                                                        \
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;                        \
      gst_validate_report_action(GST_VALIDATE_REPORTER(scenario), action,      \
                                 SCENARIO_ACTION_EXECUTION_ERROR, ##args);     \
      goto errpoint;                                                           \
    }                                                                          \
  }                                                                            \
  G_STMT_END
#endif

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
  ACTION_DONE,
  LAST_SIGNAL
};

static guint scenario_signals[LAST_SIGNAL] = { 0 };

static GList *action_types = NULL;
static void gst_validate_scenario_dispose (GObject * object);
static void gst_validate_scenario_finalize (GObject * object);
static GstValidateActionType *_find_action_type (const gchar * type_name);
static GstValidateExecuteActionReturn
_fill_action (GstValidateScenario * scenario, GstValidateAction * action,
    GstStructure * structure, gboolean add_to_lists);
static gboolean _action_set_done (GstValidateAction * action);
static GList *_find_elements_defined_in_action (GstValidateScenario * scenario,
    GstValidateAction * action);
static GstValidateAction *gst_validate_create_subaction (GstValidateScenario *
    scenario, GstStructure * lvariables, GstValidateAction * action,
    GstStructure * nstruct, gint it, gint max);

/* GstValidateSinkInformation tracks information for all sinks in the pipeline */
typedef struct
{
  GstElement *sink;             /* The sink element tracked */
  guint32 segment_seqnum;       /* The latest segment seqnum. GST_SEQNUM_INVALID if none */
  GstSegment segment;           /* The latest segment */
} GstValidateSinkInformation;

/* GstValidateSeekInformation tracks:
 * * The values used in the seek
 * * The seqnum used in the seek event
 * * The validate action to which it relates
 */
typedef struct
{
  guint32 seqnum;               /* seqnum of the seek event */

  /* Seek values */
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;

  /* The action corresponding to this seek */
  GstValidateAction *action;
} GstValidateSeekInformation;

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
  GList *non_blocking_running_actions;  /* MT safe. Protected with SCENARIO_LOCK */
  GList *on_addition_actions;   /* MT safe. Protected with SCENARIO_LOCK */

  gboolean needs_playback_parsing;

  GList *sinks;                 /* List of GstValidateSinkInformation */
  GList *seeks;                 /* List of GstValidateSeekInformation */

  /* Seek currently applied (set when all sinks received segment with
   * an identical seqnum and there is a matching pending seek).
   * do not free, should always be present in the seek list above */
  GstValidateSeekInformation *current_seek;
  /* Current unified seqnum. Set when all sinks received segment with
   * an identical seqnum, even if there wasn't a matching pending seek
   */
  guint32 current_seqnum;

  /*  List of action that need parsing when reaching ASYNC_DONE
   *  most probably to be able to query duration */

  /* seek_flags :
   *  * Only set for seek actions, and only if seek succeeded
   *  * Only Used in _check_position()
   * FIXME : Just use the seek information */
  GstSeekFlags seek_flags;
  GstFormat seek_format;

  /* segment_start/segment_stop :
   *  * Set : from seek values
   *  * Read : In _check_position()
   * FIXME : Just use the current seek information */
  GstClockTime segment_start;
  GstClockTime segment_stop;

  /* Always initialized to a default value
   * FIXME : Is it still needed with the new seeking validation system ? */
  GstClockTime seek_pos_tol;

  /* If we seeked in paused the position should be exactly what
   * the seek value was (if accurate) */
  gboolean seeked_in_pause;

  guint num_actions;

  gboolean handles_state;

  guint execute_actions_source_id;      /* MT safe. Protect with SCENARIO_LOCK */
  guint wait_id;
  guint signal_handler_id;      /* MT safe. Protect with SCENARIO_LOCK */
  guint action_execution_interval;

  /* Name of message the wait action is waiting for */
  GstValidateAction *wait_message_action;

  gboolean buffering;

  gboolean got_eos;
  gboolean changing_state;
  gboolean needs_async_done;
  gboolean ignore_eos;
  gboolean allow_errors;
  GstState target_state;

  GList *overrides;

  gchar *pipeline_name;
  GstClockTime max_latency;
  gint dropped;
  gint max_dropped;

  /* 'switch-track action' currently waiting for
   * GST_MESSAGE_STREAMS_SELECTED to be completed. */
  GstValidateAction *pending_switch_track;

  GstStructure *vars;

  GWeakRef ref_pipeline;

  GstTestClock *clock;
  guint segments_needed;

  GMainContext *context;
};

typedef struct KeyFileGroupName
{
  GKeyFile *kf;
  gchar *group_name;
} KeyFileGroupName;

#define NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE 1

static void
gst_validate_sink_information_free (GstValidateSinkInformation * info)
{
  gst_object_unref (info->sink);
  g_free (info);
}

static void
gst_validate_seek_information_free (GstValidateSeekInformation * info)
{
  gst_validate_action_unref (info->action);
  g_free (info);
}

static GstValidateInterceptionReturn
gst_validate_scenario_intercept_report (GstValidateReporter * reporter,
    GstValidateReport * report)
{
  GList *tmp;

  for (tmp = GST_VALIDATE_SCENARIO (reporter)->priv->overrides; tmp;
      tmp = tmp->next) {
    GstValidateOverride *override = (GstValidateOverride *) tmp->data;
    report->level =
        gst_validate_override_get_severity (override,
        gst_validate_issue_get_id (report->issue), report->level);
  }

  return GST_VALIDATE_REPORTER_REPORT;
}

/**
 * gst_validate_scenario_get_pipeline:
 * @scenario: The scenario to retrieve a pipeline from
 *
 * Returns: (transfer full) (nullable): The #GstPipeline the scenario is running
 * against
 */
GstElement *
gst_validate_scenario_get_pipeline (GstValidateScenario * scenario)
{
  return g_weak_ref_get (&scenario->priv->ref_pipeline);
}

static GstPipeline *
_get_pipeline (GstValidateReporter * reporter)
{
  return
      GST_PIPELINE_CAST (gst_validate_scenario_get_pipeline
      (GST_VALIDATE_SCENARIO (reporter)));
}

static void
_reporter_iface_init (GstValidateReporterInterface * iface)
{
  iface->intercept_report = gst_validate_scenario_intercept_report;
  iface->get_pipeline = _get_pipeline;
}

static GQuark chain_qdata;
G_DEFINE_TYPE_WITH_CODE (GstValidateScenario, gst_validate_scenario,
    GST_TYPE_OBJECT, G_ADD_PRIVATE (GstValidateScenario)
    G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, _reporter_iface_init)
    chain_qdata = g_quark_from_static_string ("__validate_scenario_chain_data")
    );

/* GstValidateAction implementation */
static GType _gst_validate_action_type = 0;

struct _GstValidateActionPrivate
{
  GstStructure *main_structure;
  GstValidateExecuteActionReturn state; /* Actually ActionState */
  gboolean printed;
  gboolean executing_last_subaction;
  gboolean subaction_level;
  gboolean optional;

  GstClockTime execution_time;
  GstClockTime execution_duration;
  GstClockTime timeout;

  GWeakRef scenario;
  gboolean needs_playback_parsing;
  gboolean pending_set_done;

  GMainContext *context;

  GValue it_value;
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
  json_object_unref (jreport);

  return node;
}

GType
gst_validate_action_get_type (void)
{
  if (_gst_validate_action_type == 0) {
    _gst_validate_action_type =
        g_boxed_type_register_static (g_intern_static_string
        ("GstValidateAction"), (GBoxedCopyFunc) gst_validate_action_ref,
        (GBoxedFreeFunc) gst_validate_action_unref);

    json_boxed_register_serialize_func (_gst_validate_action_type,
        JSON_NODE_OBJECT,
        (JsonBoxedSerializeFunc) gst_validate_action_serialize);
  }

  return _gst_validate_action_type;
}

static gboolean execute_next_action (GstValidateScenario * scenario);
static gboolean
gst_validate_scenario_load (GstValidateScenario * scenario,
    const gchar * scenario_name);

static GstValidateAction *
_action_copy (GstValidateAction * act)
{
  GstValidateScenario *scenario = gst_validate_action_get_scenario (act);
  GstValidateAction *copy = gst_validate_action_new (scenario,
      _find_action_type (act->type), NULL, FALSE);

  gst_object_unref (scenario);

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
  GST_VALIDATE_ACTION_LINENO (copy) = GST_VALIDATE_ACTION_LINENO (act);
  GST_VALIDATE_ACTION_FILENAME (copy) =
      g_strdup (GST_VALIDATE_ACTION_FILENAME (act));
  GST_VALIDATE_ACTION_DEBUG (copy) = g_strdup (GST_VALIDATE_ACTION_DEBUG (act));
  GST_VALIDATE_ACTION_N_REPEATS (copy) = GST_VALIDATE_ACTION_N_REPEATS (act);
  GST_VALIDATE_ACTION_RANGE_NAME (copy) = GST_VALIDATE_ACTION_RANGE_NAME (act);

  if (act->priv->it_value.g_type != 0) {
    g_value_init (&copy->priv->it_value, G_VALUE_TYPE (&act->priv->it_value));
    g_value_copy (&act->priv->it_value, &copy->priv->it_value);
  }

  return copy;
}

const gchar *
gst_validate_action_return_get_name (GstValidateActionReturn r)
{
  switch (r) {
    case GST_VALIDATE_EXECUTE_ACTION_ERROR:
      return "ERROR";
    case GST_VALIDATE_EXECUTE_ACTION_OK:
      return "OK";
    case GST_VALIDATE_EXECUTE_ACTION_ASYNC:
      return "ASYNC";
    case GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING:
      return "NON-BLOCKING";
    case GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED:
      return "ERROR(reported)";
    case GST_VALIDATE_EXECUTE_ACTION_IN_PROGRESS:
      return "IN_PROGRESS";
    case GST_VALIDATE_EXECUTE_ACTION_NONE:
      return "NONE";
    case GST_VALIDATE_EXECUTE_ACTION_DONE:
      return "DONE";
  }
  g_assert_not_reached ();
  return "???";
}

static void
_action_free (GstValidateAction * action)
{
  if (action->structure)
    gst_structure_free (action->structure);

  if (action->priv->main_structure)
    gst_structure_free (action->priv->main_structure);

  if (action->priv->it_value.g_type != 0)
    g_value_reset (&action->priv->it_value);
  g_weak_ref_clear (&action->priv->scenario);
  g_free (GST_VALIDATE_ACTION_FILENAME (action));
  g_free (GST_VALIDATE_ACTION_DEBUG (action));

  g_free (action->priv);
  g_free (action);
}

static void
gst_validate_action_init (GstValidateAction * action)
{
  gst_mini_object_init (((GstMiniObject *) action), 0,
      _gst_validate_action_type, (GstMiniObjectCopyFunction) _action_copy, NULL,
      (GstMiniObjectFreeFunction) _action_free);

  action->priv = g_new0 (GstValidateActionPrivate, 1);

  g_weak_ref_init (&action->priv->scenario, NULL);
}

GstValidateAction *
gst_validate_action_ref (GstValidateAction * action)
{
  return (GstValidateAction *) gst_mini_object_ref (GST_MINI_OBJECT (action));
}

void
gst_validate_action_unref (GstValidateAction * action)
{
  gst_mini_object_unref (GST_MINI_OBJECT (action));
}

/**
 * gst_validate_action_new:
 * @scenario: (allow-none): The scenario executing the action
 * @action_type: The action type
 * @structure: The structure containing the action arguments
 * @add_to_lists: Weather the action should be added to the scenario action list
 *
 * Returns: A newly created #GstValidateAction
 */
GstValidateAction *
gst_validate_action_new (GstValidateScenario * scenario,
    GstValidateActionType * action_type, GstStructure * structure,
    gboolean add_to_lists)
{
  GstValidateAction *action = g_new0 (GstValidateAction, 1);

  g_assert (action_type);

  gst_validate_action_init (action);
  action->playback_time = GST_CLOCK_TIME_NONE;
  action->priv->timeout = GST_CLOCK_TIME_NONE;
  action->priv->state = GST_VALIDATE_EXECUTE_ACTION_NONE;
  action->type = action_type->name;
  action->repeat = -1;

  g_weak_ref_set (&action->priv->scenario, scenario);
  if (structure) {
    gchar *filename = NULL;
    gst_structure_get (structure,
        "__lineno__", G_TYPE_INT, &GST_VALIDATE_ACTION_LINENO (action),
        "__filename__", G_TYPE_STRING, &filename,
        "__debug__", G_TYPE_STRING, &GST_VALIDATE_ACTION_DEBUG (action), NULL);
    if (filename) {
      GST_VALIDATE_ACTION_FILENAME (action) =
          g_filename_display_basename (filename);
      g_free (filename);
    }
    gst_structure_remove_fields (structure, "__lineno__", "__filename__",
        "__debug__", NULL);
    action->priv->state =
        _fill_action (scenario, action, structure, add_to_lists);
  }

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

gint
gst_validate_action_get_level (GstValidateAction * action)
{
  return action->priv->subaction_level;
}

/* GstValidateActionType implementation */
GType _gst_validate_action_type_type;
GST_DEFINE_MINI_OBJECT_TYPE (GstValidateActionType, gst_validate_action_type);
static GstValidateActionType *gst_validate_action_type_new (void);

struct _GstValidateActionTypePrivate
{
  gint n_calls;
};

static void
_action_type_free (GstValidateActionType * type)
{
  for (gint i = 0; type->parameters[i].name; i++) {
    if (type->parameters[i].free) {
      type->parameters[i].free (&type->parameters[i]);
    }
  }

  g_free (type->parameters);
  g_free (type->description);
  g_free (type->name);
  g_free (type->implementer_namespace);
  g_free (type->priv);

  if (type->overriden_type)
    gst_mini_object_unref (GST_MINI_OBJECT (type->overriden_type));

  g_free (type);
}

static void
gst_validate_action_type_init (GstValidateActionType * type)
{
  type->priv = g_new0 (GstValidateActionTypePrivate, 1);

  gst_mini_object_init ((GstMiniObject *) type, 0,
      _gst_validate_action_type_type, NULL, NULL,
      (GstMiniObjectFreeFunction) _action_type_free);
}

GstValidateActionType *
gst_validate_action_type_new (void)
{
  GstValidateActionType *type = g_new0 (GstValidateActionType, 1);

  gst_validate_action_type_init (type);

  return type;
}

static GstValidateActionType *
_find_action_type (const gchar * type_name)
{
  GList *tmp;

  for (tmp = action_types; tmp; tmp = tmp->next) {
    GstValidateActionType *atype = (GstValidateActionType *) tmp->data;
    if (g_strcmp0 (atype->name, type_name) == 0)
      return atype;
  }

  return NULL;
}

static void
_update_well_known_vars (GstValidateScenario * scenario)
{
  gint64 duration, position;
  gdouble dduration, dposition;
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  gst_structure_remove_fields (scenario->priv->vars, "position", "duration",
      NULL);

  if (!pipeline)
    return;

  if (!gst_element_query_duration (pipeline, GST_FORMAT_TIME, &duration) ||
      !GST_CLOCK_TIME_IS_VALID (duration)) {
    GstValidateMonitor *monitor =
        (GstValidateMonitor *) (g_object_get_data ((GObject *)
            pipeline, "validate-monitor"));
    GST_INFO_OBJECT (scenario,
        "Could not query duration. Trying to get duration from media-info");
    if (monitor && monitor->media_descriptor)
      duration =
          gst_validate_media_descriptor_get_duration
          (monitor->media_descriptor);
  }

  if (!GST_CLOCK_TIME_IS_VALID (duration))
    dduration = G_MAXDOUBLE;
  else
    dduration = ((double) duration / GST_SECOND);

  gst_structure_set (scenario->priv->vars, "duration", G_TYPE_DOUBLE, dduration,
      NULL);
  if (gst_element_query_position (pipeline, GST_FORMAT_TIME, &position)) {

    if (!GST_CLOCK_TIME_IS_VALID (position))
      dposition = G_MAXDOUBLE;
    else
      dposition = ((double) position / GST_SECOND);

    gst_structure_set (scenario->priv->vars, "position", G_TYPE_DOUBLE,
        dposition, NULL);
  } else {
    GST_INFO_OBJECT (scenario, "Could not query position");
  }
}

static GstElement *_get_target_element (GstValidateScenario * scenario,
    GstValidateAction * action);

static GstObject *
_get_target_object_property (GstValidateScenario * scenario,
    GstValidateAction * action, const gchar * property_path,
    GParamSpec ** pspec)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gchar **elem_pad_name = NULL;
  gchar **object_prop_name = NULL;
  const gchar *elemname;
  const gchar *padname = NULL;
  GstObject *target = NULL;
  gint i;

  elem_pad_name = g_strsplit (property_path, ".", 2);
  object_prop_name =
      g_strsplit (elem_pad_name[1] ? elem_pad_name[1] : elem_pad_name[0], "::",
      -1);
  REPORT_UNLESS (object_prop_name[1], err,
      "Property specification %s is missing a `::propename` part",
      property_path);

  if (elem_pad_name[1]) {
    elemname = elem_pad_name[0];
    padname = object_prop_name[0];
  } else {
    elemname = object_prop_name[0];
  }

  gst_structure_set (action->structure, "target-element-name", G_TYPE_STRING,
      elemname, NULL);

  target = (GstObject *) _get_target_element (scenario, action);
  gst_structure_remove_field (action->structure, "target-element-name");
  REPORT_UNLESS (target, err, "Target element with given name (%s) not found",
      elemname);

  if (padname) {
    gboolean done = FALSE;
    GstIterator *it = gst_element_iterate_pads (GST_ELEMENT (target));
    GValue v = G_VALUE_INIT;

    gst_clear_object (&target);
    while (!done) {
      switch (gst_iterator_next (it, &v)) {
        case GST_ITERATOR_OK:{
          GstPad *pad = g_value_get_object (&v);
          gchar *name = gst_object_get_name (GST_OBJECT (pad));

          if (!g_strcmp0 (name, padname)) {
            done = TRUE;
            gst_clear_object (&target);

            target = gst_object_ref (pad);
          }
          g_free (name);
          g_value_reset (&v);
          break;
        }
        case GST_ITERATOR_RESYNC:
          gst_iterator_resync (it);
          break;
        case GST_ITERATOR_ERROR:
        case GST_ITERATOR_DONE:
          done = TRUE;
      }
    }

    gst_iterator_free (it);
  }
  REPORT_UNLESS (target, err, "Could not find pad: %s::%s", elemname, padname);

  for (i = 1;;) {
    const gchar *propname = object_prop_name[i];

    *pspec =
        g_object_class_find_property (G_OBJECT_GET_CLASS (target), propname);

    REPORT_UNLESS (*pspec, err,
        "Object %" GST_PTR_FORMAT " doesn't have a property call '%s'", target,
        propname);

    if (!object_prop_name[++i])
      break;

    REPORT_UNLESS (g_type_is_a ((*pspec)->owner_type, G_TYPE_OBJECT), err,
        "Property: %" GST_PTR_FORMAT "::%s not a GObject, can't use it.",
        target, propname);

    g_object_get (target, propname, &target, NULL);
    REPORT_UNLESS (target, err,
        "Property: %" GST_PTR_FORMAT "::%s is NULL can't get %s.",
        target, propname, object_prop_name[i + 1]);
  }

  REPORT_UNLESS (res == GST_VALIDATE_EXECUTE_ACTION_OK, err, "Something fishy");

done:
  g_strfreev (elem_pad_name);
  g_strfreev (object_prop_name);
  return target;

err:
  gst_clear_object (&target);
  goto done;
}

static gboolean
_set_variable_func (const gchar * name, double *value, gpointer user_data)
{
  GstValidateScenario *scenario = (GstValidateScenario *) user_data;

  if (!gst_structure_get_double (scenario->priv->vars, name, value))
    return FALSE;

  return TRUE;
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
      actions_list_is_done (scenario->priv->non_blocking_running_actions) &&
      actions_list_is_done (scenario->priv->on_addition_actions)) {
    SCENARIO_UNLOCK (scenario);

    g_signal_emit (scenario, scenario_signals[DONE], 0);
  } else {
    SCENARIO_UNLOCK (scenario);
  }
}

static void
_reset_sink_information (GstValidateSinkInformation * sinkinfo)
{
  sinkinfo->segment_seqnum = GST_SEQNUM_INVALID;
  gst_segment_init (&sinkinfo->segment, GST_FORMAT_UNDEFINED);
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
 * Get a time value for the @name parameter of an action. This
 * method should be called to retrieve and compute a timed value of a given
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

  if (!gst_structure_has_field (action->structure, name))
    return FALSE;

  if (!gst_validate_utils_get_clocktime (action->structure, name, retval)) {
    gdouble val;
    gchar *error = NULL, *strval;
    const gchar *tmpvalue = gst_structure_get_string (action->structure, name);

    if (!tmpvalue) {
      GST_INFO_OBJECT (scenario, "Could not find %s (%" GST_PTR_FORMAT ")",
          name, action->structure);
      return -1;
    }

    _update_well_known_vars (scenario);
    strval =
        gst_validate_replace_variables_in_string (action, scenario->priv->vars,
        tmpvalue, GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_ALL);
    if (!strval)
      return FALSE;

    val =
        gst_validate_utils_parse_expression (strval, _set_variable_func,
        scenario, &error);
    if (error) {
      GST_WARNING ("Error while parsing %s: %s (%" GST_PTR_FORMAT ")",
          strval, error, scenario->priv->vars);
      g_free (error);
      g_free (strval);

      return FALSE;
    } else if (val == -1.0) {
      *retval = GST_CLOCK_TIME_NONE;
    } else {
      gint n, d;

      gst_util_double_to_fraction (val, &n, &d);
      *retval = gst_util_uint64_scale_int_round (n, GST_SECOND, d);
    }
    gst_structure_set (action->structure, name, G_TYPE_UINT64, *retval, NULL);
    g_free (strval);

    return TRUE;
  }

  return TRUE;
}

/* WITH SCENARIO LOCK TAKEN */
static GstValidateSinkInformation *
_find_sink_information (GstValidateScenario * scenario, GstElement * sink)
{
  GList *tmp;

  for (tmp = scenario->priv->sinks; tmp; tmp = tmp->next) {
    GstValidateSinkInformation *sink_info =
        (GstValidateSinkInformation *) tmp->data;
    if (sink_info->sink == sink)
      return sink_info;
  }
  return NULL;
}

/* WITH SCENARIO LOCK TAKEN */
static GstValidateSeekInformation *
_find_seek_information (GstValidateScenario * scenario, guint32 seqnum)
{
  GList *tmp;

  for (tmp = scenario->priv->seeks; tmp; tmp = tmp->next) {
    GstValidateSeekInformation *seek_info =
        (GstValidateSeekInformation *) tmp->data;
    if (seek_info->seqnum == seqnum)
      return seek_info;
  }

  return NULL;
}

/* WITH SCENARIO LOCK TAKEN */
static void
_validate_sink_information (GstValidateScenario * scenario)
{
  GList *tmp;
  gboolean all_sinks_ready = TRUE;
  gboolean identical_seqnum = TRUE;
  gboolean transitioning = FALSE;
  guint32 common_seqnum = GST_SEQNUM_INVALID;
  guint32 next_seqnum = GST_SEQNUM_INVALID;
  GstValidateSeekInformation *seek_info;

  if (scenario->priv->seeks)
    /* If we have a pending seek, get the expected seqnum to
     * figure out whether we are transitioning to a seek */
    next_seqnum =
        ((GstValidateSeekInformation *) scenario->priv->seeks->data)->seqnum;

  GST_LOG_OBJECT (scenario, "next_seqnum %" G_GUINT32_FORMAT, next_seqnum);

  for (tmp = scenario->priv->sinks; tmp; tmp = tmp->next) {
    GstValidateSinkInformation *sink_info =
        (GstValidateSinkInformation *) tmp->data;
    GST_DEBUG_OBJECT (sink_info->sink,
        "seqnum:%" G_GUINT32_FORMAT " segment:%" GST_SEGMENT_FORMAT,
        sink_info->segment_seqnum, &sink_info->segment);
    if (sink_info->segment_seqnum == GST_SEQNUM_INVALID)
      all_sinks_ready = FALSE;
    else if (sink_info->segment.format == GST_FORMAT_TIME) {
      /* Are we in the middle of switching segments (from the current
       * one, or to the next week) ? */
      if (sink_info->segment_seqnum == scenario->priv->current_seqnum ||
          sink_info->segment_seqnum == next_seqnum)
        transitioning = TRUE;

      /* We are only interested in sinks that handle TIME segments */
      if (common_seqnum == GST_SEQNUM_INVALID)
        common_seqnum = sink_info->segment_seqnum;
      else if (common_seqnum != sink_info->segment_seqnum) {
        identical_seqnum = FALSE;
      }
    }
  }

  /* If not all sinks have received a segment, just return */
  if (!all_sinks_ready)
    return;

  GST_FIXME_OBJECT (scenario,
      "All sinks have valid segment. identical_seqnum:%d transitioning:%d seqnum:%"
      G_GUINT32_FORMAT " (current:%" G_GUINT32_FORMAT ") seeks:%p",
      identical_seqnum, transitioning, common_seqnum,
      scenario->priv->current_seqnum, scenario->priv->seeks);

  if (!identical_seqnum) {
    /* If all sinks received a segment *and* there is a pending seek *and* there
     * wasn't one previously, we definitely have a failure */
    if (!transitioning && scenario->priv->current_seek == NULL
        && scenario->priv->seeks) {
      GST_VALIDATE_REPORT (scenario, EVENT_SEEK_INVALID_SEQNUM,
          "Not all segments from a given seek have the same seqnum");
      return;
    }
    /* Otherwise we're either doing the initial preroll (without seek)
     * or we are in the middle of switching to another seek */
    return;
  }

  /* Now check if we have seek data related to that seqnum */
  seek_info = _find_seek_information (scenario, common_seqnum);

  if (seek_info && seek_info != scenario->priv->current_seek) {
    GST_DEBUG_OBJECT (scenario, "Found a corresponding seek !");
    /* Updating values */
    /* FIXME : Check segment values if needed ! */
    /* FIXME : Non-flushing seek, validate here */
    if (seek_info->start_type == GST_SEEK_TYPE_SET)
      scenario->priv->segment_start = seek_info->start;
    if (seek_info->stop_type == GST_SEEK_TYPE_SET)
      scenario->priv->segment_stop = seek_info->stop;
    if (scenario->priv->target_state == GST_STATE_PAUSED)
      scenario->priv->seeked_in_pause = TRUE;
    SCENARIO_UNLOCK (scenario);
    /* If it's a non-flushing seek, validate it here
     * otherwise we will do it when the async_done is received */
    if (!(seek_info->flags & GST_SEEK_FLAG_FLUSH))
      gst_validate_action_set_done (seek_info->action);
    SCENARIO_LOCK (scenario);
  }
  /* We always set the current_seek. Can be NULL if no matching  */
  scenario->priv->current_seek = seek_info;
  scenario->priv->current_seqnum = common_seqnum;
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
 * Executes a seek event on the scenario's pipeline. You should always use
 * this method when you want to execute a seek inside a new action type
 * so that the scenario state is updated taking into account that seek.
 *
 * For more information you should have a look at #gst_event_new_seek
 *
 * Returns: %TRUE if the seek could be executed, %FALSE otherwise
 */
GstValidateExecuteActionReturn
gst_validate_scenario_execute_seek (GstValidateScenario * scenario,
    GstValidateAction * action, gdouble rate, GstFormat format,
    GstSeekFlags flags, GstSeekType start_type, GstClockTime start,
    GstSeekType stop_type, GstClockTime stop)
{
  GstEvent *seek;
  GstValidateSeekInformation *seek_info;

  GstValidateExecuteActionReturn ret = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  GstValidateScenarioPrivate *priv = scenario->priv;
  DECLARE_AND_GET_PIPELINE (scenario, action);

  seek = gst_event_new_seek (rate, format, flags, start_type, start,
      stop_type, stop);

  if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Trying to seek in format %d, but not support yet!", format);
  }

  seek_info = g_new0 (GstValidateSeekInformation, 1);
  seek_info->seqnum = GST_EVENT_SEQNUM (seek);
  seek_info->rate = rate;
  seek_info->format = format;
  seek_info->flags = flags;
  seek_info->start = start;
  seek_info->stop = stop;
  seek_info->start_type = start_type;
  seek_info->stop_type = stop_type;
  seek_info->action = gst_validate_action_ref (action);

  SCENARIO_LOCK (scenario);
  priv->seeks = g_list_append (priv->seeks, seek_info);
  SCENARIO_UNLOCK (scenario);

  gst_event_ref (seek);
  if (gst_element_send_event (pipeline, seek)) {
    priv->seek_flags = flags;
    priv->seek_format = format;
  } else {
    switch (format) {
      case GST_FORMAT_TIME:
        GST_VALIDATE_REPORT_ACTION (scenario, action, EVENT_SEEK_NOT_HANDLED,
            "Could not execute seek: '(position %" GST_TIME_FORMAT
            "), %s (num %u, missing repeat: %i), seeking to: %" GST_TIME_FORMAT
            " stop: %" GST_TIME_FORMAT " Rate %lf'",
            GST_TIME_ARGS (action->playback_time), action->name,
            action->action_number, action->repeat, GST_TIME_ARGS (start),
            GST_TIME_ARGS (stop), rate);
        break;
      default:
      {
        gchar *format_str = g_enum_to_string (GST_TYPE_FORMAT, format);

        GST_VALIDATE_REPORT_ACTION (scenario, action, EVENT_SEEK_NOT_HANDLED,
            "Could not execute seek in format %s '(position %" GST_TIME_FORMAT
            "), %s (num %u, missing repeat: %i), seeking to: %" G_GINT64_FORMAT
            " stop: %" G_GINT64_FORMAT " Rate %lf'", format_str,
            GST_TIME_ARGS (action->playback_time), action->name,
            action->action_number, action->repeat, start, stop, rate);
        g_free (format_str);
        break;
      }
    }
    SCENARIO_LOCK (scenario);
    priv->seeks = g_list_remove (priv->seeks, seek_info);
    SCENARIO_UNLOCK (scenario);

    gst_validate_seek_information_free (seek_info);
    ret = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }
  gst_event_unref (seek);
  gst_object_unref (pipeline);

  return ret;
}

static gint
_execute_seek (GstValidateScenario * scenario, GstValidateAction * action)
{
  const char *str_format, *str_flags, *str_start_type, *str_stop_type;

  gdouble rate = 1.0;
  guint format = GST_FORMAT_TIME;
  GstSeekFlags flags = 0;
  guint start_type = GST_SEEK_TYPE_SET;
  GstClockTime start;
  guint stop_type = GST_SEEK_TYPE_SET;
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
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  if (!pipeline) {
    GST_ERROR_OBJECT (scenario, "No pipeline set anymore!");

    return FALSE;
  }

  gst_validate_printf (scenario, "Back to playing\n");

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to playing");
    scenario->priv->target_state = GST_STATE_PLAYING;
  }

  gst_object_unref (pipeline);

  return FALSE;
}

static gboolean
_set_const_func (GQuark field_id, const GValue * value, GstStructure * consts)
{
  gst_structure_id_set_value (consts, field_id, value);

  return TRUE;
}

static GstValidateExecuteActionReturn
_execute_define_vars (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gst_structure_foreach (action->structure,
      (GstStructureForeachFunc) _set_const_func, scenario->priv->vars);

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static GstValidateExecuteActionReturn
_set_timed_value (GQuark field_id, const GValue * gvalue,
    GstStructure * structure)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gdouble value;
  GstClockTime timestamp;
  GstTimedValueControlSource *source = NULL;
  GstControlBinding *binding;
  GstValidateScenario *scenario;
  GstValidateAction *action;
  GstObject *obj = NULL;
  GParamSpec *paramspec = NULL;
  const gchar *field = g_quark_to_string (field_id);
  const gchar *unused_fields[] =
      { "binding-type", "source-type", "interpolation-mode",
    "timestamp", "__scenario__", "__action__", "__res__", "repeat",
    "playback-time", NULL
  };

  if (g_strv_contains (unused_fields, field))
    return TRUE;

  gst_structure_get (structure, "__scenario__", G_TYPE_POINTER, &scenario,
      "__action__", G_TYPE_POINTER, &action, NULL);


  if (G_VALUE_HOLDS_DOUBLE (gvalue))
    value = g_value_get_double (gvalue);
  else if (G_VALUE_HOLDS_INT (gvalue))
    value = (gdouble) g_value_get_int (gvalue);
  else {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Invalid value type for property '%s': %s",
        field, G_VALUE_TYPE_NAME (gvalue));
    goto err;
  }

  obj = _get_target_object_property (scenario, action, field, &paramspec);
  if (!obj || !paramspec) {
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto err;
  }

  REPORT_UNLESS (gst_validate_action_get_clocktime (scenario, action,
          "timestamp", &timestamp), err,
      "Could get timestamp on %" GST_PTR_FORMAT, action->structure);

  binding = gst_object_get_control_binding (obj, paramspec->name);
  if (!binding) {
    guint mode;
    GType source_type;
    const gchar *interpolation_mode =
        gst_structure_get_string (action->structure, "interpolation-mode");
    const gchar *source_type_name =
        gst_structure_get_string (action->structure, "source-type");

    if (source_type_name) {
      source_type = g_type_from_name (source_type_name);

      REPORT_UNLESS (g_type_is_a (source_type,
              GST_TYPE_TIMED_VALUE_CONTROL_SOURCE), err,
          "Source type '%s' is not supported", source_type_name);
    } else {
      source_type = GST_TYPE_INTERPOLATION_CONTROL_SOURCE;
    }

    source = g_object_new (source_type, NULL);
    gst_object_ref_sink (source);
    if (GST_IS_INTERPOLATION_CONTROL_SOURCE (source)) {
      if (interpolation_mode)
        REPORT_UNLESS (gst_validate_utils_enum_from_str
            (GST_TYPE_INTERPOLATION_MODE, interpolation_mode, &mode), err,
            "Could not convert interpolation-mode '%s'", interpolation_mode);

      else
        mode = GST_INTERPOLATION_MODE_LINEAR;

      g_object_set (source, "mode", mode, NULL);
    }

    if (!g_strcmp0 (gst_structure_get_string (action->structure,
                "binding-type"), "direct-absolute")) {
      binding =
          gst_direct_control_binding_new_absolute (obj, paramspec->name,
          GST_CONTROL_SOURCE (source));
    } else {
      binding =
          gst_direct_control_binding_new (obj, paramspec->name,
          GST_CONTROL_SOURCE (source));
    }

    gst_object_add_control_binding (obj, binding);
  } else {
    g_object_get (binding, "control-source", &source, NULL);
  }

  REPORT_UNLESS (GST_IS_TIMED_VALUE_CONTROL_SOURCE (source), err,
      "Could not find timed value control source on %s", field);

  REPORT_UNLESS (gst_timed_value_control_source_set (source, timestamp, value),
      err, "Could not set %s=%f at %" GST_TIME_FORMAT, field, value,
      GST_TIME_ARGS (timestamp));

  gst_object_unref (obj);
  gst_structure_set (structure, "__res__", G_TYPE_INT, res, NULL);

  return TRUE;

err:
  gst_clear_object (&obj);
  gst_structure_set (structure, "__res__", G_TYPE_INT,
      GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED, NULL);

  return FALSE;
}

static GstValidateExecuteActionReturn
_set_timed_value_property (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_ERROR;

  gst_structure_set (action->structure, "__action__", G_TYPE_POINTER,
      action, "__scenario__", G_TYPE_POINTER, scenario, NULL);

  gst_structure_foreach (action->structure,
      (GstStructureForeachFunc) _set_timed_value, action->structure);
  gst_structure_get_int (action->structure, "__res__", &res);
  gst_structure_remove_fields (action->structure, "__action__", "__scenario__",
      "__res__", NULL);

  return res;
}


static GstValidateExecuteActionReturn
_check_property (GstValidateScenario * scenario, GstValidateAction * action,
    gpointer object, const gchar * propname, const GValue * expected_value)
{
  GValue cvalue = G_VALUE_INIT;

  g_value_init (&cvalue, G_VALUE_TYPE (expected_value));
  g_object_get_property (object, propname, &cvalue);

  if (gst_value_compare (&cvalue, expected_value) != GST_VALUE_EQUAL) {
    gchar *expected = gst_value_serialize (expected_value), *observed =
        gst_value_serialize (&cvalue);

    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "%" GST_PTR_FORMAT
        "::%s expected value: '(%s)%s' different than observed: '(%s)%s'",
        object, propname, G_VALUE_TYPE_NAME (&cvalue), expected,
        G_VALUE_TYPE_NAME (expected_value), observed);

    g_free (expected);
    g_free (observed);

    g_value_reset (&cvalue);
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }
  g_value_reset (&cvalue);

  return GST_VALIDATE_EXECUTE_ACTION_OK;

}

static GstValidateExecuteActionReturn
_set_or_check_properties (GQuark field_id, const GValue * value,
    GstStructure * structure)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GstValidateScenario *scenario;
  GstValidateAction *action;
  GstObject *obj = NULL;
  GParamSpec *paramspec = NULL;
  gboolean no_value_check = FALSE;
  GstValidateObjectSetPropertyFlags flags = 0;
  const gchar *field = g_quark_to_string (field_id);
  const gchar *unused_fields[] = { "__scenario__", "__action__", "__res__",
    "playback-time", "repeat", "no-value-check", NULL
  };

  if (g_strv_contains (unused_fields, field))
    return TRUE;

  gst_structure_get (structure, "__scenario__", G_TYPE_POINTER, &scenario,
      "__action__", G_TYPE_POINTER, &action, NULL);

  gst_structure_get_boolean (structure, "no-value-check", &no_value_check);

  if (no_value_check) {
    flags |= GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_NO_VALUE_CHECK;
  }
  if (action->priv->optional)
    flags |= GST_VALIDATE_OBJECT_SET_PROPERTY_FLAGS_OPTIONAL;

  obj = _get_target_object_property (scenario, action, field, &paramspec);
  if (!obj || !paramspec) {
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }
  if (gst_structure_has_name (action->structure, "set-properties"))
    res =
        gst_validate_object_set_property_full (GST_VALIDATE_REPORTER (scenario),
        G_OBJECT (obj), paramspec->name, value, flags);
  else
    res = _check_property (scenario, action, obj, paramspec->name, value);

done:
  gst_clear_object (&obj);
  if (!gst_structure_has_field (structure, "__res__")
      || res != GST_VALIDATE_EXECUTE_ACTION_OK)
    gst_structure_set (structure, "__res__", G_TYPE_INT, res, NULL);
  return TRUE;
}

static GstValidateExecuteActionReturn
_execute_set_or_check_properties (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_ERROR;

  gst_structure_set (action->structure, "__action__", G_TYPE_POINTER,
      action, "__scenario__", G_TYPE_POINTER, scenario, NULL);

  gst_structure_foreach (action->structure,
      (GstStructureForeachFunc) _set_or_check_properties, action->structure);
  gst_structure_get_int (action->structure, "__res__", &res);
  gst_structure_remove_fields (action->structure, "__action__", "__scenario__",
      "__res__", NULL);

  return res;
}

static GstValidateExecuteActionReturn
_execute_set_state (GstValidateScenario * scenario, GstValidateAction * action)
{
  guint state;
  const gchar *str_state;
  GstStateChangeReturn ret;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  g_return_val_if_fail ((str_state =
          gst_structure_get_string (action->structure, "state")), FALSE);

  g_return_val_if_fail (gst_validate_utils_enum_from_str (GST_TYPE_STATE,
          str_state, &state), FALSE);


  scenario->priv->target_state = state;
  scenario->priv->changing_state = TRUE;
  scenario->priv->seeked_in_pause = FALSE;

  ret = gst_element_set_state (pipeline, state);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    scenario->priv->changing_state = FALSE;
    GST_VALIDATE_REPORT_ACTION (scenario, action, STATE_CHANGE_FAILURE,
        "Failed to set state to %s", str_state);

    /* Nothing async on failure, action will be removed automatically */
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
    goto done;
  } else if (ret == GST_STATE_CHANGE_ASYNC) {

    scenario->priv->needs_async_done = TRUE;
    res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;

    goto done;
  }

  scenario->priv->changing_state = FALSE;

done:
  gst_object_unref (pipeline);

  return res;
}

static GstValidateExecuteActionReturn
_execute_pause (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstClockTime duration = 0;
  GstValidateExecuteActionReturn ret;

  gst_validate_action_get_clocktime (scenario, action, "duration", &duration);
  gst_structure_set (action->structure, "state", G_TYPE_STRING, "paused", NULL);

  GST_INFO_OBJECT (scenario, "Pausing for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration));

  ret = _execute_set_state (scenario, action);

  if (ret != GST_VALIDATE_EXECUTE_ACTION_ERROR && duration)
    g_timeout_add (GST_TIME_AS_MSECONDS (duration),
        (GSourceFunc) _pause_action_restore_playing, scenario);

  return ret;
}

static GstValidateExecuteActionReturn
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

static void
gst_validate_scenario_check_dropped (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (priv->max_dropped == -1 || priv->dropped == -1)
    return;

  GST_DEBUG_OBJECT (scenario, "Number of dropped buffers: %d (max allowed: %d)",
      priv->dropped, priv->max_dropped);

  if (priv->dropped > priv->max_dropped) {
    GST_VALIDATE_REPORT (scenario, CONFIG_TOO_MANY_BUFFERS_DROPPED,
        "Too many buffers have been dropped: %d (max allowed: %d)",
        priv->dropped, priv->max_dropped);
  }
}

static GstValidateExecuteActionReturn
_execute_eos (GstValidateScenario * scenario, GstValidateAction * action)
{
  gboolean ret;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  GST_DEBUG ("Sending EOS to pipeline at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (action->playback_time));

  ret = gst_element_send_event (pipeline, gst_event_new_eos ());
  gst_object_unref (pipeline);

  return ret ? GST_VALIDATE_EXECUTE_ACTION_OK :
      GST_VALIDATE_EXECUTE_ACTION_ERROR;
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

static GstValidateExecuteActionReturn
execute_switch_track_default (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  guint index;
  gboolean relative = FALSE;
  const gchar *type, *str_index;
  GstElement *input_selector;
  GstValidateExecuteActionReturn ret = GST_VALIDATE_EXECUTE_ACTION_ERROR;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  /* First find an input selector that has the right type */
  input_selector = find_input_selector_with_type (GST_BIN (pipeline), type);
  if (input_selector) {
    GstState state, next;
    GstPad *pad, *cpad, *srcpad;

    ret = GST_VALIDATE_EXECUTE_ACTION_OK;
    str_index = gst_structure_get_string (action->structure, "index");

    if (str_index == NULL) {
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
    if (gst_element_get_state (pipeline, &state, &next, 0) &&
        state == GST_STATE_PLAYING && next == GST_STATE_VOID_PENDING) {
      srcpad = gst_element_get_static_pad (input_selector, "src");

      gst_pad_add_probe (srcpad,
          GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
          (GstPadProbeCallback) _check_select_pad_done, action, NULL);
      ret = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
      gst_object_unref (srcpad);
    }

    g_object_set (input_selector, "active-pad", pad, NULL);
    gst_object_unref (pad);
    gst_object_unref (cpad);
    gst_object_unref (input_selector);

    goto done;
  }

  /* No selector found -> Failed */
done:
  gst_object_unref (pipeline);

  return ret;
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

static GstValidateExecuteActionReturn
execute_switch_track_pb (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gint index, n;
  const gchar *type, *str_index;

  gint flags, current, tflag;
  gchar *tmp, *current_txt;

  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gboolean relative = FALSE, disabling = FALSE;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  tflag = gst_validate_utils_flags_from_str (g_type_from_name ("GstPlayFlags"),
      type);
  current_txt = g_strdup_printf ("current-%s", type);

  tmp = g_strdup_printf ("n-%s", type);
  g_object_get (pipeline, "flags", &flags, tmp, &n, current_txt, &current,
      NULL);

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
    if (n == 0) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Trying to execute a relative %s for %s track when there"
          " is no track of this type available on current stream.",
          action->type, type);

      res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
      goto done;
    }

    index = (current + index) % n;
  }

  if (!disabling) {
    GstState state, next;
    GstPad *oldpad, *newpad;
    tmp = g_strdup_printf ("get-%s-pad", type);
    g_signal_emit_by_name (G_OBJECT (pipeline), tmp, current, &oldpad);
    g_signal_emit_by_name (G_OBJECT (pipeline), tmp, index, &newpad);

    gst_validate_printf (action, "Switching to track number: %i,"
        " (from %s:%s to %s:%s)\n", index, GST_DEBUG_PAD_NAME (oldpad),
        GST_DEBUG_PAD_NAME (newpad));
    flags |= tflag;
    g_free (tmp);

    if (gst_element_get_state (pipeline, &state, &next, 0) &&
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

  g_object_set (pipeline, "flags", flags, current_txt, index, NULL);
  g_free (current_txt);

done:
  gst_object_unref (pipeline);
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

  if (G_UNLIKELY (n == 0)) {
    GST_ERROR ("No streams available of the required type");
    return result;
  }

  if (relative) {               /* We are changing track relatively to current track */
    index = (current + index) % n;
  } else
    index %= n;

  /* Add the new stream we want to switch to */
  s = streams[index];

  gst_validate_printf (action, "Switching from stream %s to %s",
      current_stream ? current_stream->stream_id : "", s->stream_id);

  return g_list_append (result, (gpointer) s->stream_id);
}

static GstValidateExecuteActionReturn
execute_switch_track_pb3 (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
  GstValidateScenarioPrivate *priv = scenario->priv;
  gint index;
  GstStreamType stype;
  const gchar *type, *str_index;
  GList *new_streams = NULL;
  GstValidatePipelineMonitor *monitor;
  DECLARE_AND_GET_PIPELINE (scenario, action);

  monitor = (GstValidatePipelineMonitor *) (g_object_get_data ((GObject *)
          pipeline, "validate-monitor"));

  if (!monitor->stream_collection) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "No stream collection message received on the bus, "
        "can not switch track.");
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

  if (!monitor->streams_selected) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "No streams selected message received on the bus");
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
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

  if (!gst_element_send_event (pipeline,
          gst_event_new_select_streams (new_streams))) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "select-streams event not handled");
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

  priv->pending_switch_track = action;
  if (scenario->priv->target_state > GST_STATE_PAUSED) {
    res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  } else {
    gst_validate_action_ref (action);
    res = GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING;
  }

done:
  gst_object_unref (pipeline);

  return res;
}

static GstValidateExecuteActionReturn
_execute_switch_track (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidatePipelineMonitor *monitor;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  monitor = (GstValidatePipelineMonitor *) (g_object_get_data ((GObject *)
          pipeline, "validate-monitor"));
  gst_object_unref (pipeline);

  if (monitor->is_playbin)
    return execute_switch_track_pb (scenario, action);
  else if (monitor->is_playbin3)
    return execute_switch_track_pb3 (scenario, action);

  return execute_switch_track_default (scenario, action);
}

static GstValidateExecuteActionReturn
_execute_set_rank_or_disable_feature (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  guint rank;
  GList *features, *origlist;
  GstPlugin *plugin;
  GstPluginFeature *feature;
  const gchar *name;
  gboolean removing_feature =
      gst_structure_has_name (action->structure, "remove-plugin-feature");
  GstRegistry *registry = gst_registry_get ();

  REPORT_UNLESS (
      (name = gst_structure_get_string (action->structure, "feature-name")) ||
      (name = gst_structure_get_string (action->structure, "name")), done,
      "Could not find the name of the plugin/feature(s) to tweak");

  if (removing_feature)
    REPORT_UNLESS (
        (gst_structure_get_uint (action->structure, "rank", &rank)) ||
        (gst_structure_get_int (action->structure, "rank", (gint *) & rank)),
        done, "Could not get rank to set on %s", name);

  feature = gst_registry_lookup_feature (registry, name);
  if (feature) {
    if (removing_feature)
      gst_plugin_feature_set_rank (feature, rank);
    else
      gst_registry_remove_feature (registry, feature);
    gst_object_unref (feature);

    goto done;
  }

  REPORT_UNLESS ((plugin = gst_registry_find_plugin (registry, name)),
      done, "Could not find %s", name);

  if (removing_feature) {
    gst_registry_remove_plugin (registry, plugin);
    goto done;
  }

  origlist = features =
      gst_registry_get_feature_list_by_plugin (registry,
      gst_plugin_get_name (plugin));
  for (; features; features = features->next)
    gst_plugin_feature_set_rank (features->data, rank);
  gst_plugin_feature_list_free (origlist);

done:
  return res;
}

static inline gboolean
_add_execute_actions_gsource (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  SCENARIO_LOCK (scenario);
  if (priv->execute_actions_source_id == 0 && priv->wait_id == 0
      && priv->signal_handler_id == 0 && priv->wait_message_action == NULL) {
    if (!scenario->priv->action_execution_interval)
      priv->execute_actions_source_id =
          g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
          (GSourceFunc) execute_next_action,
          gst_object_ref (GST_OBJECT_CAST (scenario)), gst_object_unref);
    else
      priv->execute_actions_source_id =
          g_timeout_add_full (G_PRIORITY_DEFAULT,
          scenario->priv->action_execution_interval,
          (GSourceFunc) execute_next_action,
          gst_object_ref (GST_OBJECT_CAST (scenario)), gst_object_unref);
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
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  if (!pipeline) {
    GST_ERROR_OBJECT (scenario, "No pipeline set anymore!");

    return FALSE;
  }

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

    goto fail;
  }

  if (has_pos && has_dur && !priv->got_eos) {
    if (*position > duration) {
      _add_execute_actions_gsource (scenario);
      GST_VALIDATE_REPORT (scenario,
          QUERY_POSITION_SUPERIOR_DURATION,
          "Reported position %" GST_TIME_FORMAT " > reported duration %"
          GST_TIME_FORMAT, GST_TIME_ARGS (*position), GST_TIME_ARGS (duration));

      goto done;
    }
  }

done:
  gst_object_unref (pipeline);
  return TRUE;

fail:
  gst_object_unref (pipeline);
  return FALSE;
}

static gboolean
_check_position (GstValidateScenario * scenario, GstValidateAction * act,
    GstClockTime * position, gdouble * rate)
{
  GstQuery *query;

  GstClockTime start_with_tolerance, stop_with_tolerance;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline;

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
          && *position < start_with_tolerance
          && priv->seek_format == GST_FORMAT_TIME)) {

    GST_VALIDATE_REPORT_ACTION (scenario, act, QUERY_POSITION_OUT_OF_SEGMENT,
        "Current position %" GST_TIME_FORMAT " not in the expected range [%"
        GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, GST_TIME_ARGS (*position),
        GST_TIME_ARGS (start_with_tolerance),
        GST_TIME_ARGS (stop_with_tolerance));
  }

  pipeline = gst_validate_scenario_get_pipeline (scenario);
  if (pipeline == NULL) {
    GST_INFO_OBJECT (scenario, "No pipeline set anymore");

    return TRUE;
  }

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (GST_ELEMENT (pipeline), query))
    gst_query_parse_segment (query, rate, NULL, NULL, NULL);
  gst_query_unref (query);
  gst_object_unref (pipeline);

  if (priv->seeked_in_pause && priv->seek_flags & GST_SEEK_FLAG_ACCURATE &&
      priv->seek_format == GST_FORMAT_TIME) {
    if (*rate > 0
        && (GstClockTime) ABS (GST_CLOCK_DIFF (*position,
                priv->segment_start)) > priv->seek_pos_tol) {
      priv->seeked_in_pause = FALSE;
      GST_VALIDATE_REPORT_ACTION (scenario, act,
          EVENT_SEEK_RESULT_POSITION_WRONG,
          "Reported position after accurate seek in PAUSED state should be exactly"
          " what the user asked for. Position %" GST_TIME_FORMAT
          " is not not the expected one:  %" GST_TIME_FORMAT,
          GST_TIME_ARGS (*position), GST_TIME_ARGS (priv->segment_start));
    }
  }

  return TRUE;
}

static gboolean
_check_message_type (GstValidateScenario * scenario, GstValidateAction * act,
    GstMessage * message)
{
  return act && message
      && !g_strcmp0 (gst_structure_get_string (act->structure, "on-message"),
      gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
}

static gboolean
_should_execute_action (GstValidateScenario * scenario, GstValidateAction * act,
    GstClockTime position, gdouble rate)
{
  GstElement *pipeline = NULL;

  if (!act) {
    GST_DEBUG_OBJECT (scenario, "No action to execute");

    goto no;
  }

  pipeline = gst_validate_scenario_get_pipeline (scenario);
  if (pipeline == NULL) {

    if (!(GST_VALIDATE_ACTION_GET_TYPE (act)->flags &
            GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE)) {
      GST_VALIDATE_REPORT_ACTION (scenario, act,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Trying to execute an %s action after the pipeline has been destroyed"
          " but the type has not been marked as "
          "GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE", act->type);

      return FALSE;
    } else if (GST_CLOCK_TIME_IS_VALID (act->playback_time)) {
      GST_VALIDATE_REPORT_ACTION (scenario, act,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Trying to execute action %s with playback time %" GST_TIME_FORMAT
          " after the pipeline has been destroyed. It is impossible"
          " to execute an action with a playback time specified"
          " after the pipeline has been destroyed", act->type,
          GST_TIME_ARGS (act->playback_time));

      goto no;
    }

    GST_DEBUG_OBJECT (scenario, "No pipeline, go and execute action!");

    goto yes;
  } else if (scenario->priv->got_eos) {
    GST_DEBUG_OBJECT (scenario, "Just got EOS go and execute next action!");
    scenario->priv->got_eos = FALSE;
  } else if (GST_STATE (pipeline) < GST_STATE_PAUSED) {
    GST_DEBUG_OBJECT (scenario, "Pipeline not even in paused, "
        "just executing actions");

    goto yes;
  } else if (act->playback_time == GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (scenario, "No timing info, executing action");

    goto yes;
  } else if ((rate > 0 && (GstClockTime) position < act->playback_time)) {
    GST_DEBUG_OBJECT (scenario, "positive rate and position %" GST_TIME_FORMAT
        " < playback_time %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
        GST_TIME_ARGS (act->playback_time));

    goto no;
  } else if (rate < 0 && (GstClockTime) position > act->playback_time) {
    GST_DEBUG_OBJECT (scenario, "negative rate and position %" GST_TIME_FORMAT
        " < playback_time %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
        GST_TIME_ARGS (act->playback_time));

    goto no;
  }

yes:
  gst_object_unref (pipeline);
  return TRUE;

no:
  gst_clear_object (&pipeline);
  return FALSE;
}

static gboolean
_set_action_playback_time (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  if (!gst_validate_action_get_clocktime (scenario, action,
          "playback-time", &action->playback_time)) {
    gst_validate_error_structure (action,
        "Could not parse playback-time in %" GST_PTR_FORMAT, action->structure);

    return FALSE;
  }

  gst_structure_set (action->structure, "playback-time", GST_TYPE_CLOCK_TIME,
      action->playback_time, NULL);

  return TRUE;
}

static gboolean
gst_validate_parse_next_action_playback_time (GstValidateScenario * self)
{
  GstValidateAction *action;
  GstValidateScenarioPrivate *priv = self->priv;

  if (!priv->actions)
    return TRUE;

  action = (GstValidateAction *) priv->actions->data;
  if (!action->priv->needs_playback_parsing)
    return TRUE;

  if (!_set_action_playback_time (self, action)) {
    GST_ERROR_OBJECT (self, "Could not set playback_time!");

    return FALSE;
  }
  action->priv->needs_playback_parsing = FALSE;

  return TRUE;
}

static gboolean
_foreach_find_iterator (GQuark field_id, GValue * value,
    GstValidateAction * action)
{
  const gchar *field = g_quark_to_string (field_id);

  if (!g_strcmp0 (field, "actions"))
    return TRUE;

  if (!GST_VALUE_HOLDS_INT_RANGE (value) && !GST_VALUE_HOLDS_ARRAY (value)) {
    gst_validate_error_structure (action,
        "Unsupported iterator type `%s` for %s"
        ". Only ranges (`[(int)start, (int)stop, [(int)step]]`) and arrays "
        " (`<item1, item2>`) are supported", field, G_VALUE_TYPE_NAME (value));
    return TRUE;
  }

  if (GST_VALIDATE_ACTION_RANGE_NAME (action)) {
    gst_validate_error_structure (action, "Wrong iterator syntax, "
        " only one iterator field is supported.");
    return FALSE;
  }

  GST_VALIDATE_ACTION_RANGE_NAME (action) = field;
  return TRUE;
}


/**
 * gst_validate_execute_action:
 * @action_type: The #GstValidateActionType to execute
 * @action: (transfer full): The #GstValidateAction to execute
 *
 * Executes @action
 */
GstValidateExecuteActionReturn
gst_validate_execute_action (GstValidateActionType * action_type,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res;
  GstValidateScenario *scenario;

  g_return_val_if_fail (g_strcmp0 (action_type->name, action->type) == 0,
      GST_VALIDATE_EXECUTE_ACTION_ERROR);

  scenario = gst_validate_action_get_scenario (action);
  g_assert (scenario);

  action->priv->context = g_main_context_ref (scenario->priv->context);
  if (action_type->prepare) {
    res = action_type->prepare (action);
    if (res == GST_VALIDATE_EXECUTE_ACTION_DONE) {
      gst_validate_print_action (action, NULL);
      return GST_VALIDATE_EXECUTE_ACTION_OK;
    }

    if (res != GST_VALIDATE_EXECUTE_ACTION_OK) {
      GST_ERROR_OBJECT (scenario, "Action %" GST_PTR_FORMAT
          " could not be prepared", action->structure);

      gst_object_unref (scenario);
      return res;
    }
  }

  gst_validate_print_action (action, NULL);

  action->priv->execution_time = gst_util_get_timestamp ();
  action->priv->state = GST_VALIDATE_EXECUTE_ACTION_IN_PROGRESS;
  action_type->priv->n_calls++;
  res = action_type->execute (scenario, action);
  gst_object_unref (scenario);

  return res;
}

/* scenario can be NULL **only** if the action is a CONFIG action and
 * add_to_lists is FALSE */
static GstValidateExecuteActionReturn
_fill_action (GstValidateScenario * scenario, GstValidateAction * action,
    GstStructure * structure, gboolean add_to_lists)
{
  gdouble playback_time;
  gboolean is_config = FALSE;
  GstValidateActionType *action_type;
  GstValidateScenarioPrivate *priv = scenario ? scenario->priv : NULL;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_NONE;
  gboolean optional, needs_parsing = FALSE;

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
  } else if (gst_structure_has_field_typed (structure, "playback-time",
          G_TYPE_STRING)
      || gst_structure_has_field_typed (structure, "playback_time",
          G_TYPE_STRING)) {

    if (add_to_lists && priv) {
      action->priv->needs_playback_parsing = TRUE;
      needs_parsing = TRUE;
    }
  } else
    GST_INFO_OBJECT (scenario,
        "No playback time for action %" GST_PTR_FORMAT, structure);

  if (!gst_validate_utils_get_clocktime (structure,
          "timeout", &action->priv->timeout)) {
    GST_INFO_OBJECT (scenario,
        "No timeout time for action %" GST_PTR_FORMAT, structure);
  }

  action->structure = gst_structure_copy (structure);

  if (!(action->name = gst_structure_get_string (action->structure, "name")))
    action->name = "";

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

    action_type->priv->n_calls++;
    res = action_type->execute (scenario, action);
    gst_validate_print_action (action, NULL);

    return res;
  }

  if (!add_to_lists)
    return res;

  if (priv != NULL) {
    GstValidateActionType *type = _find_action_type (action->type);
    gboolean can_execute_on_addition =
        type->flags & GST_VALIDATE_ACTION_TYPE_CAN_EXECUTE_ON_ADDITION
        && !GST_CLOCK_TIME_IS_VALID (action->playback_time)
        && !gst_structure_has_field (action->structure, "on-message");

    if (needs_parsing)
      can_execute_on_addition = FALSE;

    if (can_execute_on_addition) {
      GList *tmp;

      for (tmp = priv->actions; tmp; tmp = tmp->next) {
        GstValidateAction *act = (GstValidateAction *) tmp->data;
        if (GST_CLOCK_TIME_IS_VALID (act->playback_time)) {
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

static gboolean
gst_validate_scenario_execute_next_or_restart_looping (GstValidateScenario *
    scenario)
{
  /* Recurse to the next action if it is possible
   * to execute right away */
  if (!scenario->priv->execute_on_idle) {
    GST_DEBUG_OBJECT (scenario, "linking next action execution");

    return execute_next_action (scenario);
  } else {
    _add_execute_actions_gsource (scenario);
    GST_DEBUG_OBJECT (scenario, "Executing only on idle, waiting for"
        " next dispatch");
  }
  return G_SOURCE_CONTINUE;
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
execute_next_action_full (GstValidateScenario * scenario, GstMessage * message)
{
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

  if (!act) {
    _check_scenario_is_done (scenario);
    return G_SOURCE_CONTINUE;
  }

  if (message && GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS
      && act->playback_time != GST_CLOCK_TIME_NONE) {
    GST_VALIDATE_REPORT_ACTION (scenario, act,
        SCENARIO_ACTION_ENDED_EARLY,
        "Got EOS before action playback time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (act->playback_time));
    goto execute_action;
  }

  switch (act->priv->state) {
    case GST_VALIDATE_EXECUTE_ACTION_NONE:
    case GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING:
      break;
    case GST_VALIDATE_EXECUTE_ACTION_IN_PROGRESS:
      return G_SOURCE_CONTINUE;
    case GST_VALIDATE_EXECUTE_ACTION_ASYNC:
      if (GST_CLOCK_TIME_IS_VALID (act->priv->timeout)) {
        GstClockTime etime =
            gst_util_get_timestamp () - act->priv->execution_time;

        if (etime > act->priv->timeout) {
          gchar *str = gst_structure_to_string (act->structure);

          GST_VALIDATE_REPORT_ACTION (scenario, act,
              SCENARIO_ACTION_EXECUTION_ERROR,
              "Action %s timed out after: %" GST_TIME_FORMAT, str,
              GST_TIME_ARGS (etime));

          g_free (str);
        }
      }
      GST_LOG_OBJECT (scenario, "Action %" GST_PTR_FORMAT " still running",
          act->structure);

      return G_SOURCE_CONTINUE;
    default:
      GST_ERROR ("State is %d", act->priv->state);
      g_assert_not_reached ();
  }

  if (message) {
    if (!_check_message_type (scenario, act, message))
      return G_SOURCE_CONTINUE;
  } else if ((act && gst_structure_get_string (act->structure, "on-message") &&
          !GST_CLOCK_TIME_IS_VALID (act->playback_time)) ||
      (!_check_position (scenario, act, &position, &rate))) {
    return G_SOURCE_CONTINUE;
  }

  if (!_should_execute_action (scenario, act, position, rate)) {
    _add_execute_actions_gsource (scenario);

    return G_SOURCE_CONTINUE;
  }

execute_action:
  type = _find_action_type (act->type);

  GST_DEBUG_OBJECT (scenario, "Executing %" GST_PTR_FORMAT
      " at %" GST_TIME_FORMAT, act->structure, GST_TIME_ARGS (position));
  priv->seeked_in_pause = FALSE;

  if (message)
    gst_structure_remove_field (act->structure, "playback-time");
  else
    gst_structure_remove_field (act->structure, "on-message");

  act->priv->state = gst_validate_execute_action (type, act);
  switch (act->priv->state) {
    case GST_VALIDATE_EXECUTE_ACTION_ASYNC:
      GST_DEBUG_OBJECT (scenario, "Remove source, waiting for action"
          " to be done.");

      SCENARIO_LOCK (scenario);
      priv->execute_actions_source_id = 0;
      SCENARIO_UNLOCK (scenario);

      return G_SOURCE_CONTINUE;
    case GST_VALIDATE_EXECUTE_ACTION_IN_PROGRESS:
      return G_SOURCE_CONTINUE;
    case GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING:
      SCENARIO_LOCK (scenario);
      priv->non_blocking_running_actions =
          g_list_append (priv->non_blocking_running_actions, act);
      priv->actions = g_list_remove (priv->actions, act);
      SCENARIO_UNLOCK (scenario);
      return gst_validate_scenario_execute_next_or_restart_looping (scenario);
    default:
      gst_validate_action_set_done (act);
      return G_SOURCE_CONTINUE;
  }
}

static gboolean
execute_next_action (GstValidateScenario * scenario)
{
  return execute_next_action_full (scenario, NULL);
}

static gboolean
stop_waiting (GstValidateAction * action)
{
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);

  SCENARIO_LOCK (scenario);
  scenario->priv->wait_id = 0;
  SCENARIO_UNLOCK (scenario);

  gst_validate_action_set_done (action);
  _add_execute_actions_gsource (scenario);
  gst_object_unref (scenario);


  return G_SOURCE_REMOVE;
}

static void
stop_waiting_signal (GstStructure * data)
{
  guint sigid = 0;
  GstElement *target;
  GstStructure *check = NULL;
  GstValidateAction *action;
  GstValidateScenario *scenario;

  gst_structure_get (data, "target", G_TYPE_POINTER, &target,
      "action", GST_TYPE_VALIDATE_ACTION, &action, "sigid", G_TYPE_UINT, &sigid,
      NULL);
  gst_structure_free (data);

  scenario = gst_validate_action_get_scenario (action);

  g_assert (scenario);
  SCENARIO_LOCK (scenario);
  g_signal_handler_disconnect (target,
      sigid ? sigid : scenario->priv->signal_handler_id);
  if (!sigid)
    scenario->priv->signal_handler_id = 0;
  SCENARIO_UNLOCK (scenario);

  if (gst_structure_get (action->structure, "check", GST_TYPE_STRUCTURE,
          &check, NULL)) {
    GstValidateAction *subact =
        gst_validate_create_subaction (scenario, NULL, action,
        check, 0, 0);
    GstValidateActionType *subact_type = _find_action_type (subact->type);
    if (!(subact_type->flags & GST_VALIDATE_ACTION_TYPE_CHECK)) {
      gst_validate_error_structure (action,
          "`check` action %s is not marked as 'check'", subact->type);
    }

    gst_validate_execute_action (subact_type, subact);
    gst_validate_action_unref (subact);
  }

  gst_validate_action_set_done (action);
  gst_validate_action_unref (action);
  _add_execute_actions_gsource (scenario);
  gst_object_unref (scenario);
  gst_object_unref (target);
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
  gboolean non_blocking;
  GstValidateScenarioPrivate *priv = scenario->priv;
  const gchar *signal_name = gst_structure_get_string
      (action->structure, "signal-name");
  GList *targets = NULL;
  GstElement *target;
  GstStructure *data;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  DECLARE_AND_GET_PIPELINE (scenario, action);

  REPORT_UNLESS (signal_name, err, "No signal-name given for wait action");
  targets = _find_elements_defined_in_action (scenario, action);
  REPORT_UNLESS ((g_list_length (targets) == 1), err,
      "Could not find target element.");

  gst_validate_printf (action, "Waiting for '%s' signal\n", signal_name);

  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }

  target = targets->data;
  data =
      gst_structure_new ("a", "action", GST_TYPE_VALIDATE_ACTION, action,
      "target", G_TYPE_POINTER, target, NULL);
  SCENARIO_LOCK (scenario);
  priv->signal_handler_id = g_signal_connect_swapped (target, signal_name,
      (GCallback) stop_waiting_signal, data);

  non_blocking =
      gst_structure_get_boolean (action->structure, "non-blocking",
      &non_blocking);
  if (non_blocking) {
    gst_structure_set (data, "sigid", G_TYPE_UINT, priv->signal_handler_id,
        NULL);
    priv->signal_handler_id = 0;
  }
  SCENARIO_UNLOCK (scenario);

  gst_object_unref (pipeline);
  g_list_free (targets);


  return non_blocking ? GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING :
      GST_VALIDATE_EXECUTE_ACTION_ASYNC;

err:
  g_list_free_full (targets, gst_object_unref);
  gst_object_unref (pipeline);
  return res;
}

static gboolean
_execute_wait_for_message (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const gchar *message_type = gst_structure_get_string
      (action->structure, "message-type");
  DECLARE_AND_GET_PIPELINE (scenario, action);

  gst_validate_printf (action, "Waiting for '%s' message\n", message_type);

  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }

  g_assert (!priv->wait_message_action);
  priv->wait_message_action = gst_validate_action_ref (action);
  gst_object_unref (pipeline);

  return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static GstValidateExecuteActionReturn
_execute_wait (GstValidateScenario * scenario, GstValidateAction * action)
{
  gboolean on_clock = FALSE;

  gst_structure_get_boolean (action->structure, "on-clock", &on_clock);
  if (gst_structure_has_field (action->structure, "signal-name")) {
    return _execute_wait_for_signal (scenario, action);
  } else if (gst_structure_has_field (action->structure, "message-type")) {
    return _execute_wait_for_message (scenario, action);
  } else if (on_clock) {
    gst_test_clock_wait_for_next_pending_id (scenario->priv->clock, NULL);

    return GST_VALIDATE_EXECUTE_ACTION_OK;
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
  DECLARE_AND_GET_PIPELINE (scenario, action);

  gst_structure_get_int (action->structure, "details", &details);
  if (name)
    dotname = g_strdup_printf ("validate.action.%s", name);
  else
    dotname = g_strdup ("validate.action.unnamed");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline), details, dotname);

  g_free (dotname);
  gst_object_unref (pipeline);

  return TRUE;
}

static GstElement *
_get_target_element (GstValidateScenario * scenario, GstValidateAction * action)
{
  const gchar *name;
  GstElement *target;
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  if (!pipeline) {
    GST_ERROR_OBJECT (scenario, "No pipeline set anymore!");

    return NULL;
  }

  name = gst_structure_get_string (action->structure, "target-element-name");
  if (name == NULL) {
    gst_object_unref (pipeline);

    return NULL;
  }

  if (g_strcmp0 (GST_OBJECT_NAME (pipeline), name) == 0) {
    target = gst_object_ref (pipeline);
  } else {
    target = gst_bin_get_by_name (GST_BIN (pipeline), name);
  }

  if (target == NULL)
    GST_ERROR ("Target element with given name (%s) not found", name);
  gst_object_unref (pipeline);

  return target;
}

/* _get_target_elements_by_klass_or_factory_name:
 * @scenario: a #GstValidateScenario
 * @action: a #GstValidateAction
 *
 * Returns all the elements in the pipeline whose GST_ELEMENT_METADATA_KLASS
 * matches the 'target-element-klass' of @action and the factory name matches
 * the 'target-element-factory-name'.
 *
 * Returns: (transfer full) (element-type GstElement): a list of #GstElement
 */
static GList *
_get_target_elements_by_klass_or_factory_name (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GList *result = NULL;
  GstIterator *it;
  const gchar *klass, *fname;
  GValue v = G_VALUE_INIT, param = G_VALUE_INIT;
  gboolean done = FALSE;
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  if (!pipeline) {
    GST_ERROR_OBJECT (scenario, "No pipeline set anymore!");

    return NULL;
  }

  klass = gst_structure_get_string (action->structure, "target-element-klass");
  fname =
      gst_structure_get_string (action->structure,
      "target-element-factory-name");
  if (!klass && !fname) {
    gst_object_unref (pipeline);

    return NULL;
  }

  if (klass && gst_validate_element_has_klass (pipeline, klass))
    result = g_list_prepend (result, gst_object_ref (pipeline));

  if (fname && gst_element_get_factory (pipeline)
      && !g_strcmp0 (GST_OBJECT_NAME (gst_element_get_factory (pipeline)),
          fname))
    result = g_list_prepend (result, gst_object_ref (pipeline));

  it = gst_bin_iterate_recurse (GST_BIN (pipeline));

  g_value_init (&param, G_TYPE_STRING);
  g_value_set_string (&param, klass);

  while (!done) {
    switch (gst_iterator_next (it, &v)) {
      case GST_ITERATOR_OK:{
        GstElement *child = g_value_get_object (&v);

        if (g_list_find (result, child))
          goto next;

        if (klass && gst_validate_element_has_klass (child, klass)) {
          result = g_list_prepend (result, gst_object_ref (child));
          goto next;
        }

        if (fname && gst_element_get_factory (child)
            && !g_strcmp0 (GST_OBJECT_NAME (gst_element_get_factory (child)),
                fname))
          result = g_list_prepend (result, gst_object_ref (child));
      next:
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
  gst_iterator_free (it);
  gst_object_unref (pipeline);

  return result;
}

static GList *
_find_elements_defined_in_action (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstElement *target;
  GList *targets = NULL;

  /* set-property can be applied on either:
   * - a single element having target-element-name as name
   * - all the elements having target-element-klass as klass
   */
  if (gst_structure_get_string (action->structure, "target-element-name")) {
    target = _get_target_element (scenario, action);
    if (target == NULL)
      return FALSE;

    targets = g_list_append (targets, target);
  } else if (gst_structure_get_string (action->structure,
          "target-element-klass") ||
      gst_structure_get_string (action->structure,
          "target-element-factory-name")) {
    targets = _get_target_elements_by_klass_or_factory_name (scenario, action);
  }

  return targets;
}

static GstValidateExecuteActionReturn
_execute_check_action_type_calls (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  const gchar *type;
  GstValidateActionType *t;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gint n;

  REPORT_UNLESS (gst_structure_get_int (action->structure, "n", &n),
      done, "No `n`!");
  REPORT_UNLESS ((type = gst_structure_get_string (action->structure, "type")),
      done, "No `type`!");
  REPORT_UNLESS ((t =
          _find_action_type (type)), done, "Can't find `%s`!", type);
  REPORT_UNLESS (t->priv->n_calls == n, done,
      "%s called %d times instead of expected %d", type, t->priv->n_calls, n);


done:
  return res;
}

static GstValidateExecuteActionReturn
_execute_check_subaction_level (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gint n;

  REPORT_UNLESS (gst_structure_get_int (action->structure, "level", &n),
      done, "No `n`!");
  REPORT_UNLESS (gst_validate_action_get_level (action) == n, done,
      "Expected subaction level %d, got %d", n,
      gst_validate_action_get_level (action));


done:
  return res;
}

static gboolean
set_env_var (GQuark field_id, GValue * value,
    GSubprocessLauncher * subproc_launcher)
{
  g_subprocess_launcher_setenv (subproc_launcher, g_quark_to_string (field_id),
      g_value_get_string (value), TRUE);

  return TRUE;
}

static GstValidateExecuteActionReturn
_run_command (GstValidateScenario * scenario, GstValidateAction * action)
{
  gchar **argv = NULL, *_stderr = NULL;
  GError *error = NULL;
  const GValue *env = NULL;
  GSubprocess *subproc = NULL;
  GSubprocessLauncher *subproc_launcher = NULL;
  GstValidateExecuteActionReturn res =
      GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

  REPORT_UNLESS ((argv = gst_validate_utils_get_strv (action->structure,
              "argv")), done,
      "Couldn't find `argv` as array of strings in %" GST_PTR_FORMAT,
      action->structure);

  subproc_launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_STDERR_PIPE);
  g_subprocess_launcher_unsetenv (subproc_launcher, "GST_VALIDATE_SCENARIO");
  g_subprocess_launcher_unsetenv (subproc_launcher, "GST_VALIDATE_CONFIG");

  env = gst_structure_get_value (action->structure, "env");
  REPORT_UNLESS (!env || GST_VALUE_HOLDS_STRUCTURE (env), done,
      "The `env` parameter should be a GstStructure, got %s",
      G_VALUE_TYPE_NAME (env));
  if (env) {
    gst_structure_foreach (gst_value_get_structure (env),
        (GstStructureForeachFunc) set_env_var, subproc_launcher);
  }

  REPORT_UNLESS (
      (subproc =
          g_subprocess_launcher_spawnv (subproc_launcher,
              (const gchar * const *) argv, &error)), done,
      "Couldn't start subprocess: %s", error->message);

  REPORT_UNLESS (g_subprocess_communicate_utf8 (subproc, NULL, NULL, NULL,
          &_stderr, &error), done, "Failed to run check: %s", error->message);

  REPORT_UNLESS (g_subprocess_get_exit_status (subproc) == 0,
      done, "Sub command failed. Stderr: %s", _stderr);

  g_free (_stderr);

  res = GST_VALIDATE_EXECUTE_ACTION_OK;

done:
  if (argv)
    g_strfreev (argv);
  g_clear_object (&subproc_launcher);
  g_clear_object (&subproc);

  return res;
}

static GstValidateExecuteActionReturn
_execute_check_pad_caps (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GList *elements = NULL;
  GstPad *pad = NULL;
  GstStructure *expected_struct = NULL;
  GstCaps *expected = NULL, *current_caps = NULL;
  const gchar *pad_name, *comparison_type =
      gst_structure_get_string (action->structure, "comparision-mode");

  DECLARE_AND_GET_PIPELINE (scenario, action);

  REPORT_UNLESS (elements =
      _find_elements_defined_in_action (scenario, action), done,
      "Could not find any element from %" GST_PTR_FORMAT, action->structure);

  REPORT_UNLESS (g_list_length (elements) == 1, done,
      "More than one element found from %" GST_PTR_FORMAT, action->structure);

  pad_name = gst_structure_get_string (action->structure, "pad");
  REPORT_UNLESS (pad =
      gst_element_get_static_pad (elements->data, pad_name), done,
      "Could not find pad %s in %" GST_PTR_FORMAT, pad_name, elements->data);

  current_caps = gst_pad_get_current_caps (pad);
  if (gst_structure_get (action->structure, "expected-caps", GST_TYPE_STRUCTURE,
          &expected_struct, NULL))
    expected = gst_caps_new_full (gst_structure_copy (expected_struct), NULL);
  else
    gst_structure_get (action->structure, "expected-caps", GST_TYPE_CAPS,
        &expected, NULL);

  if (!comparison_type || !g_strcmp0 (comparison_type, "intersect")) {
    REPORT_UNLESS (expected, done, "Can't intersect with NULL expected caps");
    REPORT_UNLESS (gst_caps_can_intersect (expected, current_caps), done,
        "Caps can't intesect. Expected: \n - %" GST_PTR_FORMAT "\nGot:\n - %"
        GST_PTR_FORMAT, expected, current_caps);
  } else if (!g_strcmp0 (comparison_type, "equal")) {
    REPORT_UNLESS ((expected == NULL && current_caps == NULL)
        || gst_caps_is_equal (expected, current_caps), done,
        "Caps do not match. Expected: %" GST_PTR_FORMAT " got %" GST_PTR_FORMAT,
        expected, current_caps);
  } else {
    REPORT_UNLESS (FALSE, done, "Invalid caps `comparision-type`: '%s'",
        comparison_type);
  }

done:
  g_clear_object (&pipeline);
  g_clear_object (&pad);
  g_list_free_full (elements, gst_object_unref);
  gst_clear_structure (&expected_struct);
  gst_clear_caps (&current_caps);
  gst_clear_caps (&expected);

  return res;

}

static GstValidateExecuteActionReturn
_execute_check_position (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstClockTime expected_pos, pos;

  if (!gst_validate_action_get_clocktime (scenario, action,
          "expected-position", &expected_pos)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not retrieve expected position in: %" GST_PTR_FORMAT,
        action->structure);

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  if (!_get_position (scenario, NULL, &pos)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Could not get pipeline position");

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  if (pos != expected_pos) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Pipeline position doesn't match expectations"
        " got %" GST_TIME_FORMAT " instead of %" GST_TIME_FORMAT,
        GST_TIME_ARGS (pos), GST_TIME_ARGS (expected_pos));

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  return GST_VALIDATE_EXECUTE_ACTION_OK;

}

static GstValidateExecuteActionReturn
_execute_set_or_check_property (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GList *targets, *l;
  const gchar *property;
  const GValue *property_value;
  gboolean ret = GST_VALIDATE_EXECUTE_ACTION_OK;
  gboolean check = gst_structure_has_name (action->structure, "check-property");

  targets = _find_elements_defined_in_action (scenario, action);
  if (!targets) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "No element found for action: %" GST_PTR_FORMAT, action->structure);

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  property = gst_structure_get_string (action->structure, "property-name");
  property_value = gst_structure_get_value (action->structure,
      "property-value");

  for (l = targets; l != NULL; l = g_list_next (l)) {
    if (!check) {
      GstValidateActionReturn tmpres;

      tmpres =
          gst_validate_object_set_property (GST_VALIDATE_REPORTER (scenario),
          G_OBJECT (l->data), property, property_value, action->priv->optional);

      if (!tmpres)
        ret = tmpres;
    } else {
      ret =
          _check_property (scenario, action, l->data, property, property_value);
    }
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

  g_free (str);

  return TRUE;
}

static GstValidateExecuteActionReturn
_execute_emit_signal (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GstElement *target;
  guint n_params = 0;
  GSignalQuery query = { 0, };
  GValue *values = NULL, lparams = { 0, };
  const GValue *params;

  REPORT_UNLESS ((target =
          _get_target_element (scenario, action)), out, "No element found");

  query.signal_name =
      gst_structure_get_string (action->structure, "signal-name");
  query.signal_id = g_signal_lookup (query.signal_name, G_OBJECT_TYPE (target));
  REPORT_UNLESS (query.signal_id != 0, out, "Invalid signal `%s::%s`",
      G_OBJECT_TYPE_NAME (target), query.signal_name);

  g_signal_query (query.signal_id, &query);

  params = gst_structure_get_value (action->structure, "params");
  if (params) {
    if (G_VALUE_HOLDS_STRING (params)) {
      g_value_init (&lparams, GST_TYPE_ARRAY);

      REPORT_UNLESS (gst_value_deserialize (&lparams,
              g_value_get_string (params)), out,
          "\"params\" argument should be a value array or a string deserializable"
          " as value array, got string %s", g_value_get_string (params)
          );
      params = &lparams;
    } else {
      REPORT_UNLESS (GST_VALUE_HOLDS_ARRAY (params), out,
          "\"params\" argument should be a value array, got %s",
          G_VALUE_TYPE_NAME (params));
    }
    n_params = gst_value_array_get_size (params);
  }
  REPORT_UNLESS (query.n_params == (n_params), out,
      "Expected %d `params` got %d", query.n_params, n_params);
  values = g_malloc0 ((n_params + 2) * sizeof (GValue));
  g_value_init (&values[0], G_OBJECT_TYPE (target));
  g_value_take_object (&values[0], target);
  for (gint i = 1; i < n_params + 1; i++) {
    const GValue *param = gst_value_array_get_value (params, i - 1);
    g_value_init (&values[i], query.param_types[i - 1]);

    if (query.param_types[i - 1] == G_TYPE_BYTES
        && G_VALUE_TYPE (param) == G_TYPE_STRING) {
      const gchar *s = g_value_get_string (param);
      g_value_take_boxed (&values[i], g_bytes_new (s, strlen (s)));
    } else {
      REPORT_UNLESS (g_value_transform (param, &values[i]), out,
          "Could not transform param %d from %s to %s", i - 1,
          G_VALUE_TYPE_NAME (param), G_VALUE_TYPE_NAME (&values[i]));
    }
  }

  g_signal_emitv (values, query.signal_id, 0, NULL);

  for (gint i = 0; i < n_params + 1; i++)
    g_value_reset (&values[i]);

  if (G_VALUE_TYPE (&lparams))
    g_value_reset (&lparams);

out:
  return res;

}

typedef struct _ChainWrapperFunctionData ChainWrapperFunctionData;
typedef GstFlowReturn (*ChainWrapperFunction) (GstPad * pad, GstObject * parent,
    GstBuffer * buffer, ChainWrapperFunctionData * data);

struct _ChainWrapperFunctionData
{
  GstPadChainFunction wrapped_chain_func;
  gpointer wrapped_chain_data;
  GDestroyNotify wrapped_chain_notify;
  ChainWrapperFunction wrapper_function;
  gpointer wrapper_function_user_data;

  GMutex actions_lock;
  GList *actions;

};

static void
chain_wrapper_function_free (ChainWrapperFunctionData * data)
{
  g_list_free_full (data->actions, (GDestroyNotify) gst_validate_action_unref);
  g_mutex_clear (&data->actions_lock);
}

static GstFlowReturn
_pad_chain_wrapper (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  ChainWrapperFunctionData *data =
      g_object_get_qdata (G_OBJECT (pad), chain_qdata);

  return data->wrapper_function (pad, parent, buffer,
      g_object_get_qdata (G_OBJECT (pad), chain_qdata));
}

static void
wrap_pad_chain_function (GstPad * pad, ChainWrapperFunction new_function,
    GstValidateAction * action)
{
  ChainWrapperFunctionData *data =
      g_object_get_qdata (G_OBJECT (pad), chain_qdata);

  if (data) {
    g_mutex_lock (&data->actions_lock);
    data->actions = g_list_append (data->actions, action);
    g_mutex_unlock (&data->actions_lock);

    return;
  }

  data = g_new0 (ChainWrapperFunctionData, 1);
  data->actions = g_list_append (data->actions, action);

  g_object_set_qdata_full (G_OBJECT (pad), chain_qdata, data,
      (GDestroyNotify) chain_wrapper_function_free);

  data->wrapped_chain_func = pad->chainfunc;
  data->wrapper_function = new_function;
  pad->chainfunc = _pad_chain_wrapper;
}

static GstFlowReturn
appsrc_push_chain_wrapper (GstPad * pad, GstObject * parent,
    GstBuffer * buffer, ChainWrapperFunctionData * data)
{
  GstValidateAction *action;
  GstValidateScenario *scenario;
  GstFlowReturn ret;

  g_mutex_lock (&data->actions_lock);
  if (data->actions) {
    action = data->actions->data;
    data->actions = g_list_remove (data->actions, action);
    g_mutex_unlock (&data->actions_lock);

    scenario = gst_validate_action_get_scenario (action);
  } else {
    g_mutex_unlock (&data->actions_lock);

    return data->wrapped_chain_func (pad, parent, buffer);
  }

  GST_VALIDATE_SCENARIO_EOS_HANDLING_LOCK (scenario);
  ret = data->wrapped_chain_func (pad, parent, buffer);
  gst_validate_action_set_done (action);
  gst_validate_action_unref (action);
  GST_VALIDATE_SCENARIO_EOS_HANDLING_UNLOCK (scenario);
  g_object_unref (scenario);

  return ret;
}

static gboolean
structure_get_uint64_permissive (const GstStructure * structure,
    const gchar * fieldname, guint64 * dest)
{
  const GValue *original;
  GValue transformed = G_VALUE_INIT;

  original = gst_structure_get_value (structure, fieldname);
  if (!original)
    return FALSE;

  g_value_init (&transformed, G_TYPE_UINT64);
  if (!g_value_transform (original, &transformed))
    return FALSE;

  *dest = g_value_get_uint64 (&transformed);
  g_value_unset (&transformed);
  return TRUE;
}

static gint
_execute_appsrc_push (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstElement *target = NULL;
  gchar *file_name = NULL;
  gchar *file_contents = NULL;
  GError *error = NULL;
  GstBuffer *buffer;
  guint64 offset = 0;
  guint64 size = 0, read;
  gint push_sample_ret;
  gboolean wait;
  GFileInfo *finfo = NULL;
  GFile *f = NULL;
  GstPad *appsrc_pad = NULL;
  GstPad *peer_pad = NULL;
  GInputStream *stream = NULL;
  GstValidateExecuteActionReturn res;
  GstSegment segment;
  GstCaps *caps = NULL;
  GstSample *sample;

  /* We will only wait for the the buffer to be pushed if we are in a state
   * that allows flow of buffers (>=PAUSED). Otherwise the buffer will just
   * be enqueued. */
  wait = scenario->priv->target_state >= GST_STATE_PAUSED;

  target = _get_target_element (scenario, action);
  REPORT_UNLESS (target, err, "No element found.");
  file_name =
      g_strdup (gst_structure_get_string (action->structure, "file-name"));
  REPORT_UNLESS (file_name, err, "Missing file-name property.");

  structure_get_uint64_permissive (action->structure, "offset", &offset);
  structure_get_uint64_permissive (action->structure, "size", &size);

  f = g_file_new_for_path (file_name);
  stream = G_INPUT_STREAM (g_file_read (f, NULL, &error));
  REPORT_UNLESS (!error, err, "Could not open file for action. Error: %s",
      error->message);

  if (offset > 0) {
    read = g_input_stream_skip (stream, offset, NULL, &error);
    REPORT_UNLESS (!error, err, "Could not skip to offset. Error: %s",
        error->message);
    REPORT_UNLESS (read == offset, err,
        "Could not skip to offset, only skipped: %" G_GUINT64_FORMAT, read);
  }

  if (size <= 0) {
    finfo =
        g_file_query_info (f, G_FILE_ATTRIBUTE_STANDARD_SIZE,
        G_FILE_QUERY_INFO_NONE, NULL, &error);

    REPORT_UNLESS (!error, err, "Could not query file size. Error: %s",
        error->message);
    size = g_file_info_get_size (finfo);
  }

  file_contents = g_malloc (size);
  read = g_input_stream_read (stream, file_contents, size, NULL, &error);
  REPORT_UNLESS (!error, err, "Could not read input file. Error: %s",
      error->message);
  REPORT_UNLESS (read == size, err,
      "Could read enough data, only read: %" G_GUINT64_FORMAT, read);

  buffer = gst_buffer_new_wrapped (file_contents, size);
  file_contents = NULL;
  gst_validate_action_get_clocktime (scenario,
      action, "pts", &GST_BUFFER_PTS (buffer)
      );
  gst_validate_action_get_clocktime (scenario,
      action, "dts", &GST_BUFFER_DTS (buffer)
      );
  gst_validate_action_get_clocktime (scenario,
      action, "duration", &GST_BUFFER_DURATION (buffer)
      );

  {
    const GValue *caps_value;
    caps_value = gst_structure_get_value (action->structure, "caps");
    if (caps_value) {
      if (G_VALUE_HOLDS_STRING (caps_value)) {
        caps = gst_caps_from_string (g_value_get_string (caps_value));
        REPORT_UNLESS (caps, err, "Invalid caps string: %s",
            g_value_get_string (caps_value));
      } else {
        caps = gst_caps_copy (gst_value_get_caps (caps_value));
      }

      REPORT_UNLESS (caps, err, "Could not get caps value");
    }
  }

  /* We temporarily override the peer pad chain function to finish the action
   * once the buffer chain actually ends. */
  appsrc_pad = gst_element_get_static_pad (target, "src");
  peer_pad = gst_pad_get_peer (appsrc_pad);
  REPORT_UNLESS (peer_pad, err, "Action failed, pad not linked");

  wrap_pad_chain_function (peer_pad, appsrc_push_chain_wrapper, action);

  /* Keep the action alive until set done is called. */
  gst_validate_action_ref (action);

  sample = gst_sample_new (buffer, caps, NULL, NULL);
  gst_clear_caps (&caps);
  gst_buffer_unref (buffer);
  if (gst_structure_has_field (action->structure, "segment")) {
    GstFormat format;
    GstStructure *segment_struct;
    GstClockTime tmp;

    REPORT_UNLESS (gst_structure_get (action->structure, "segment",
            GST_TYPE_STRUCTURE, &segment_struct, NULL), err,
        "Segment field not in right format (expected GstStructure).");

    if (!gst_structure_get (segment_struct, "format", GST_TYPE_FORMAT, &format,
            NULL))
      g_object_get (target, "format", &format, NULL);

    gst_segment_init (&segment, format);
    if (gst_validate_utils_get_clocktime (segment_struct, "base", &tmp))
      segment.base = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "offset", &tmp))
      segment.offset = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "time", &tmp))
      segment.time = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "position", &tmp))
      segment.position = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "duration", &tmp))
      segment.duration = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "start", &tmp))
      segment.start = tmp;
    if (gst_validate_utils_get_clocktime (segment_struct, "stop", &tmp))
      segment.stop = tmp;
    gst_structure_get_double (segment_struct, "rate", &segment.rate);

    gst_structure_free (segment_struct);

    gst_sample_set_segment (sample, &segment);
  }

  g_signal_emit_by_name (target, "push-sample", sample, &push_sample_ret);
  gst_sample_unref (sample);
  REPORT_UNLESS (push_sample_ret == GST_FLOW_OK, err,
      "push-buffer signal failed in action.");

  if (wait) {
    res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  } else {
    gst_validate_printf (NULL,
        "Pipeline is not ready to push buffers, interlacing appsrc-push action...\n");
    res = GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING;
  }
done:
  gst_clear_object (&target);
  gst_clear_object (&appsrc_pad);
  gst_clear_object (&peer_pad);
  g_clear_pointer (&file_name, g_free);
  g_clear_pointer (&file_contents, g_free);
  g_clear_error (&error);
  g_clear_object (&f);
  g_clear_object (&finfo);
  g_clear_object (&stream);

  return res;

err:
  res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  goto done;
}

static gint
_execute_appsrc_eos (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstElement *target;
  gint eos_ret;

  target = _get_target_element (scenario, action);
  if (target == NULL) {
    gchar *structure_string = gst_structure_to_string (action->structure);
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "No element found for action: %s",
        structure_string);
    g_free (structure_string);
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  g_signal_emit_by_name (target, "end-of-stream", &eos_ret);
  if (eos_ret != GST_FLOW_OK) {
    gchar *structure_string = gst_structure_to_string (action->structure);
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Failed to emit end-of-stream signal for action: %s", structure_string);
    g_free (structure_string);
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  gst_object_unref (target);
  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static gint
_execute_flush (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstElement *target;
  GstEvent *event;
  gboolean reset_time = TRUE;

  target = _get_target_element (scenario, action);
  if (target == NULL) {
    gchar *structure_string = gst_structure_to_string (action->structure);
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "No element found for action: %s",
        structure_string);
    g_free (structure_string);
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  gst_structure_get_boolean (action->structure, "reset-time", &reset_time);

  event = gst_event_new_flush_start ();
  if (!gst_element_send_event (target, event)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "FLUSH_START event was not handled");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  event = gst_event_new_flush_stop (reset_time);
  if (!gst_element_send_event (target, event)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "FLUSH_STOP event was not handled");
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  return GST_VALIDATE_EXECUTE_ACTION_OK;
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
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Could not find plugin to disable: %s",
        plugin_name);

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  gst_validate_printf (action, "Disabling plugin \"%s\"\n", plugin_name);
  gst_registry_remove_plugin (gst_registry_get (), plugin);

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static gboolean
gst_validate_action_setup_repeat (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gchar *repeat_expr;
  gchar *error = NULL;
  gint repeat, position, i;

  if (!gst_structure_has_field (action->structure, "repeat"))
    return TRUE;

  if (gst_structure_get_int (action->structure, "repeat", &repeat))
    goto done;

  if (gst_structure_get_double (action->structure, "repeat",
          (gdouble *) & repeat))
    goto done;

  repeat_expr = gst_validate_replace_variables_in_string (action,
      scenario->priv->vars, gst_structure_get_string (action->structure,
          "repeat"), GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_ALL);
  if (!repeat_expr) {
    gst_validate_error_structure (action, "Invalid value for 'repeat'");
    return FALSE;
  }

  repeat = gst_validate_utils_parse_expression (repeat_expr, _set_variable_func,
      scenario, &error);
  if (error) {
    gst_validate_error_structure (action, "Invalid value for 'repeat': %s",
        error);
    g_free (error);
    return FALSE;
  }
  g_free (repeat_expr);

done:
  gst_structure_remove_field (action->structure, "repeat");
  gst_structure_remove_field (action->priv->main_structure, "repeat");

  action->repeat = 0;
  GST_VALIDATE_ACTION_N_REPEATS (action) = repeat;

  position = g_list_index (scenario->priv->actions, action);
  g_assert (position >= 0);
  for (i = 1; i < repeat; i++) {
    GstValidateAction *copy = _action_copy (action);

    copy->repeat = i;
    scenario->priv->actions =
        g_list_insert (scenario->priv->actions, copy, position + i);
  }

  return TRUE;
}

static GstValidateExecuteActionReturn
gst_validate_action_default_prepare_func (GstValidateAction * action)
{
  gint i;
  GstClockTime tmp;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GstValidateActionType *type = gst_validate_get_action_type (action->type);
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);

  _update_well_known_vars (scenario);
  if (!gst_validate_action_setup_repeat (scenario, action))
    goto err;

  if (GST_VALIDATE_ACTION_N_REPEATS (action)) {
    if (action->priv->it_value.g_type != 0) {
      gst_structure_set_value (scenario->priv->vars,
          GST_VALIDATE_ACTION_RANGE_NAME (action), &action->priv->it_value);
    } else {
      gst_structure_set (scenario->priv->vars,
          GST_VALIDATE_ACTION_RANGE_NAME (action) ?
          GST_VALIDATE_ACTION_RANGE_NAME (action) : "repeat", G_TYPE_INT,
          action->repeat, NULL);
    }
  }
  gst_validate_structure_resolve_variables (action, action->structure,
      scenario->priv->vars, GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_ALL);
  for (i = 0; type->parameters[i].name; i++) {
    if (type->parameters[i].types
        && g_str_has_suffix (type->parameters[i].types, "(GstClockTime)"))
      gst_validate_action_get_clocktime (scenario, action,
          type->parameters[i].name, &tmp);
  }


done:
  gst_clear_mini_object ((GstMiniObject **) & type);
  if (scenario)
    gst_object_unref (scenario);

  return res;
err:
  res = GST_VALIDATE_EXECUTE_ACTION_ERROR;
  goto done;
}

static GstValidateExecuteActionReturn
gst_validate_set_property_prepare_func (GstValidateAction * action)
{
  action->priv->optional = gst_structure_has_field_typed (action->structure,
      "on-all-instances", G_TYPE_BOOLEAN);

  return gst_validate_action_default_prepare_func (action);
}

static GList *
add_gvalue_to_list_as_struct (gpointer source, GList * list, const GValue * v)
{
  if (G_VALUE_HOLDS_STRING (v)) {
    GstStructure *structure =
        gst_structure_new_from_string (g_value_get_string (v));

    if (!structure)
      gst_validate_error_structure (source, "Invalid structure: %s",
          g_value_get_string (v));

    return g_list_append (list, structure);
  }

  if (GST_VALUE_HOLDS_STRUCTURE (v))
    return g_list_append (list,
        gst_structure_copy (gst_value_get_structure (v)));


  gst_validate_error_structure (source, "Expected a string or a structure,"
      " got %s instead", gst_value_serialize (v));
  return NULL;
}

static GList *
gst_validate_utils_get_structures (gpointer source,
    GstStructure * str, const gchar * fieldname)
{
  guint i, size;
  GList *res = NULL;
  const GValue *value = gst_structure_get_value (str, fieldname);

  if (!value)
    return NULL;

  if (G_VALUE_HOLDS_STRING (value) || GST_VALUE_HOLDS_STRUCTURE (value))
    return add_gvalue_to_list_as_struct (source, res, value);

  if (!GST_VALUE_HOLDS_LIST (value) && !GST_VALUE_HOLDS_ARRAY (value)) {
    g_error ("%s must have type list of structure/string (or a string), "
        "e.g. %s={ [struct1, a=val1], [struct2, a=val2] }, got: \"%s\" in %s",
        fieldname, fieldname, gst_value_serialize (value),
        gst_structure_to_string (str));
    return NULL;
  }

  size =
      GST_VALUE_HOLDS_LIST (value) ? gst_value_list_get_size (value) :
      gst_value_array_get_size (value);
  for (i = 0; i < size; i++)
    res =
        add_gvalue_to_list_as_struct (source, res,
        GST_VALUE_HOLDS_LIST (value) ?
        gst_value_list_get_value (value, i) :
        gst_value_array_get_value (value, i));

  return res;
}

static GstValidateAction *
gst_validate_create_subaction (GstValidateScenario * scenario,
    GstStructure * lvariables, GstValidateAction * action,
    GstStructure * nstruct, gint it, gint max)
{
  GstValidateAction *subaction;
  GstValidateActionType *action_type =
      _find_action_type (gst_structure_get_name (nstruct));

  if (!action_type)
    gst_validate_error_structure (action,
        "Unknown action type: '%s'", gst_structure_get_name (nstruct));
  subaction = gst_validate_action_new (scenario, action_type, nstruct, FALSE);
  GST_VALIDATE_ACTION_RANGE_NAME (subaction) =
      GST_VALIDATE_ACTION_RANGE_NAME (action);
  GST_VALIDATE_ACTION_FILENAME (subaction) =
      g_strdup (GST_VALIDATE_ACTION_FILENAME (action));
  GST_VALIDATE_ACTION_DEBUG (subaction) =
      g_strdup (GST_VALIDATE_ACTION_DEBUG (action));
  GST_VALIDATE_ACTION_LINENO (subaction) = GST_VALIDATE_ACTION_LINENO (action);
  subaction->repeat = it;
  subaction->priv->subaction_level = action->priv->subaction_level + 1;
  GST_VALIDATE_ACTION_N_REPEATS (subaction) = max;
  gst_validate_structure_resolve_variables (subaction, subaction->structure,
      lvariables,
      GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_LOCAL_ONLY |
      GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_FAILURE |
      GST_VALIDATE_STRUCTURE_RESOLVE_VARIABLES_NO_EXPRESSION);
  gst_structure_free (nstruct);

  return subaction;
}

static GstValidateExecuteActionReturn
gst_validate_foreach_prepare (GstValidateAction * action)
{
  gint it, i;
  gint min = 0, max = 1, step = 1;
  const GValue *it_array = NULL;
  GstValidateScenario *scenario;
  GList *actions, *tmp;

  scenario = gst_validate_action_get_scenario (action);
  g_assert (scenario);
  _update_well_known_vars (scenario);
  gst_validate_action_setup_repeat (scenario, action);

  GST_VALIDATE_ACTION_RANGE_NAME (action) = NULL;
  gst_structure_foreach (action->structure,
      (GstStructureForeachFunc) _foreach_find_iterator, action);

  /* Allow using the repeat field here too */
  if (!GST_VALIDATE_ACTION_RANGE_NAME (action)
      && !GST_VALIDATE_ACTION_N_REPEATS (action))
    gst_validate_error_structure (action, "Missing range specifier field.");

  if (GST_VALIDATE_ACTION_RANGE_NAME (action)) {
    const GValue *it_value = gst_structure_get_value (action->structure,
        GST_VALIDATE_ACTION_RANGE_NAME (action));

    if (GST_VALUE_HOLDS_INT_RANGE (it_value)) {
      min = gst_value_get_int_range_min (it_value);
      max = gst_value_get_int_range_max (it_value);
      step = gst_value_get_int_range_step (it_value);

      if (min % step != 0)
        gst_validate_error_structure (action,
            "Range min[%d] must be a multiple of step[%d].", min, step);

      if (max % step != 0)
        gst_validate_error_structure (action,
            "Range max[%d] must be a multiple of step[%d].", max, step);
    } else {
      it_array = it_value;
      max = gst_value_array_get_size (it_array);
    }
  } else {
    min = action->repeat;
    max = action->repeat + 1;
  }

  actions = gst_validate_utils_get_structures (action, action->structure,
      "actions");
  i = g_list_index (scenario->priv->actions, action);
  for (it = min; it < max; it = it + step) {
    GstStructure *lvariables = gst_structure_new_empty ("vars");
    const GValue *it_value = NULL;

    if (it_array) {
      it_value = gst_value_array_get_value (it_array, it);

      gst_structure_set_value (lvariables,
          GST_VALIDATE_ACTION_RANGE_NAME (action), it_value);

    }

    for (tmp = actions; tmp; tmp = tmp->next) {
      GstValidateAction *subact =
          gst_validate_create_subaction (scenario, lvariables, action,
          gst_structure_copy (tmp->data), it, max);
      scenario->priv->actions =
          g_list_insert (scenario->priv->actions, subact, i++);
      if (it_value) {
        g_value_init (&subact->priv->it_value, G_VALUE_TYPE (it_value));
        g_value_copy (it_value, &subact->priv->it_value);
      }
    }

    gst_structure_free (lvariables);
  }
  g_list_free_full (actions, (GDestroyNotify) gst_structure_free);

  scenario->priv->actions = g_list_remove (scenario->priv->actions, action);
  gst_structure_remove_field (action->structure, "actions");

  gst_object_unref (scenario);
  return GST_VALIDATE_EXECUTE_ACTION_DONE;
}

static gboolean
_check_structure_has_expected_value (GQuark field_id, const GValue * value,
    GstStructure * message_struct)
{
  const GValue *v = gst_structure_id_get_value (message_struct, field_id);

  if (!v) {
    gst_structure_set (message_struct, "__validate_has_expected_values",
        G_TYPE_BOOLEAN, FALSE, NULL);
    return FALSE;
  }

  if (gst_value_compare (value, v) != GST_VALUE_EQUAL) {
    gst_structure_set (message_struct, "__validate_has_expected_values",
        G_TYPE_BOOLEAN, FALSE, NULL);
    return FALSE;
  }

  gst_structure_set (message_struct, "__validate_has_expected_values",
      G_TYPE_BOOLEAN, TRUE, NULL);

  return TRUE;
}

static void
_check_waiting_for_message (GstValidateScenario * scenario,
    GstMessage * message)
{
  GstStructure *expected_values = NULL;
  GstValidateScenarioPrivate *priv = scenario->priv;
  const gchar *message_type;

  if (!priv->wait_message_action) {
    GST_LOG_OBJECT (scenario, "Not waiting for message");
    return;
  }

  message_type = gst_structure_get_string (priv->wait_message_action->structure,
      "message-type");

  if (g_strcmp0 (message_type, GST_MESSAGE_TYPE_NAME (message)))
    return;

  GST_LOG_OBJECT (scenario, " Waiting for %s and got %s", message_type,
      GST_MESSAGE_TYPE_NAME (message));

  gst_structure_get (priv->wait_message_action->structure, "expected-values",
      GST_TYPE_STRUCTURE, &expected_values, NULL);
  if (expected_values) {
    gboolean res = FALSE;
    GstStructure *message_struct =
        (GstStructure *) gst_message_get_structure (message);

    message_struct =
        message_struct ? gst_structure_copy (message_struct) : NULL;
    if (!message_struct) {
      GST_DEBUG_OBJECT (scenario,
          "Waiting for %" GST_PTR_FORMAT " but message has no structure.",
          priv->wait_message_action->structure);
      return;
    }

    gst_structure_set (message_struct, "__validate_has_expected_values",
        G_TYPE_BOOLEAN, FALSE, NULL);
    gst_structure_foreach (expected_values,
        (GstStructureForeachFunc) _check_structure_has_expected_value,
        message_struct);

    if (!gst_structure_get_boolean (message_struct,
            "__validate_has_expected_values", &res) || !res) {
      return;
    }
  }

  gst_validate_action_set_done (priv->wait_message_action);
  _add_execute_actions_gsource (scenario);
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

static void
gst_validate_scenario_check_latency (GstValidateScenario * scenario,
    GstElement * pipeline)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstQuery *query;
  GstClockTime min_latency;

  query = gst_query_new_latency ();
  if (!gst_element_query (GST_ELEMENT_CAST (pipeline), query)) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Failed to perform LATENCY query");
    gst_query_unref (query);
    return;
  }

  gst_query_parse_latency (query, NULL, &min_latency, NULL);
  gst_query_unref (query);
  GST_DEBUG_OBJECT (scenario, "Pipeline latency: %" GST_TIME_FORMAT
      " max allowed: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (min_latency), GST_TIME_ARGS (priv->max_latency));

  if (priv->max_latency != GST_CLOCK_TIME_NONE &&
      min_latency > priv->max_latency) {
    GST_VALIDATE_REPORT (scenario, CONFIG_LATENCY_TOO_HIGH,
        "Pipeline latency is too high: %" GST_TIME_FORMAT " (max allowed %"
        GST_TIME_FORMAT ")", GST_TIME_ARGS (min_latency),
        GST_TIME_ARGS (priv->max_latency));
  }
}

static gboolean
gst_validate_scenario_is_flush_seeking (GstValidateScenario * scenario)
{
  GstValidateSeekInformation *seekinfo = scenario->priv->current_seek;

  if (!seekinfo)
    return FALSE;

  if (!(seekinfo->flags & GST_SEEK_FLAG_FLUSH))
    return FALSE;

  return seekinfo->action->priv->state == GST_VALIDATE_EXECUTE_ACTION_ASYNC;
}

static void
gst_validate_scenario_reset (GstValidateScenario * scenario)
{
  /* Reset sink information */
  SCENARIO_LOCK (scenario);
  g_list_foreach (scenario->priv->sinks, (GFunc) _reset_sink_information, NULL);
  /* Reset current seek */
  scenario->priv->current_seek = NULL;
  scenario->priv->current_seqnum = GST_SEQNUM_INVALID;
  SCENARIO_UNLOCK (scenario);
}

typedef struct
{
  GstValidateScenario *scenario;
  GstMessage *message;
} MessageData;

static void
message_data_free (MessageData * d)
{
  gst_message_unref (d->message);
  gst_object_unref (d->scenario);

  g_free (d);
}

static gboolean
handle_bus_message (MessageData * d)
{
  gboolean is_error = FALSE;
  GstMessage *message = d->message;
  GstValidateScenario *scenario = d->scenario;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = gst_validate_scenario_get_pipeline (scenario);

  if (!pipeline) {
    GST_ERROR_OBJECT (scenario, "No pipeline set anymore!");
    return G_SOURCE_REMOVE;
  }

  GST_DEBUG_OBJECT (scenario, "message %" GST_PTR_FORMAT, message);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ASYNC_DONE:
      if (!gst_validate_scenario_is_flush_seeking (scenario) &&
          priv->needs_async_done) {
        priv->needs_async_done = FALSE;
        if (priv->actions && _action_sets_state (priv->actions->data)
            && !priv->changing_state)
          gst_validate_action_set_done (priv->actions->data);
      }

      if (priv->needs_playback_parsing) {
        priv->needs_playback_parsing = FALSE;
        if (!gst_validate_parse_next_action_playback_time (scenario))
          return G_SOURCE_REMOVE;
      }
      _add_execute_actions_gsource (scenario);
      break;
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState old_state, state, pending_state;
      gboolean reached_state;

      if (!pipeline || GST_MESSAGE_SRC (message) != GST_OBJECT (pipeline))
        break;

      gst_message_parse_state_changed (message, &old_state, &state,
          &pending_state);

      reached_state = pending_state == GST_STATE_VOID_PENDING;

      if (old_state == GST_STATE_PAUSED && state == GST_STATE_READY)
        gst_validate_scenario_reset (scenario);

      if (reached_state && gst_validate_scenario_is_flush_seeking (scenario))
        gst_validate_action_set_done (priv->current_seek->action);

      if (priv->changing_state && priv->target_state == state) {
        priv->changing_state = FALSE;
        if (priv->actions && _action_sets_state (priv->actions->data)
            && reached_state)
          gst_validate_action_set_done (priv->actions->data);
      }

      if (old_state == scenario->priv->target_state - 1
          && state == scenario->priv->target_state)
        _add_execute_actions_gsource (scenario);

      /* GstBin only send a new latency message when reaching PLAYING if
       * async-handling=true so check the latency manually. */
      if (state == GST_STATE_PLAYING)
        gst_validate_scenario_check_latency (scenario, pipeline);
      break;
    }
    case GST_MESSAGE_ERROR:
      is_error = TRUE;

      /* Passthrough */
    case GST_MESSAGE_EOS:
    {
      GstValidateAction *stop_action;
      GstValidateActionType *stop_action_type;
      GstStructure *s;

      if (!is_error && priv->ignore_eos) {
        GST_INFO_OBJECT (scenario, "Got EOS but ignoring it!");
        goto done;
      }

      if (is_error && priv->allow_errors) {
        GST_INFO_OBJECT (scenario, "Got error but ignoring it!");
        if (scenario->priv->needs_async_done || scenario->priv->changing_state) {

          if (scenario->priv->actions) {
            GstValidateAction *act =
                gst_validate_action_ref (scenario->priv->actions->data);

            GST_VALIDATE_REPORT_ACTION (scenario, act,
                SCENARIO_ACTION_EXECUTION_ERROR,
                "Error message happened while executing action");
            gst_validate_action_set_done (act);

            gst_validate_action_unref (act);
          }

          scenario->priv->needs_async_done = scenario->priv->changing_state =
              FALSE;
        }
        goto done;
      }

      GST_VALIDATE_SCENARIO_EOS_HANDLING_LOCK (scenario);
      {
        /* gst_validate_action_set_done() does not finish the action
         * immediately. Instead, it posts a task to the main thread to do most
         * of the work in _action_set_done().
         *
         * While the EOS handling lock guarantees that if an action had to call
         * gst_validate_action_set_done() it has done so, it does not guarantee
         * that _action_set_done() has been called.
         *
         * Is it possible that this handler is run before _action_set_done(), so
         * we check at this point for actions that have a pending_set_done and
         * call it before continuing. */
        GList *actions = g_list_copy (priv->actions);
        GList *i;
        for (i = actions; i; i = i->next) {
          GstValidateAction *action = (GstValidateAction *) i->data;
          if (action->priv->pending_set_done)
            _action_set_done (action);
        }
        g_list_free (actions);
      }

      if (!is_error) {
        priv->got_eos = TRUE;
        if (priv->wait_message_action) {

          if (priv->actions && priv->actions->next) {
            GST_DEBUG_OBJECT (scenario,
                "Waiting for a message and got a next action"
                " to execute, letting it a chance!");
            GST_VALIDATE_SCENARIO_EOS_HANDLING_UNLOCK (scenario);
            goto done;
          } else {
            /* Clear current message wait if waiting for EOS */
            _check_waiting_for_message (scenario, message);
          }
        }
      }

      SCENARIO_LOCK (scenario);
      /* Make sure that if there is an ASYNC_DONE in the message queue, we do not
         take it into account */
      g_list_free_full (priv->seeks,
          (GDestroyNotify) gst_validate_seek_information_free);
      priv->seeks = NULL;
      SCENARIO_UNLOCK (scenario);

      GST_DEBUG_OBJECT (scenario, "Got EOS; generate 'stop' action");

      stop_action_type = _find_action_type ("stop");
      s = gst_structure_new ("stop", "generated-after-eos", G_TYPE_BOOLEAN,
          !is_error, "generated-after-error", G_TYPE_BOOLEAN, is_error, NULL);
      stop_action = gst_validate_action_new (scenario, stop_action_type,
          s, FALSE);
      gst_structure_free (s);
      gst_validate_execute_action (stop_action_type, stop_action);
      gst_mini_object_unref (GST_MINI_OBJECT (stop_action));

      GST_VALIDATE_SCENARIO_EOS_HANDLING_UNLOCK (scenario);
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
        GstValidateScenario *scenario =
            gst_validate_action_get_scenario (priv->pending_switch_track);

        expected =
            gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST
            (priv->pending_switch_track), ACTION_EXPECTED_STREAM_QUARK);

        if (g_list_length (expected) != g_list_length (streams_selected)) {
          GST_VALIDATE_REPORT_ACTION (scenario, priv->pending_switch_track,
              SCENARIO_ACTION_EXECUTION_ERROR,
              "Was expecting %d selected streams but got %d",
              g_list_length (expected), g_list_length (streams_selected));
          goto action_done;
        }

        for (l = expected; l; l = g_list_next (l)) {
          const gchar *stream_id = l->data;

          if (!streams_list_contain (streams_selected, stream_id)) {
            GST_VALIDATE_REPORT_ACTION (scenario, priv->pending_switch_track,
                SCENARIO_ACTION_EXECUTION_ERROR,
                "Stream %s has not be activated", stream_id);
            goto action_done;
          }
        }

      action_done:
        gst_object_unref (scenario);
        gst_validate_action_set_done (priv->pending_switch_track);
        priv->pending_switch_track = NULL;
      }

      g_list_free_full (streams_selected, gst_object_unref);
      break;
    }
    case GST_MESSAGE_LATENCY:
      gst_validate_scenario_check_latency (scenario, pipeline);
      break;

    case GST_MESSAGE_QOS:
    {
      guint64 dropped;

      /* Check the maximum allowed when scenario is terminating so the report
       * will include the actual number of dropped buffers. */
      gst_message_parse_qos_stats (message, NULL, NULL, &dropped);
      if (dropped != -1)
        priv->dropped = dropped;
      break;
    }
    case GST_MESSAGE_APPLICATION:
    {
      const GstStructure *s;
      s = gst_message_get_structure (message);
      if (gst_structure_has_name (s, "validate-segment")) {
        GstValidateSinkInformation *sink_info;

        SCENARIO_LOCK (scenario);
        sink_info =
            _find_sink_information (scenario,
            (GstElement *) GST_MESSAGE_SRC (message));

        if (sink_info) {
          const GValue *segment_value;
          const GstSegment *segment;

          GST_DEBUG_OBJECT (scenario, "Got segment update for %s",
              GST_ELEMENT_NAME (sink_info->sink));
          sink_info->segment_seqnum = GST_MESSAGE_SEQNUM (message);
          segment_value = gst_structure_get_value (s, "segment");
          g_assert (segment_value != NULL);
          segment = (const GstSegment *) g_value_get_boxed (segment_value);
          gst_segment_copy_into (segment, &sink_info->segment);
          _validate_sink_information (scenario);
        }
        SCENARIO_UNLOCK (scenario);
      }
    }
    default:
      break;
  }

done:
  gst_object_unref (pipeline);
  /* Check if we got the message expected by a wait action */
  _check_waiting_for_message (scenario, message);

  execute_next_action_full (scenario, message);

  return G_SOURCE_REMOVE;
}

static void
message_cb (GstBus * bus, GstMessage * message, GstValidateScenario * scenario)
{
  MessageData *d = g_new0 (MessageData, 1);

  d->message = gst_message_ref (message);
  d->scenario = gst_object_ref (scenario);

  g_main_context_invoke_full (scenario->priv->context,
      G_PRIORITY_DEFAULT_IDLE,
      (GSourceFunc) handle_bus_message, d, (GDestroyNotify) message_data_free);
}

static gboolean
_action_type_has_parameter (GstValidateActionType * atype,
    const gchar * paramname)
{
  gint i;

  if (!atype->parameters)
    return FALSE;

  for (i = 0; atype->parameters[i].name; i++)
    if (g_strcmp0 (atype->parameters[i].name, paramname) == 0)
      return TRUE;

  return FALSE;
}

static gboolean
gst_validate_scenario_load_structures (GstValidateScenario * scenario,
    GList * structures, gboolean * is_config, gchar * origin_file)
{
  gboolean ret = TRUE;
  GList *tmp;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GList *config;

  *is_config = FALSE;

  if (!structures) {
    GST_INFO_OBJECT (scenario, "No structures provided");
    return FALSE;
  }

  for (tmp = structures; tmp; tmp = tmp->next) {
    GstValidateAction *action;
    GstValidateActionType *action_type;
    const gchar *type;
    gboolean on_clock = FALSE;
    GstStructure *structure = (GstStructure *) tmp->data;

    type = gst_structure_get_name (structure);
    if (!g_strcmp0 (type, "description") || !g_strcmp0 (type, "meta")) {
      const gchar *pipeline_name;

      gst_structure_get_boolean (structure, "is-config", is_config);
      gst_structure_get_boolean (structure, "handles-states",
          &priv->handles_state);
      if (!gst_structure_get_enum (structure, "target-state", GST_TYPE_STATE,
              (gint *) & priv->target_state) && !priv->handles_state) {
        priv->target_state = GST_STATE_PLAYING;
      }

      gst_structure_get_boolean (structure, "ignore-eos", &priv->ignore_eos);
      gst_structure_get_boolean (structure, "allow-errors",
          &priv->allow_errors);
      gst_structure_get_boolean (structure, "actions-on-idle",
          &priv->execute_on_idle);


      pipeline_name = gst_structure_get_string (structure, "pipeline-name");
      if (pipeline_name) {
        g_free (priv->pipeline_name);
        priv->pipeline_name = g_strdup (pipeline_name);
      }

      gst_validate_utils_get_clocktime (structure, "max-latency",
          &priv->max_latency);

      gst_structure_get_int (structure, "max-dropped", &priv->max_dropped);
      scenario->description = gst_structure_copy (structure);

      continue;
    } else if (!(action_type = _find_action_type (type))) {
      if (gst_structure_has_field (structure, "optional-action-type")) {
        GST_INFO_OBJECT (scenario,
            "Action type not found %s but marked as not mandatory", type);
        continue;
      }

      gst_validate_error_structure (structure,
          "Unknown action type: '%s'", type);
      goto failed;
    }

    gst_structure_get_boolean (structure, "on-clock", &on_clock);
    if ((!g_strcmp0 (type, "crank-clock") || on_clock) && !priv->clock)
      priv->clock = GST_TEST_CLOCK (gst_test_clock_new ());

    if (action_type->parameters) {
      guint i;

      for (i = 0; action_type->parameters[i].name; i++) {
        if (action_type->parameters[i].mandatory &&
            gst_structure_has_field (structure,
                action_type->parameters[i].name) == FALSE) {
          gst_validate_error_structure (structure,
              "Mandatory field '%s' not present in structure: %" GST_PTR_FORMAT,
              action_type->parameters[i].name, structure);
          goto failed;
        }
      }
    }

    action = gst_validate_action_new (scenario, action_type, structure, TRUE);
    if (action->priv->state == GST_VALIDATE_EXECUTE_ACTION_ERROR) {
      GST_ERROR_OBJECT (scenario, "Newly created action: %" GST_PTR_FORMAT
          " was in error state", structure);

      goto failed;
    }

    action->action_number = priv->num_actions++;

    if (action->priv->state == GST_VALIDATE_EXECUTE_ACTION_OK) {
      GST_DEBUG_OBJECT (scenario,
          "Unrefing action that has already been executed");
      gst_validate_action_unref (action);
      action = NULL;
    }
  }

  /* max-latency and max-dropped can be overridden using config */
  for (config = gst_validate_plugin_get_config (NULL); config;
      config = g_list_next (config)) {
    GstClockTime max_latency;

    gst_validate_utils_get_clocktime (config->data, "max-latency",
        &max_latency);
    if (GST_CLOCK_TIME_IS_VALID (max_latency))
      priv->max_latency = max_latency;

    gst_structure_get_int (config->data, "max-dropped", &priv->max_dropped);
  }

done:
  g_list_free_full (structures, (GDestroyNotify) gst_structure_free);

  return ret;

failed:
  ret = FALSE;

  goto done;
}

gchar **
gst_validate_scenario_get_include_paths (const gchar * relative_scenario)
{
  gint n;
  gchar **env_scenariodir;
  gchar *scenarios_path = g_strdup (g_getenv ("GST_VALIDATE_SCENARIOS_PATH"));

  if (relative_scenario) {
    gchar *relative_dir = g_path_get_dirname (relative_scenario);
    gchar *tmp_scenarios_path =
        g_strdup_printf ("%s%c%s", scenarios_path, G_SEARCHPATH_SEPARATOR,
        relative_dir);
    g_free (relative_dir);

    g_free (scenarios_path);
    scenarios_path = tmp_scenarios_path;
  }

  env_scenariodir =
      scenarios_path ? g_strsplit (scenarios_path, G_SEARCHPATH_SEPARATOR_S,
      0) : NULL;
  g_free (scenarios_path);

  n = env_scenariodir ? g_strv_length (env_scenariodir) : 0;
  env_scenariodir = g_realloc_n (env_scenariodir, n + 3, sizeof (gchar *));
  env_scenariodir[n] = g_build_filename (g_get_user_data_dir (),
      "gstreamer-" GST_API_VERSION, "validate",
      GST_VALIDATE_SCENARIO_DIRECTORY, NULL);
  env_scenariodir[n + 1] =
      g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION, "validate",
      GST_VALIDATE_SCENARIO_DIRECTORY, NULL);
  env_scenariodir[n + 2] = NULL;

  return env_scenariodir;
}

static gboolean
_load_scenario_file (GstValidateScenario * scenario,
    gchar * scenario_file, gboolean * is_config)
{
  return gst_validate_scenario_load_structures (scenario,
      gst_validate_utils_structs_parse_from_filename (scenario_file,
          (GstValidateGetIncludePathsFunc)
          gst_validate_scenario_get_include_paths, NULL), is_config,
      scenario_file);
}

static gboolean
gst_validate_scenario_load (GstValidateScenario * scenario,
    const gchar * scenario_name)
{
  gchar **scenarios = NULL;
  guint i;
  gboolean found_actions = FALSE, is_config, ret = FALSE;
  gchar **include_paths = gst_validate_scenario_get_include_paths (NULL);

  if (!scenario_name)
    goto invalid_name;

  scenarios = g_strsplit (scenario_name, ":", -1);

  for (i = 0; scenarios[i]; i++) {
    guint include_i;
    gchar *lfilename = NULL, *tldir = NULL, *scenario_file = NULL;

    ret = FALSE;

    /* First check if the scenario name is not a full path to the
     * actual scenario */
    if (g_file_test (scenarios[i], G_FILE_TEST_IS_REGULAR)) {
      GST_DEBUG_OBJECT (scenario, "Scenario: %s is a full path to a scenario. "
          "Trying to load it", scenarios[i]);
      if ((ret = _load_scenario_file (scenario, scenarios[i], &is_config))) {
        scenario_file = scenarios[i];
        goto check_scenario;
      }
    }

    if (g_str_has_suffix (scenarios[i], GST_VALIDATE_SCENARIO_SUFFIX))
      lfilename = g_strdup (scenarios[i]);
    else
      lfilename =
          g_strdup_printf ("%s" GST_VALIDATE_SCENARIO_SUFFIX, scenarios[i]);

    for (include_i = 0; include_paths[include_i]; include_i++) {
      tldir = g_build_filename (include_paths[include_i], lfilename, NULL);
      if ((ret = _load_scenario_file (scenario, tldir, &is_config))) {
        scenario_file = tldir;
        break;
      }
      g_free (tldir);
    }

    if (!ret)
      goto error;

    /* else check scenario */
  check_scenario:
    if (!is_config) {
      gchar *scenario_dir = g_path_get_dirname (scenario_file);
      gchar *scenario_fname = g_path_get_basename (scenario_file);
      gchar **scenario_name =
          g_regex_split_simple ("\\.scenario", scenario_fname, 0, 0);

      gst_structure_set (scenario->priv->vars,
          "SCENARIO_DIR", G_TYPE_STRING, scenario_dir,
          "SCENARIO_NAME", G_TYPE_STRING, scenario_name[0],
          "SCENARIO_PATH", G_TYPE_STRING, scenario_file, NULL);

      g_free (scenario_dir);
      g_free (scenario_fname);
      g_strfreev (scenario_name);
    }

    g_free (tldir);
    g_free (lfilename);

    if (!is_config) {
      if (found_actions == TRUE)
        goto one_actions_scenario_max;
      else
        found_actions = TRUE;
    }
  }

done:

  if (include_paths)
    g_strfreev (include_paths);

  g_strfreev (scenarios);

  if (ret == FALSE)
    gst_validate_abort ("Could not set scenario %s => EXIT\n", scenario_name);

  return ret;

invalid_name:
  {
    GST_ERROR ("Invalid name for scenario '%s'", GST_STR_NULL (scenario_name));
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
      g_value_take_object (value,
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

  object_class->dispose = gst_validate_scenario_dispose;
  object_class->finalize = gst_validate_scenario_finalize;

  object_class->get_property = gst_validate_scenario_get_property;
  object_class->set_property = gst_validate_scenario_set_property;

  g_object_class_install_property (object_class, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  g_object_class_install_property (object_class, PROP_HANDLES_STATE,
      g_param_spec_boolean ("handles-states", "Handles state",
          "True if the application should not handle the first state change. "
          "False if it is application responsibility",
          FALSE, G_PARAM_READABLE));

  g_object_class_install_property (object_class,
      PROP_EXECUTE_ON_IDLE,
      g_param_spec_boolean ("execute-on-idle",
          "Force waiting between actions",
          "Always execute actions on idle and do not chain them to execute as"
          " fast as possible. Setting this property is useful if action"
          " execution can lead to the addition of new sources on the same main"
          " loop as it provides these new GSource a chance to be dispatched"
          " between actions", FALSE, G_PARAM_READWRITE));

  /**
   * GstValidateScenario::done:
   * @scenario: The scenario running
   *
   * Emitted once all actions have been executed
   */
  scenario_signals[DONE] =
      g_signal_new ("done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstValidateScenario::action-done:
   * @scenario: The scenario running
   * @action: The #GstValidateAction that is done running
   *
   * Emitted when an action is done.
   *
   * Since: 1.20
   */
  scenario_signals[ACTION_DONE] =
      g_signal_new ("action-done", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1,
      GST_TYPE_VALIDATE_ACTION);
}

static void
gst_validate_scenario_init (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv =
      gst_validate_scenario_get_instance_private (scenario);

  priv->seek_pos_tol = DEFAULT_SEEK_TOLERANCE;
  priv->segment_start = 0;
  priv->segment_stop = GST_CLOCK_TIME_NONE;
  priv->current_seek = NULL;
  priv->current_seqnum = GST_SEQNUM_INVALID;
  priv->action_execution_interval = 10;
  priv->vars = gst_structure_new_empty ("vars");
  priv->needs_playback_parsing = TRUE;
  g_weak_ref_init (&scenario->priv->ref_pipeline, NULL);
  priv->max_latency = GST_CLOCK_TIME_NONE;
  priv->max_dropped = -1;
  priv->clock = NULL;

  g_mutex_init (&priv->lock);

  scenario->priv->context = g_main_context_get_thread_default ();
  if (!scenario->priv->context)
    scenario->priv->context = g_main_context_default ();
  g_main_context_ref (scenario->priv->context);
}

static void
gst_validate_scenario_dispose (GObject * object)
{
  GstValidateScenarioPrivate *priv = GST_VALIDATE_SCENARIO (object)->priv;

  g_weak_ref_clear (&priv->ref_pipeline);

  if (priv->bus) {
    gst_bus_remove_signal_watch (priv->bus);
    gst_object_unref (priv->bus);
    priv->bus = NULL;
  }

  gst_object_replace ((GstObject **) & priv->clock, NULL);

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->dispose (object);
}

static void
gst_validate_scenario_finalize (GObject * object)
{
  GstValidateScenario *self = GST_VALIDATE_SCENARIO (object);
  GstValidateScenarioPrivate *priv = self->priv;

  /* Because g_object_add_weak_pointer() is used, this MUST be on the
   * main thread. */
  g_assert (g_main_context_acquire (priv->context));
  g_main_context_release (priv->context);

  g_main_context_unref (priv->context);
  priv->context = NULL;

  g_list_free_full (priv->seeks,
      (GDestroyNotify) gst_validate_seek_information_free);
  g_list_free_full (priv->sinks,
      (GDestroyNotify) gst_validate_sink_information_free);
  g_list_free_full (priv->actions, (GDestroyNotify) gst_validate_action_unref);
  g_list_free_full (priv->non_blocking_running_actions,
      (GDestroyNotify) gst_validate_action_unref);
  g_list_free_full (priv->on_addition_actions,
      (GDestroyNotify) gst_validate_action_unref);
  g_free (priv->pipeline_name);
  gst_structure_free (priv->vars);
  if (self->description)
    gst_structure_free (self->description);
  g_mutex_clear (&priv->lock);

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->finalize (object);
}

static void _element_added_cb (GstBin * bin, GstElement * element,
    GstValidateScenario * scenario);
static void _element_removed_cb (GstBin * bin, GstElement * element,
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
  return gst_validate_element_matches_target (element, action->structure);
}

/* Returns TRUE if:
 * * The element has no parent (pipeline)
 * * Or it's a sink*/
static gboolean
_all_parents_are_sink (GstElement * element)
{
  if (GST_OBJECT_PARENT (element) == NULL)
    return TRUE;

  if (!GST_OBJECT_FLAG_IS_SET (element, GST_ELEMENT_FLAG_SINK))
    return FALSE;

  return _all_parents_are_sink ((GstElement *) GST_OBJECT_PARENT (element));
}

static void
_element_removed_cb (GstBin * bin, GstElement * element,
    GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (GST_IS_BASE_SINK (element)) {
    GstValidateSinkInformation *sink_info;
    SCENARIO_LOCK (scenario);
    sink_info = _find_sink_information (scenario, element);
    if (sink_info) {
      GST_DEBUG_OBJECT (scenario, "Removing sink information for %s",
          GST_ELEMENT_NAME (element));
      priv->sinks = g_list_remove (priv->sinks, sink_info);
      gst_validate_sink_information_free (sink_info);
    }
    SCENARIO_UNLOCK (scenario);
  }
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
      if (gst_validate_execute_action (action_type, action)) {
        if (!gst_structure_has_field_typed (action->structure,
                "on-all-instances", G_TYPE_BOOLEAN)) {
          priv->on_addition_actions =
              g_list_remove_link (priv->on_addition_actions, tmp);
          gst_mini_object_unref (GST_MINI_OBJECT (action));
          g_list_free (tmp);
          tmp = priv->on_addition_actions;
        } else {
          tmp = tmp->next;
        }
      } else
        tmp = tmp->next;
    } else
      tmp = tmp->next;
  }

  /* If it's a new GstBaseSink, add to list of sink information */
  if (GST_IS_BASE_SINK (element) && _all_parents_are_sink (element)) {
    GstValidateSinkInformation *sink_info =
        g_new0 (GstValidateSinkInformation, 1);
    GST_DEBUG_OBJECT (scenario, "Adding %s to list of tracked sinks",
        GST_ELEMENT_NAME (element));
    sink_info->sink = gst_object_ref (element);
    priv->sinks = g_list_append (priv->sinks, sink_info);
  }

  SCENARIO_UNLOCK (scenario);

  /* If it's a bin, listen to the child */
  if (GST_IS_BIN (element)) {
    g_signal_connect (element, "element-added", (GCallback) _element_added_cb,
        scenario);
    g_signal_connect (element, "element-removed",
        (GCallback) _element_removed_cb, scenario);
    iterate_children (scenario, GST_BIN (element));
  }
}

static GstValidateScenario *
gst_validate_scenario_new (GstValidateRunner *
    runner, GstElement * pipeline, gchar * scenario_name, GList * structures)
{
  GList *config;
  GstValidateScenario *scenario =
      g_object_new (GST_TYPE_VALIDATE_SCENARIO, "validate-runner",
      runner, NULL);

  g_object_ref_sink (scenario);

  if (structures) {
    gboolean is_config;
    gst_validate_scenario_load_structures (scenario, structures, &is_config,
        scenario_name);
  } else {

    GST_LOG ("Creating scenario %s", scenario_name);
    if (!gst_validate_scenario_load (scenario, scenario_name)) {
      g_object_unref (scenario);

      return NULL;
    }
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

  gst_validate_printf (NULL,
      "**-> Running scenario %s on pipeline %s**\n", scenario_name,
      GST_OBJECT_NAME (pipeline));

  g_weak_ref_init (&scenario->priv->ref_pipeline, pipeline);
  if (scenario->priv->clock) {
    gst_element_set_clock (pipeline, GST_CLOCK_CAST (scenario->priv->clock));
    gst_pipeline_use_clock (GST_PIPELINE (pipeline),
        GST_CLOCK_CAST (scenario->priv->clock));
  }
  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (scenario),
      g_filename_display_basename (scenario_name));

  g_signal_connect (pipeline, "element-added", (GCallback) _element_added_cb,
      scenario);
  g_signal_connect (pipeline, "element-removed",
      (GCallback) _element_removed_cb, scenario);

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
      if (scenario->priv->action_execution_interval > 0)
        scenario->priv->execute_on_idle = TRUE;
      break;
    } else if (gst_structure_get_int (config->data,
            "scenario-action-execution-interval", &interval)) {
      if (interval > 0) {
        scenario->priv->action_execution_interval = (guint) interval;
        scenario->priv->execute_on_idle = TRUE;
        GST_DEBUG_OBJECT (scenario, "Setting action execution interval to %d",
            scenario->priv->action_execution_interval);
        break;
      } else {
        GST_WARNING_OBJECT (scenario, "Interval is negative: %d", interval);
      }
    }
  }

  if (scenario->priv->handles_state) {
    GST_INFO_OBJECT (scenario, "Scenario handles state."
        " Starting the get position source");
    _add_execute_actions_gsource (scenario);
  } else if (scenario->priv->target_state == GST_STATE_NULL) {
    GST_INFO_OBJECT (scenario,
        "Target state is NULL, starting action execution");
    _add_execute_actions_gsource (scenario);
  }

  scenario->priv->overrides =
      gst_validate_override_registry_get_override_for_names
      (gst_validate_override_registry_get (), "scenarios", NULL);

  return scenario;
}

GstValidateScenario *
gst_validate_scenario_from_structs (GstValidateRunner * runner,
    GstElement * pipeline, GList * structures, gchar * origin_file)
{
  g_return_val_if_fail (structures, NULL);

  return gst_validate_scenario_new (runner, pipeline, origin_file, structures);
}

/**
 * gst_validate_scenario_factory_create:
 * @runner: The #GstValidateRunner to use to report issues
 * @pipeline: The pipeline to run the scenario on
 * @scenario_name: The name (or path) of the scenario to run
 *
 * Returns: (transfer full) (nullable): A #GstValidateScenario or NULL
 */
GstValidateScenario *
gst_validate_scenario_factory_create (GstValidateRunner *
    runner, GstElement * pipeline, const gchar * scenario_name)
{
  return gst_validate_scenario_new (runner, pipeline, (gchar *) scenario_name,
      NULL);
}

static gboolean
_add_description (GQuark field_id, const GValue * value, KeyFileGroupName * kfg)
{
  gchar *tmp = gst_value_serialize (value);
  gchar *tmpcompress = g_strcompress (tmp);

  g_key_file_set_string (kfg->kf, kfg->group_name,
      g_quark_to_string (field_id), tmpcompress);

  g_free (tmpcompress);
  g_free (tmp);

  return TRUE;
}

gboolean
gst_validate_scenario_check_and_set_needs_clock_sync (GList * structures,
    GstStructure ** meta)
{
  gboolean needs_clock_sync = FALSE;
  GList *tmp;

  for (tmp = structures; tmp; tmp = tmp->next) {
    GstStructure *_struct = (GstStructure *) tmp->data;
    gboolean is_meta = gst_structure_has_name (_struct, "description")
        || gst_structure_has_name (_struct, "meta");

    if (!is_meta) {
      GstValidateActionType *type =
          _find_action_type (gst_structure_get_name (_struct));

      if (type && type->flags & GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK)
        needs_clock_sync = TRUE;
      continue;
    }

    if (!*meta)
      *meta = gst_structure_copy (_struct);
  }

  if (needs_clock_sync) {
    if (*meta)
      gst_structure_set (*meta, "need-clock-sync", G_TYPE_BOOLEAN, TRUE, NULL);
    else
      *meta = gst_structure_from_string ("description, need-clock-sync=true;",
          NULL);
  }

  return needs_clock_sync;
}

static gboolean
_parse_scenario (GFile * f, GKeyFile * kf)
{
  gboolean ret = FALSE;
  gchar *path = g_file_get_path (f);

  if (g_str_has_suffix (path, GST_VALIDATE_SCENARIO_SUFFIX)
      || g_str_has_suffix (path, GST_VALIDATE_VALIDATE_TEST_SUFFIX)) {
    GstStructure *meta = NULL;
    GList *tmp, *structures = gst_validate_structs_parse_from_gfile (f,
        (GstValidateGetIncludePathsFunc)
        gst_validate_scenario_get_include_paths);

    gst_validate_scenario_check_and_set_needs_clock_sync (structures, &meta);
    for (tmp = structures; tmp; tmp = tmp->next)
      gst_structure_remove_fields (tmp->data, "__lineno__", "__filename__",
          "__debug__", NULL);

    if (meta) {
      KeyFileGroupName kfg;

      kfg.group_name = g_file_get_path (f);
      kfg.kf = kf;

      gst_structure_remove_fields (meta, "__lineno__", "__filename__",
          "__debug__", NULL);
      gst_structure_foreach (meta,
          (GstStructureForeachFunc) _add_description, &kfg);
      gst_structure_free (meta);
      g_free (kfg.group_name);
    } else {
      g_key_file_set_string (kf, path, "noinfo", "nothing");
    }
    g_list_free_full (structures, (GDestroyNotify) gst_structure_free);

    ret = TRUE;
  }

  g_free (path);
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
  g_free (tldir);

  kf = g_key_file_new ();
  if (num_scenarios > 0) {
    gint i;
    GFile *file;

    for (i = 0; i < num_scenarios; i++) {
      file = g_file_new_for_path (scenarios[i]);
      if (!_parse_scenario (file, kf)) {
        GST_ERROR ("Could not parse scenario: %s", scenarios[i]);

        res = 1;
      }
      g_clear_object (&file);
    }

    goto done;
  }

  envvar = g_getenv ("GST_VALIDATE_SCENARIOS_PATH");
  if (envvar)
    env_scenariodir = g_strsplit (envvar, ":", 0);

  _list_scenarios_in_dir (dir, kf);
  g_clear_object (&dir);

  tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
      "validate", GST_VALIDATE_SCENARIO_DIRECTORY, NULL);
  dir = g_file_new_for_path (tldir);
  _list_scenarios_in_dir (dir, kf);
  g_clear_object (&dir);
  g_free (tldir);

  if (env_scenariodir) {
    guint i;
    GFile *subfile;

    for (i = 0; env_scenariodir[i]; i++) {
      subfile = g_file_new_for_path (env_scenariodir[i]);
      _list_scenarios_in_dir (subfile, kf);
      g_clear_object (&subfile);
    }
  }

  /* Hack to make it work within the development environment */
  dir = g_file_new_for_path ("data/scenarios");
  _list_scenarios_in_dir (dir, kf);
  g_clear_object (&dir);

done:
  result = g_key_file_to_data (kf, &datalength, &err);
  gst_validate_printf (NULL, "All scenarios available:\n%s", result);

  if (output_file && !err) {
    if (!g_file_set_contents (output_file, result, datalength, &err)) {
      GST_WARNING ("Error writing to file '%s'", output_file);
    }
  }

  g_free (result);

  if (env_scenariodir)
    g_strfreev (env_scenariodir);

  if (err) {
    GST_WARNING ("Got error '%s' listing scenarios", err->message);
    g_clear_error (&err);

    res = FALSE;
  }
  g_clear_object (&dir);

  g_key_file_free (kf);

  return res;
}

static GstValidateActionReturn
check_last_sample_internal (GstValidateScenario * scenario,
    GstValidateAction * action, GstElement * sink)
{
  GstSample *sample;
  gchar *sum;
  GstBuffer *buffer;
  const gchar *target_sum;
  guint64 frame_number;
  GstValidateExecuteActionReturn res = GST_VALIDATE_EXECUTE_ACTION_OK;
  GstVideoTimeCodeMeta *tc_meta;

  g_object_get (sink, "last-sample", &sample, NULL);
  if (sample == NULL) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not \"check-last-sample\" as %" GST_PTR_FORMAT
        " 'last-sample' property is NULL"
        ". MAKE SURE THE 'enable-last-sample' PROPERTY IS SET TO 'TRUE'!",
        sink);

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  buffer = gst_sample_get_buffer (sample);
  target_sum = gst_structure_get_string (action->structure, "checksum");
  if (target_sum) {
    GstMapInfo map;

    if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Last sample buffer could not be mapped, action can't run.");
      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
      goto done;
    }
    sum = g_compute_checksum_for_data (G_CHECKSUM_SHA1, map.data, map.size);
    gst_buffer_unmap (buffer, &map);

    if (g_strcmp0 (sum, target_sum)) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Last buffer checksum '%s' is different than the expected one: '%s'",
          sum, target_sum);

      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    }
    g_free (sum);

    goto done;
  }

  if (!gst_structure_get_uint64 (action->structure, "timecode-frame-number",
          &frame_number)) {
    gint iframe_number;

    if (!gst_structure_get_int (action->structure, "timecode-frame-number",
            &iframe_number)) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "The 'checksum' or 'time-code-frame-number' parameters of the "
          "`check-last-sample` action type needs to be specified, none found");

      res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
      goto done;
    }

    frame_number = (guint64) iframe_number;
  }

  tc_meta = gst_buffer_get_video_time_code_meta (buffer);
  if (!tc_meta) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not \"check-last-sample\" as the buffer doesn't contain a TimeCode"
        " meta");
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    goto done;
  }

  if (gst_video_time_code_frames_since_daily_jam (&tc_meta->tc) != frame_number) {
    GST_VALIDATE_REPORT (scenario, SCENARIO_ACTION_EXECUTION_ERROR,
        "Last buffer frame number '%" G_GINT64_FORMAT
        "' is different than the expected one: '%" G_GINT64_FORMAT "'",
        gst_video_time_code_frames_since_daily_jam (&tc_meta->tc),
        frame_number);
    res = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

done:
  gst_sample_unref (sample);
  return res;
}

static void
sink_last_sample_notify_cb (GstElement * sink, GParamSpec * arg G_GNUC_UNUSED,
    GstValidateAction * action)
{
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);

  if (!scenario) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "No pipeline anymore, can't check last sample");
    goto done;
  }

  check_last_sample_internal (scenario, action, sink);
  gst_object_unref (scenario);

done:
  g_signal_handlers_disconnect_by_func (sink, sink_last_sample_notify_cb,
      action);
  gst_validate_action_set_done (action);
  gst_validate_action_unref (action);
}

static GstValidateExecuteActionReturn
_check_last_sample_value (GstValidateScenario * scenario,
    GstValidateAction * action, GstElement * sink)
{
  GstSample *sample;

  /* Connect before checking last sample to avoid a race where
   * the sample is set between the time we connect and the time
   * the time we get it */
  g_signal_connect (sink, "notify::last-sample",
      G_CALLBACK (sink_last_sample_notify_cb),
      gst_validate_action_ref (action));

  g_object_get (sink, "last-sample", &sample, NULL);
  if (sample == NULL)
    return GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  gst_sample_unref (sample);
  gst_validate_action_unref (action);

  g_signal_handlers_disconnect_by_func (sink, sink_last_sample_notify_cb,
      action);

  return check_last_sample_internal (scenario, action, sink);
}

static gboolean
_sink_matches_last_sample_specs (GstElement * sink, const gchar * name,
    const gchar * fname, GstCaps * sinkpad_caps)
{
  GstCaps *tmpcaps;
  GstPad *sinkpad;
  GObjectClass *klass = G_OBJECT_GET_CLASS (sink);
  GParamSpec *paramspec = g_object_class_find_property (klass, "last-sample");

  if (!paramspec)
    return FALSE;

  if (paramspec->value_type != GST_TYPE_SAMPLE)
    return FALSE;

  if (!name && !fname && !sinkpad_caps)
    return TRUE;

  if (name && !g_strcmp0 (GST_OBJECT_NAME (sink), name))
    return TRUE;

  if (fname
      && !g_strcmp0 (GST_OBJECT_NAME (gst_element_get_factory (sink)), fname))
    return TRUE;

  if (!sinkpad_caps)
    return FALSE;

  sinkpad = gst_element_get_static_pad (sink, "sink");
  if (!sinkpad)
    return FALSE;

  tmpcaps = gst_pad_get_current_caps (sinkpad);
  if (tmpcaps) {
    gboolean res = gst_caps_can_intersect (tmpcaps, sinkpad_caps);

    GST_DEBUG_OBJECT (sink, "Matches caps: %" GST_PTR_FORMAT, tmpcaps);
    gst_caps_unref (tmpcaps);

    return res;
  } else {
    GST_INFO_OBJECT (sink, "No caps set yet, can't check it.");
  }

  return FALSE;
}

static GstValidateExecuteActionReturn
_execute_check_last_sample (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstIterator *it;
  GValue data = { 0, };
  gboolean done = FALSE;
  GstCaps *caps = NULL;
  GstElement *sink = NULL, *tmpelement;
  const gchar *name = gst_structure_get_string (action->structure, "sink-name"),
      *factory_name =
      gst_structure_get_string (action->structure, "sink-factory-name"),
      *caps_str = gst_structure_get_string (action->structure, "sinkpad-caps");
  DECLARE_AND_GET_PIPELINE (scenario, action);

  if (caps_str) {
    caps = gst_caps_from_string (caps_str);

    g_assert (caps);
  }

  it = gst_bin_iterate_recurse (GST_BIN (pipeline));
  while (!done) {
    switch (gst_iterator_next (it, &data)) {
      case GST_ITERATOR_OK:
        tmpelement = g_value_get_object (&data);
        if (_sink_matches_last_sample_specs (tmpelement, name, factory_name,
                caps)) {
          if (sink) {
            if (!gst_object_has_as_ancestor (GST_OBJECT (tmpelement),
                    GST_OBJECT (sink))) {
              gchar *tmp = gst_structure_to_string (action->structure);

              GST_VALIDATE_REPORT_ACTION (scenario, action,
                  SCENARIO_ACTION_EXECUTION_ERROR,
                  "Could not \"check-last-sample\" as several elements were found "
                  "from describing string: '%s' (%s and %s match)", tmp,
                  GST_OBJECT_NAME (sink), GST_OBJECT_NAME (tmpelement));

              g_free (tmp);
            }

            gst_object_unref (sink);
          }

          sink = gst_object_ref (tmpelement);
        }
        g_value_reset (&data);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        g_clear_object (&sink);
        break;
      case GST_ITERATOR_ERROR:
        /* Fallthrough */
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (it);
  if (caps)
    gst_caps_unref (caps);

  if (!sink) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not \"check-last-sample\" as no sink was found from description: '%"
        GST_PTR_FORMAT "'", action->structure);

    goto error;
  }

  g_clear_object (&pipeline);
  return _check_last_sample_value (scenario, action, sink);

error:
  g_clear_object (&sink);
  g_clear_object (&pipeline);
  return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
}

static GstPadProbeReturn
_check_is_key_unit_cb (GstPad * pad, GstPadProbeInfo * info,
    GstValidateAction * action)
{
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);
  GstClockTime target_running_time = GST_CLOCK_TIME_NONE;
  gint count_bufs = 0;

  gst_validate_action_get_clocktime (scenario, action,
      "running-time", &target_running_time);
  if (GST_IS_EVENT (GST_PAD_PROBE_INFO_DATA (info))) {
    if (gst_video_event_is_force_key_unit (GST_PAD_PROBE_INFO_DATA (info)))
      gst_structure_set (action->structure, "__priv_seen_event", G_TYPE_BOOLEAN,
          TRUE, NULL);
    else if (GST_EVENT_TYPE (info->data) == GST_EVENT_SEGMENT
        && GST_PAD_DIRECTION (pad) == GST_PAD_SRC) {
      const GstSegment *segment = NULL;

      gst_event_parse_segment (info->data, &segment);
      gst_structure_set (action->structure, "__priv_segment", GST_TYPE_SEGMENT,
          segment, NULL);
    }
  } else if (GST_IS_BUFFER (GST_PAD_PROBE_INFO_DATA (info))
      && gst_structure_has_field_typed (action->structure, "__priv_seen_event",
          G_TYPE_BOOLEAN)) {
    GstSegment *segment = NULL;

    if (GST_CLOCK_TIME_IS_VALID (target_running_time)) {
      GstClockTime running_time;

      gst_structure_get (action->structure, "__priv_segment", GST_TYPE_SEGMENT,
          &segment, NULL);
      running_time =
          gst_segment_to_running_time (segment, GST_FORMAT_TIME,
          GST_BUFFER_TIMESTAMP (info->data));

      if (running_time < target_running_time)
        goto done;
    }

    gst_structure_get_int (action->structure, "__priv_count_bufs", &count_bufs);
    if (GST_BUFFER_FLAG_IS_SET (GST_PAD_PROBE_INFO_BUFFER (info),
            GST_BUFFER_FLAG_DELTA_UNIT)) {
      if (count_bufs >= NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE) {
        GST_VALIDATE_REPORT_ACTION (scenario, action,
            SCENARIO_ACTION_EXECUTION_ERROR,
            "Did not receive a key frame after requested one, "
            "at running_time %" GST_TIME_FORMAT " (with a %i "
            "frame tolerance)", GST_TIME_ARGS (target_running_time),
            NOT_KF_AFTER_FORCE_KF_EVT_TOLERANCE);

        gst_validate_action_set_done (action);
        gst_object_unref (scenario);
        return GST_PAD_PROBE_REMOVE;
      }

      gst_structure_set (action->structure, "__priv_count_bufs", G_TYPE_INT,
          count_bufs++, NULL);
    } else {
      GST_INFO_OBJECT (pad,
          "Properly got keyframe after \"force-keyframe\" event "
          "with running_time %" GST_TIME_FORMAT " (latency %d frame(s))",
          GST_TIME_ARGS (target_running_time), count_bufs);

      gst_structure_remove_fields (action->structure, "__priv_count_bufs",
          "__priv_segment", "__priv_seen_event", NULL);
      gst_validate_action_set_done (action);
      gst_object_unref (scenario);
      return GST_PAD_PROBE_REMOVE;
    }
  }
done:
  gst_object_unref (scenario);

  return GST_PAD_PROBE_OK;
}

static GstValidateExecuteActionReturn
_execute_crank_clock (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstClockTime expected_diff, expected_time;
  GstClockTime prev_time =
      gst_clock_get_time (GST_CLOCK (scenario->priv->clock));

  if (!gst_test_clock_crank (scenario->priv->clock)) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR, "Cranking clock failed");

    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  }

  if (gst_validate_action_get_clocktime (scenario, action,
          "expected-elapsed-time", &expected_diff)) {
    GstClockTime elapsed =
        gst_clock_get_time (GST_CLOCK (scenario->priv->clock)) - prev_time;

    if (expected_diff != elapsed) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Elapsed time during test clock cranking different than expected,"
          " waited for %" GST_TIME_FORMAT " instead of the expected %"
          GST_TIME_FORMAT, GST_TIME_ARGS (elapsed),
          GST_TIME_ARGS (expected_diff));

      return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    }
  }

  if (gst_validate_action_get_clocktime (scenario, action, "expected-time",
          &expected_time)) {
    GstClockTime time = gst_clock_get_time (GST_CLOCK (scenario->priv->clock));

    if (expected_time != time) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Clock time after cranking different than expected,"
          " got %" GST_TIME_FORMAT " instead of the expected %" GST_TIME_FORMAT,
          GST_TIME_ARGS (time), GST_TIME_ARGS (expected_time));

      return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
    }
  }

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static gboolean
_execute_request_key_unit (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  guint count = 0;
  gboolean all_headers = FALSE;
  gboolean ret = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
  GstEvent *event = NULL;
  GstQuery *segment_query;
  GList *targets = NULL, *tmp;
  GstElement *video_encoder = NULL;
  GstPad *pad = NULL, *encoder_srcpad = NULL;
  GstClockTime running_time = GST_CLOCK_TIME_NONE;
  GstSegment segment = { 0, };
  const gchar *direction = gst_structure_get_string (action->structure,
      "direction"), *pad_name, *srcpad_name;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  if (gst_structure_get_string (action->structure, "target-element-name")) {
    GstElement *target = _get_target_element (scenario, action);
    if (target == NULL)
      return FALSE;

    targets = g_list_append (targets, target);
  } else {
    if (!gst_structure_get_string (action->structure,
            "target-element-klass") &&
        !gst_structure_get_string (action->structure,
            "target-element-factory-name")) {
      gst_structure_set (action->structure, "target-element-klass",
          G_TYPE_STRING, "Video/Encoder", NULL);
    }

    targets = _get_target_elements_by_klass_or_factory_name (scenario, action);
  }

  if (!targets) {
    GST_VALIDATE_REPORT_ACTION (scenario, action,
        SCENARIO_ACTION_EXECUTION_ERROR,
        "Could not find any element from action: %" GST_PTR_FORMAT,
        action->structure);
    goto fail;
  }

  gst_validate_action_get_clocktime (scenario, action,
      "running-time", &running_time);
  gst_structure_get_boolean (action->structure, "all-headers", &all_headers);
  if (!gst_structure_get_uint (action->structure, "count", &count)) {
    gst_structure_get_int (action->structure, "count", (gint *) & count);
  }
  pad_name = gst_structure_get_string (action->structure, "pad");
  srcpad_name = gst_structure_get_string (action->structure, "srcpad");
  if (!srcpad_name)
    srcpad_name = "src";

  for (tmp = targets; tmp; tmp = tmp->next) {
    video_encoder = tmp->data;
    encoder_srcpad = gst_element_get_static_pad (video_encoder, srcpad_name);
    if (!encoder_srcpad) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR, "Could not find pad %s",
          srcpad_name);

      goto fail;
    }
    if (g_strcmp0 (direction, "upstream") == 0) {
      event = gst_video_event_new_upstream_force_key_unit (running_time,
          all_headers, count);

      pad = gst_element_get_static_pad (video_encoder, srcpad_name);
      if (!pad) {
        GST_VALIDATE_REPORT_ACTION (scenario, action,
            SCENARIO_ACTION_EXECUTION_ERROR, "Could not find pad %s",
            srcpad_name);

        goto fail;
      }
      GST_ERROR_OBJECT (encoder_srcpad, "Sending RequestKeyUnit event");
      gst_pad_add_probe (encoder_srcpad,
          GST_PAD_PROBE_TYPE_EVENT_UPSTREAM,
          (GstPadProbeCallback) _check_is_key_unit_cb,
          gst_validate_action_ref (action),
          (GDestroyNotify) gst_validate_action_unref);
    } else if (g_strcmp0 (direction, "downstream") == 0) {
      GstClockTime timestamp = GST_CLOCK_TIME_NONE,
          stream_time = GST_CLOCK_TIME_NONE;

      if (!pad_name)
        pad_name = "sink";

      pad = gst_element_get_static_pad (video_encoder, pad_name);
      if (!pad) {
        GST_VALIDATE_REPORT_ACTION (scenario, action,
            SCENARIO_ACTION_EXECUTION_ERROR, "Could not find pad %s", pad_name);

        goto fail;
      }

      gst_validate_action_get_clocktime (scenario, action,
          "timestamp", &timestamp);

      gst_validate_action_get_clocktime (scenario, action,
          "stream-time", &stream_time);

      event =
          gst_video_event_new_downstream_force_key_unit (timestamp, stream_time,
          running_time, all_headers, count);

      gst_pad_add_probe (pad,
          GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
          (GstPadProbeCallback) _check_is_key_unit_cb,
          gst_validate_action_ref (action),
          (GDestroyNotify) gst_validate_action_unref);
    } else {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "request keyunit direction %s invalid (should be in"
          " [downstrean, upstream]", direction);

      goto fail;
    }

    gst_validate_printf (action, "Sending a \"force key unit\" event %s\n",
        direction);

    segment_query = gst_query_new_segment (GST_FORMAT_TIME);
    gst_pad_query (encoder_srcpad, segment_query);

    gst_query_parse_segment (segment_query, &(segment.rate),
        &(segment.format), (gint64 *) & (segment.start),
        (gint64 *) & (segment.stop));
    gst_structure_set (action->structure, "__priv_segment", GST_TYPE_SEGMENT,
        &segment, NULL);

    gst_pad_add_probe (encoder_srcpad,
        GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        (GstPadProbeCallback) _check_is_key_unit_cb,
        gst_validate_action_ref (action),
        (GDestroyNotify) gst_validate_action_unref);


    if (!gst_pad_send_event (pad, event)) {
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR,
          "Could not send \"force key unit\" event %s", direction);
      goto fail;
    }

    gst_clear_object (&pad);
    gst_clear_object (&encoder_srcpad);
  }

done:
  g_list_free_full (targets, gst_object_unref);
  gst_clear_object (&pad);
  gst_clear_object (&encoder_srcpad);

  return ret;

fail:
  ret = GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
  goto done;
}

static GstValidateExecuteActionReturn
_execute_stop (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstBus *bus;
  GstValidateScenarioPrivate *priv = scenario->priv;

  DECLARE_AND_GET_PIPELINE (scenario, action);

  bus = gst_element_get_bus (pipeline);
  SCENARIO_LOCK (scenario);
  if (priv->execute_actions_source_id) {
    g_source_remove (priv->execute_actions_source_id);
    priv->execute_actions_source_id = 0;
  }
  if (scenario->priv->actions || scenario->priv->non_blocking_running_actions ||
      scenario->priv->on_addition_actions) {
    guint nb_actions = 0;
    gchar *actions = g_strdup (""), *tmpconcat;
    GList *tmp;
    GList *all_actions = g_list_concat (g_list_concat (scenario->priv->actions,
            scenario->priv->non_blocking_running_actions),
        scenario->priv->on_addition_actions);

    for (tmp = all_actions; tmp; tmp = tmp->next) {
      GstValidateAction *remaining_action = (GstValidateAction *) tmp->data;
      GstValidateActionType *type;

      if (remaining_action == action)
        continue;

      type = _find_action_type (remaining_action->type);

      tmpconcat = actions;

      if (type->flags & GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL ||
          remaining_action->priv->state == GST_VALIDATE_EXECUTE_ACTION_OK ||
          remaining_action->priv->optional) {
        gst_validate_action_unref (remaining_action);

        continue;
      }

      nb_actions++;

      actions = g_strdup_printf ("%s\n%*s- `%s` at %s:%d", actions, 20, "",
          remaining_action->type,
          GST_VALIDATE_ACTION_FILENAME (remaining_action),
          GST_VALIDATE_ACTION_LINENO (remaining_action));
      gst_validate_action_unref (remaining_action);
      g_free (tmpconcat);
    }
    g_list_free (all_actions);
    scenario->priv->actions = NULL;
    scenario->priv->non_blocking_running_actions = NULL;
    scenario->priv->on_addition_actions = NULL;


    if (nb_actions > 0) {
      GstClockTime position = GST_CLOCK_TIME_NONE;

      _get_position (scenario, NULL, &position);
      GST_VALIDATE_REPORT (scenario, SCENARIO_NOT_ENDED,
          "%i actions were not executed: %s (position: %" GST_TIME_FORMAT
          ")", nb_actions, actions, GST_TIME_ARGS (position));
    }
    g_free (actions);
  }
  SCENARIO_UNLOCK (scenario);

  gst_validate_scenario_check_dropped (scenario);

  gst_bus_post (bus,
      gst_message_new_request_state (GST_OBJECT_CAST (scenario),
          GST_STATE_NULL));
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  return TRUE;
}

static gboolean
_action_set_done (GstValidateAction * action)
{
  gchar *repeat_message = NULL;
  JsonBuilder *jbuild;
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);

  if (scenario == NULL || !action->priv->pending_set_done)
    return G_SOURCE_REMOVE;

  action->priv->execution_duration =
      gst_util_get_timestamp () - action->priv->execution_time;

  jbuild = json_builder_new ();
  json_builder_begin_object (jbuild);
  json_builder_set_member_name (jbuild, "type");
  json_builder_add_string_value (jbuild, "action-done");
  json_builder_set_member_name (jbuild, "action-type");
  json_builder_add_string_value (jbuild, action->type);
  json_builder_set_member_name (jbuild, "execution-duration");
  json_builder_add_double_value (jbuild,
      ((gdouble) action->priv->execution_duration / GST_SECOND));
  json_builder_end_object (jbuild);

  gst_validate_send (json_builder_get_root (jbuild));
  g_object_unref (jbuild);

  action->priv->pending_set_done = FALSE;
  switch (action->priv->state) {
    case GST_VALIDATE_EXECUTE_ACTION_ERROR:
      GST_VALIDATE_REPORT_ACTION (scenario, action,
          SCENARIO_ACTION_EXECUTION_ERROR, "Action %s failed", action->type);
    case GST_VALIDATE_EXECUTE_ACTION_ASYNC:
    case GST_VALIDATE_EXECUTE_ACTION_IN_PROGRESS:
    case GST_VALIDATE_EXECUTE_ACTION_NONE:
    case GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED:
    case GST_VALIDATE_EXECUTE_ACTION_OK:
    {
      scenario->priv->actions = g_list_remove (scenario->priv->actions, action);

      _check_scenario_is_done (scenario);

      if (!gst_validate_parse_next_action_playback_time (scenario)) {
        gst_validate_error_structure (scenario->priv->actions ? scenario->
            priv->actions->data : NULL,
            "Could not determine next action playback time!");
      }

      GST_INFO_OBJECT (scenario, "Action %" GST_PTR_FORMAT " is DONE now"
          " executing next", action->structure);

      break;
    }
    case GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING:
      break;
  }

  if (GST_VALIDATE_ACTION_N_REPEATS (action))
    repeat_message =
        g_strdup_printf ("[%d/%d]", action->repeat,
        GST_VALIDATE_ACTION_N_REPEATS (action));

  gst_validate_printf (NULL,
      "%*c⇨ Action `%s` at %s:%d done '%s' %s (duration: %" GST_TIME_FORMAT
      ")\n\n", (action->priv->subaction_level * 2) - 1, ' ',
      gst_structure_get_name (action->priv->main_structure),
      GST_VALIDATE_ACTION_FILENAME (action),
      GST_VALIDATE_ACTION_LINENO (action),
      gst_validate_action_return_get_name (action->priv->state),
      repeat_message ? repeat_message : "",
      GST_TIME_ARGS (action->priv->execution_duration));
  g_free (repeat_message);

  g_signal_emit (scenario, scenario_signals[ACTION_DONE], 0, action);
  if (action->priv->state != GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING)
    /* We took the 'scenario' reference... unreffing it now */
    gst_validate_action_unref (action);

  action->priv->state = GST_VALIDATE_EXECUTE_ACTION_DONE;
  gst_validate_scenario_execute_next_or_restart_looping (scenario);
  gst_object_unref (scenario);
  return G_SOURCE_REMOVE;
}

/* gst_validate_action_set_done:
 * @action: The action that is done executing
 *
 * Sets @action as "done", meaning that the next action can
 * now be executed.
 */
void
gst_validate_action_set_done (GstValidateAction * action)
{
  GMainContext *context = action->priv->context;
  GstValidateScenario *scenario = gst_validate_action_get_scenario (action);

  action->priv->context = NULL;
  if (action->priv->state == GST_VALIDATE_EXECUTE_ACTION_NON_BLOCKING) {
    GList *item = NULL;

    if (scenario) {
      SCENARIO_LOCK (scenario);
      item = g_list_find (scenario->priv->non_blocking_running_actions, action);
      scenario->priv->non_blocking_running_actions =
          g_list_delete_link (scenario->priv->non_blocking_running_actions,
          item);
      SCENARIO_UNLOCK (scenario);
    }

    if (item)
      gst_validate_action_unref (action);
  }

  g_assert (!action->priv->pending_set_done);
  action->priv->pending_set_done = TRUE;

  if (scenario && scenario->priv->wait_message_action == action) {
    gst_validate_action_unref (scenario->priv->wait_message_action);
    scenario->priv->wait_message_action = NULL;
  }
  gst_clear_object (&scenario);

  g_main_context_invoke_full (action->priv->context,
      G_PRIORITY_DEFAULT_IDLE,
      (GSourceFunc) _action_set_done,
      gst_validate_action_ref (action),
      (GDestroyNotify) gst_validate_action_unref);

  if (context)
    g_main_context_unref (context);
}

/**
 * gst_validate_action_get_scenario:
 * @action: The action for which to retrieve the scenario
 *
 * Retrieve the scenario from which @action is executed.
 *
 * Returns: (transfer full) (nullable): The scenario from which the action is being executed.
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
 *                         is registered inside a plugin.
 * @function: (scope notified): The function to be called to execute the action
 * @parameters: (allow-none) (array zero-terminated=1) (element-type GstValidateActionParameter): The #GstValidateActionParameter usable as parameter of the type
 * @description: A description of the new type
 * @flags: The #GstValidateActionTypeFlags to set on the new action type
 *
 * Register a new action type to the action type system. If the action type already
 * exists, it will be overridden by the new definition
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
_free_action_types (GList * _action_types)
{
  g_list_free_full (_action_types, (GDestroyNotify) gst_mini_object_unref);
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
 * @parameters: (allow-none) (array zero-terminated=1) (element-type GstValidateActionParameter): The #GstValidateActionParameter usable as parameter of the type
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
 * @num_wanted_types: Length of @wanted_types
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
  gboolean print_all = (num_wanted_types == 1
      && !g_strcmp0 (wanted_types[0], "all"));

  if (print_all)
    gst_validate_printf (NULL, "# GstValidate action types");

  for (tmp = gst_validate_list_action_types (); tmp; tmp = tmp->next) {
    GstValidateActionType *atype = (GstValidateActionType *) tmp->data;
    gboolean print = print_all;

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
      gst_validate_printf (atype, "\n");
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

  if (!print_all && num_wanted_types && num_wanted_types > nfound) {
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
      (GCopyFunc) gst_validate_action_ref, NULL);

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
  GList *tmp;

  register_action_types ();

  for (tmp = gst_validate_plugin_get_config (NULL); tmp; tmp = tmp->next) {
    const gchar *action_typename;
    GstStructure *plug_conf = (GstStructure *) tmp->data;

    if ((action_typename = gst_structure_get_string (plug_conf, "action"))) {
      GstValidateAction *action;
      GstValidateActionType *atype = _find_action_type (action_typename);

      if (!atype) {
        gst_validate_error_structure (plug_conf,
            "[CONFIG ERROR] Action type %s not found", action_typename);

        continue;
      }


      if (atype->flags & GST_VALIDATE_ACTION_TYPE_HANDLED_IN_CONFIG) {
        GST_INFO ("Action type %s from configuration files"
            " is handled.", action_typename);
        continue;
      }

      if (!(atype->flags & GST_VALIDATE_ACTION_TYPE_CONFIG) &&
          !(_action_type_has_parameter (atype, "as-config"))) {
        gst_validate_error_structure (plug_conf,
            "[CONFIG ERROR] Action '%s' is not a config action",
            action_typename);

        continue;
      }

      gst_structure_set (plug_conf, "as-config", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_structure_set_name (plug_conf, action_typename);

      action = gst_validate_action_new (NULL, atype, plug_conf, FALSE);
      gst_validate_action_unref (action);
    }
  }
}

void
register_action_types (void)
{
  GstValidateActionType *type;
  GST_DEBUG_CATEGORY_INIT (gst_validate_scenario_debug, "gstvalidatescenario",
      GST_DEBUG_FG_YELLOW, "Gst validate scenarios");

  _gst_validate_action_type = gst_validate_action_get_type ();
  _gst_validate_action_type_type = gst_validate_action_type_get_type ();

  /*  *INDENT-OFF* */
  REGISTER_ACTION_TYPE ("meta", NULL,
      ((GstValidateActionParameter [])  {
      {
        .name = "summary",
        .description = "Whether the scenario is a config only scenario (ie. explain what it does)",
        .mandatory = FALSE,
        .types = "string",
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
        .description = "Whether the scenario executes seek actions or not",
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
        .description = "Whether the scenario needs the execution to be synchronized with the pipeline's\n"
                       "clock. Letting the user know if it can be used with a 'fakesink sync=false' sink",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "true if some action requires a playback-time false otherwise"
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
      {
        .name = "max-latency",
        .description = "The maximum latency in nanoseconds allowed for this pipeline.\n"
          "It can be overridden using core configuration, like for example by defining the "
          "env variable GST_VALIDATE_CONFIG=core,max-latency=33000000",
        .mandatory = FALSE,
        .types = "double, int",
        .possible_variables = NULL,
        .def = "infinite (GST_CLOCK_TIME_NONE)"
      },
      {
        .name = "max-dropped",
        .description = "The maximum number of buffers which can be dropped by the QoS system allowed for this pipeline.\n"
          "It can be overridden using core configuration, like for example by defining the "
          "env variable GST_VALIDATE_CONFIG=core,max-dropped=100",
        .mandatory = FALSE,
        .types = "int",
        .possible_variables = NULL,
        .def = "infinite (-1)"
      },
      {
        .name = "ignore-eos",
        .description = "Ignore EOS and keep executing the scenario when it happens.\n By default "
          "a 'stop' action is generated one EOS",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {
        .name = "allow-errors",
        .description = "Ignore error messages and keep executing the\n"
          "scenario when it happens. By default a 'stop' action is generated on ERROR messages",
        .mandatory = FALSE,
        .types = "boolean",
        .possible_variables = NULL,
        .def = "false"
      },
      {NULL}
      }),
      "Scenario metadata.\nNOTE: it used to be called \"description\"",
      GST_VALIDATE_ACTION_TYPE_CONFIG);

  REGISTER_ACTION_TYPE ("seek", _execute_seek,
      ((GstValidateActionParameter [])  {
        {
          .name = "start",
          .description = "The starting value of the seek",
          .mandatory = TRUE,
          .types = "double or string (GstClockTime)",
          .possible_variables =
            "`position`: The current position in the stream\n"
            "`duration`: The duration of the stream",
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
        {
          .name = "stop",
          .description = "The stop value of the seek",
          .mandatory = FALSE,
          .types = "double or string (GstClockTime)",
          .possible_variables =
            "`position`: The current position in the stream\n"
            "`duration`: The duration of the stream",
          .def ="GST_CLOCK_TIME_NONE",
        },
        {NULL}
      }),
      "Seeks into the stream. This is an example of a seek happening when the stream reaches 5 seconds\n"
      "or 1 eighth of its duration and seeks to 10s or 2 eighths of its duration:\n"
      "  seek, playback-time=\"min(5.0, (duration/8))\", start=\"min(10, 2*(duration/8))\", flags=accurate+flush",
      GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK
  );

  REGISTER_ACTION_TYPE ("pause", _execute_pause,
      ((GstValidateActionParameter []) {
        {
          .name = "duration",
          .description = "The duration during which the stream will be paused",
          .mandatory = FALSE,
          .types = "double or string (GstClockTime)",
          .possible_variables = NULL,
          .def = "0.0",
        },
        {NULL}
      }),
      "Sets pipeline to PAUSED. You can add a 'duration'\n"
      "parameter so the pipeline goes back to playing after that duration\n"
      "(in second)",
      GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK | GST_VALIDATE_ACTION_TYPE_ASYNC);

  REGISTER_ACTION_TYPE ("play", _execute_play, NULL,
      "Sets the pipeline state to PLAYING", GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("stop", _execute_stop, NULL,
      "Stops the execution of the scenario. It will post a 'request-state'"
      " message on the bus with NULL as a requested state"
      " and the application is responsible for stopping itself."
      " If you override that action type, make sure to link up.",
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
      "The 'switch-track' command can be used to switch tracks."
      , GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("wait", _execute_wait,
      ((GstValidateActionParameter []) {
        {
          .name = "duration",
          .description = "the duration while no other action will be executed",
          .mandatory = FALSE,
          .types = "double or string (GstClockTime)",
          NULL},
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to wait @signal-name on.",
          .mandatory = FALSE,
          .types = "string"
        },
        {
          .name = "target-element-factory-name",
          .description = "The name factory for which to wait @signal-name on",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "signal-name",
          .description = "The name of the signal to wait for on @target-element-name."
              " To ensure that the signal is executed without blocking while waiting for it"
              " you can set the field 'non-blocking=true'.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "non-blocking",
          .description = "**Only for signals**."
            "Make the action non blocking meaning that next actions will be\n"
            "executed without waiting for the signal to be emitted.",
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
        {
          .name = "expected-values",
          .description = "Expected values in the message structure (valid only when "
            "`message-type`). Example: "
            "wait, on-client=true, message-type=buffering, expected-values=[values, buffer-percent=100]",
          .mandatory = FALSE,
          .types = "structure",
          NULL
        },
        {
          .name = "on-clock",
          .description = "Wait until the test clock gets a new pending entry.\n"
            "See #gst_test_clock_wait_for_next_pending_id.",
          .mandatory = FALSE,
          .types = "boolean",
          NULL
        },
        {
          .name = "check",
          .description = "The check action to execute when non blocking signal is received",
          .mandatory = FALSE,
          .types = "structure",
          NULL
        },
        {NULL}
      }),
      "Waits for signal 'signal-name', message 'message-type', or during 'duration' seconds",
      GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE);

  REGISTER_ACTION_TYPE ("dot-pipeline", _execute_dot_pipeline, NULL,
      "Dots the pipeline (the 'name' property will be used in the dot filename).\n"
      "For more information have a look at the GST_DEBUG_BIN_TO_DOT_FILE documentation.\n"
      "Note that the GST_DEBUG_DUMP_DOT_DIR env variable needs to be set",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("set-rank", _execute_set_rank_or_disable_feature,
      ((GstValidateActionParameter []) {
        {
          .name = "name",
          .description = "The name of a GstFeature or GstPlugin",
          .mandatory = TRUE,
          .types = "string",
          NULL},
        {
          .name = "rank",
          .description = "The GstRank to set on @name",
          .mandatory = TRUE,
          .types = "string, int",
          NULL},
        {NULL}
      }),
      "Changes the ranking of a particular plugin feature(s)",
      GST_VALIDATE_ACTION_TYPE_CONFIG);

  REGISTER_ACTION_TYPE ("remove-feature", _execute_set_rank_or_disable_feature,
      ((GstValidateActionParameter []) {
        {
          .name = "name",
          .description = "The name of a GstFeature or GstPlugin to remove",
          .mandatory = TRUE,
          .types = "string",
          NULL
        },
        {NULL}
      }),
      "Remove a plugin feature(s) or a plugin from the registry",
      GST_VALIDATE_ACTION_TYPE_CONFIG);

  REGISTER_ACTION_TYPE ("set-feature-rank", _execute_set_rank_or_disable_feature,
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

  REGISTER_ACTION_TYPE ("set-vars", _execute_define_vars,
      ((GstValidateActionParameter []) {
        {NULL}
      }),
      "Define vars to be used in other actions.\n"
      "For example you can define vars for buffer checksum"
      " to be used in the \"check-last-sample\" action type as follow:\n\n"
      "```\n"
      " set-vars, frame1=SomeRandomHash1,frame2=Anotherhash...\n"
      " check-last-sample, checksum=frame1\n"
      "```\n",
      GST_VALIDATE_ACTION_TYPE_NONE);

  GST_TYPE_INTERPOLATION_CONTROL_SOURCE;
  GST_TYPE_TRIGGER_CONTROL_SOURCE;
  REGISTER_ACTION_TYPE ("set-timed-value-properties", _set_timed_value_property,
      ((GstValidateActionParameter []) {
        {
          .name = "binding-type",
          .description = "The name of the type of binding to use",
          .types = "string",
          .mandatory = FALSE,
          .def = "direct",
        },
        {
          .name = "source-type",
          .description = "The name of the type of ControlSource to use",
          .types = "string",
          .mandatory = FALSE,
          .def = "GstInterpolationControlSource",
        },
        {
          .name = "interpolation-mode",
          .description = "The name of the GstInterpolationMode to set on the source",
          .types = "string",
          .mandatory = FALSE,
          .def = "linear",
        },
        {
          .name = "timestamp",
          .description = "The timestamp of the keyframe",
          .types = "string or float (GstClockTime)",
          .mandatory = TRUE,
        },
        {NULL}
      }),
        "Sets GstTimedValue on pads on elements properties using GstControlBindings\n"
        "and GstControlSource as defined in the parameters.\n"
        "The properties values to set will be defined as:\n\n"
        "```\n"
        "element-name.padname::property-name=new-value\n"
        "```\n\n"
        "> NOTE: `.padname` is not needed if setting a property on an element\n\n"
        "This action also adds necessary control source/control bindings.\n",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("check-properties", _execute_set_or_check_properties,
      ((GstValidateActionParameter []) {
        {NULL}
      }),
        "Check elements and pads properties values.\n"
        "The properties values to check will be defined as:\n\n"
        "```\n"
        "element-name.padname::property-name\n"
        "```\n\n"
        "> NOTE: `.padname` is not needed if checking an element property\n\n",
      GST_VALIDATE_ACTION_TYPE_CHECK);

  REGISTER_ACTION_TYPE ("set-properties", _execute_set_or_check_properties,
      ((GstValidateActionParameter []) {
        {NULL}
      }),
        "Set elements and pads properties values.\n"
        "The properties values to set will be defined as:\n\n"
        "```\n"
        "    element-name.padname::property-name\n"
        "```\n\n"
        "> NOTE: `.padname` is not needed if set an element property\n\n",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("set-property", _execute_set_or_check_property,
      ((GstValidateActionParameter []) {
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to set a property on",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-factory-name",
          .description = "The name factory for which to set a property on built elements",
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
        {
          .name = "on-all-instances",
          .description = "Whether to set property on all instances matching "
                         "the requirements",
          .mandatory = FALSE,
          .types = "boolean",
          NULL
        },
        {NULL}
      }),
      "Sets a property of an element or klass of elements in the pipeline.\n"
      "Besides property-name and value, either 'target-element-name' or\n"
      "'target-element-klass' needs to be defined",
      GST_VALIDATE_ACTION_TYPE_CAN_EXECUTE_ON_ADDITION |
          GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL |
          GST_VALIDATE_ACTION_TYPE_HANDLED_IN_CONFIG);
  type->prepare = gst_validate_set_property_prepare_func;

  REGISTER_ACTION_TYPE("check-property", _execute_set_or_check_property,
      ((GstValidateActionParameter[]) {
          { .name = "target-element-name",
              .description = "The name of the GstElement to check a property value",
              .mandatory = FALSE,
              .types = "string",
              NULL },
          { .name = "target-element-factory-name",
              .description = "The name factory for which to check a property value on built elements",
              .mandatory = FALSE,
              .types = "string",
              NULL },
          { .name = "target-element-klass",
              .description = "The klass of the GstElements to check a property on",
              .mandatory = FALSE,
              .types = "string",
              NULL },
          { .name = "property-name",
              .description = "The name of the property to set on @target-element-name",
              .mandatory = TRUE,
              .types = "string",
              NULL },
          { .name = "property-value",
              .description = "The expected value of @property-name",
              .mandatory = TRUE,
              .types = "The same type of @property-name",
              NULL },
          { NULL } }),
      "Check the value of property of an element or klass of elements in the pipeline.\n"
      "Besides property-name and value, either 'target-element-name' or\n"
      "'target-element-klass' needs to be defined",
      GST_VALIDATE_ACTION_TYPE_CHECK);

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
        {
          .name = "params",
          .description = "The signal parameters",
          .mandatory = FALSE,
          .types = "ValueArray",
          NULL
        },
        {NULL}
      }),
      "Emits a signal to an element in the pipeline",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("appsrc-push", _execute_appsrc_push,
      ((GstValidateActionParameter [])
      {
        {
          .name = "target-element-name",
          .description = "The name of the appsrc to push data on",
          .mandatory = TRUE,
          .types = "string"
        },
        {
          .name = "file-name",
          .description = "Relative path to a file whose contents will be pushed as a buffer",
          .mandatory = TRUE,
          .types = "string"
        },
        {
          .name = "offset",
          .description = "Offset within the file where the buffer will start",
          .mandatory = FALSE,
          .types = "uint64"
        },
        {
          .name = "size",
          .description = "Number of bytes from the file that will be pushed as a buffer",
          .mandatory = FALSE,
          .types = "uint64"
        },
        {
          .name = "caps",
          .description = "Caps for the buffer to be pushed",
          .mandatory = FALSE,
          .types = "caps"
        },
        {
          .name = "pts",
          .description = "Buffer PTS",
          .mandatory = FALSE,
          .types = "GstClockTime"
        },
        {
          .name = "dts",
          .description = "Buffer DTS",
          .mandatory = FALSE,
          .types = "GstClockTime"
        },
        {
          .name = "duration",
          .description = "Buffer duration",
          .mandatory = FALSE,
          .types = "GstClockTime"
        },
        {
          .name = "segment",
          .description = "The GstSegment to configure as part of the sample",
          .mandatory = FALSE,
          .types = "(GstStructure)segment,"
                      "[start=(GstClockTime)]"
                      "[stop=(GstClockTime)]"
                      "[base=(GstClockTime)]"
                      "[offset=(GstClockTime)]"
                      "[time=(GstClockTime)]"
                      "[postion=(GstClockTime)]"
                      "[duration=(GstClockTime)]"
        },
        {NULL}
      }),
      "Queues a sample in an appsrc. If the pipeline state allows flow of buffers, "
      " the next action is not run until the buffer has been pushed.",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("appsrc-eos", _execute_appsrc_eos,
      ((GstValidateActionParameter [])
      {
        {
          .name = "target-element-name",
          .description = "The name of the appsrc to emit EOS on",
          .mandatory = TRUE,
          .types = "string"
        },
        {NULL}
      }),
      "Queues a EOS event in an appsrc.",
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE ("flush", _execute_flush,
      ((GstValidateActionParameter [])
      {
        {
          .name = "target-element-name",
          .description = "The name of the appsrc to flush on",
          .mandatory = TRUE,
          .types = "string"
        },
        {
          .name = "reset-time",
          .description = "Whether the flush should reset running time",
          .mandatory = FALSE,
          .types = "boolean",
          .def = "TRUE"
        },
        {NULL}
      }),
      "Sends FLUSH_START and FLUSH_STOP events.",
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

  REGISTER_ACTION_TYPE ("check-last-sample", _execute_check_last_sample,
      ((GstValidateActionParameter []) {
        {
          .name = "sink-name",
          .description = "The name of the sink element to check sample on.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "sink-factory-name",
          .description = "The name of the factory of the sink element to check sample on.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "sinkpad-caps",
          .description = "The caps (as string) of the sink to check.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "checksum",
          .description = "The reference checksum of the buffer.",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "timecode-frame-number",
          .description = "The frame number of the buffer as specified on its"
                         " GstVideoTimeCodeMeta",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {NULL}
      }),
      "Checks the last-sample checksum or frame number (set on its "
      " GstVideoTimeCodeMeta) on declared Sink element."
      " This allows checking the checksum of a buffer after a 'seek' or after a"
      " GESTimeline 'commit'"
      " for example",
      GST_VALIDATE_ACTION_TYPE_NON_BLOCKING | GST_VALIDATE_ACTION_TYPE_CHECK);

    REGISTER_ACTION_TYPE ("crank-clock", _execute_crank_clock,
      ((GstValidateActionParameter []) {
        {
          .name = "expected-time",
          .description = "Expected clock time after cranking",
          .mandatory = FALSE,
          .types = "GstClockTime",
          NULL
        },
        {
          .name = "expected-elapsed-time",
          .description = "Check time elapsed during the clock cranking",
          .mandatory = FALSE,
          .types = "GstClockTime",
          NULL
        },
        {NULL}
      }), "Crank the clock, possibly checking how much time was supposed to be waited on the clock"
          " and/or the clock running time after the crank."
          " Using one `crank-clock` action in a scenario implies that the scenario is driving the "
          " clock and a #GstTestClock will be used. The user will need to crank it the number of "
          " time required (using the `repeat` parameter comes handy here).",
        GST_VALIDATE_ACTION_TYPE_NEEDS_CLOCK);

    REGISTER_ACTION_TYPE ("video-request-key-unit", _execute_request_key_unit,
      ((GstValidateActionParameter []) {
        {
          .name = "direction",
          .description = "The direction for the event to travel, should be in\n"
                          "  * [upstream, downstream]",
          .mandatory = TRUE,
          .types = "string",
          NULL
        },
        {
          .name = "running-time",
          .description = "The running_time can be set to request a new key unit at a specific running_time.\n"
                          "If not set, GST_CLOCK_TIME_NONE will be used so upstream elements will produce a new key unit "
                          "as soon as possible.",
          .mandatory = FALSE,
          .types = "double or string",
          .possible_variables = "position: The current position in the stream\n"
            "duration: The duration of the stream",
          NULL
        },
        {
          .name = "all-headers",
          .description = "TRUE to produce headers when starting a new key unit",
          .mandatory = FALSE,
          .def = "FALSE",
          .types = "boolean",
          NULL
        },
        {
          .name = "count",
          .description = "integer that can be used to number key units",
          .mandatory = FALSE,
          .def = "0",
          .types = "int",
          NULL
        },
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to send a send force-key-unit to",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-factory-name",
          .description = "The factory name of the GstElements to send a send force-key-unit to",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-klass",
          .description = "The klass of the GstElements to send a send force-key-unit to",
          .mandatory = FALSE,
          .def = "Video/Encoder",
          .types = "string",
          NULL
        },
        {
          .name = "pad",
          .description = "The name of the GstPad to send a send force-key-unit to",
          .mandatory = FALSE,
          .def = "sink",
          .types = "string",
          NULL
        },
        {
          .name = "srcpad",
          .description = "The name of the GstPad to send a send force-key-unit to",
          .mandatory = FALSE,
          .def = "src",
          .types = "string",
          NULL
        },
        {NULL}
      }),
      "Request a video key unit", FALSE);

  REGISTER_ACTION_TYPE("check-position", _execute_check_position,
      ((GstValidateActionParameter[]) {
          { .name = "expected-position",
              .description = "The expected pipeline position",
              .mandatory = TRUE,
              .types = "GstClockTime",
              NULL },
        {NULL}
      }),
      "Check current pipeline position.\n",
      /* FIXME: Make MT safe so it can be marked as GST_VALIDATE_ACTION_TYPE_CHECK */
      GST_VALIDATE_ACTION_TYPE_NONE);

  REGISTER_ACTION_TYPE("check-current-pad-caps", _execute_check_pad_caps,
      ((GstValidateActionParameter[]) {
        {
           .name = "expected-caps",
           .description = "The expected caps. If not present, expected no caps to be set",
           .mandatory = FALSE,
           .types = "caps,structure",
           NULL
        },
        {
          .name = "target-element-name",
          .description = "The name of the GstElement to send a send force-key-unit to",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-factory-name",
          .description = "The factory name of the GstElements to get pad from",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "target-element-klass",
          .description = "The klass of the GstElements to get pad from",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "pad",
          .description = "The name of the GstPad to get pad from",
          .mandatory = FALSE,
          .types = "string",
          NULL
        },
        {
          .name = "comparison-type",
          .description = "",
          .mandatory = FALSE,
          .types = "string in [intersect, equal]",
          NULL
        },
        {NULL}
      }),
      "Check currently set caps on a particular pad.\n",
      GST_VALIDATE_ACTION_TYPE_NONE | GST_VALIDATE_ACTION_TYPE_CHECK );

  REGISTER_ACTION_TYPE("run-command", _run_command,
      ((GstValidateActionParameter[]) {
          {
            .name = "argv",
            .description = "The subprocess arguments, include the program name itself",
            .mandatory = TRUE,
            .types = "(string){array,}",
            NULL
          },
          {
            .name = "env",
            .description = "Extra environment variables to set",
            .mandatory = FALSE,
            .types = "structure",
            NULL
          },
        {NULL}
      }),
      "Run an external command.\n",
      GST_VALIDATE_ACTION_TYPE_CAN_BE_OPTIONAL);

    REGISTER_ACTION_TYPE("foreach", NULL,
        ((GstValidateActionParameter[]) {
            { .name = "actions",
                .description = "The array of actions to repeat",
                .mandatory = TRUE,
                .types = "{array of [structures]}",
                NULL },
            { NULL } }),
        "Run actions defined in the `actions` array the number of times specified\n"
        "with an iterator parameter passed in. The iterator can be\n"
        "a range like: `i=[start, end, step]` or array of values\n"
        "such as: `values=<value1, value2>`.\n"
        "One and only one iterator field is supported as parameter.",
        GST_VALIDATE_ACTION_TYPE_NONE);
    type->prepare = gst_validate_foreach_prepare;

    /* Internal actions types to test the validate scenario implementation */
    REGISTER_ACTION_TYPE("priv_check-action-type-calls",
      _execute_check_action_type_calls, NULL, NULL, 0);
    REGISTER_ACTION_TYPE("priv_check-subaction-level",
      _execute_check_subaction_level, NULL, NULL, 0);
    /*  *INDENT-ON* */
}

void
gst_validate_scenario_deinit (void)
{
  _free_action_types (action_types);
  action_types = NULL;
}
