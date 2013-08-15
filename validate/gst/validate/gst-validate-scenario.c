/* GStreamer
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gio/gio.h>
#include <string.h>

#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"

#define GST_VALIDATE_SCENARIO_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_VALIDATE_SCENARIO, GstValidateScenarioPrivate))

#define GST_VALIDATE_SCENARIO_SUFFIX ".scenario"
#define GST_VALIDATE_SCENARIO_DIRECTORY "qa-scenario"

#define DEFAULT_SEEK_TOLERANCE (0.1 * GST_SECOND)       /* tolerance seek interval
                                                           TODO make it overridable  */
enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_LAST
};

static void gst_validate_scenario_dispose (GObject * object);
static void gst_validate_scenario_finalize (GObject * object);

G_DEFINE_TYPE_WITH_CODE (GstValidateScenario, gst_validate_scenario,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, NULL));

typedef enum
{
  SCENARIO_ACTION_UNKNOWN = 0,
  SCENARIO_ACTION_SEEK,
  SCENARIO_ACTION_PAUSE,
  SCENARIO_ACTION_EOS,
} ScenarioActionType;

typedef struct _ScenarioAction
{
  ScenarioActionType type;
  gchar *name;
  GstClockTime playback_time;
  guint action_number;          /* The sequential number on which the action should
                                   be executed */
} ScenarioAction;

#define SCENARIO_ACTION(act) ((ScenarioAction *)act)

typedef struct _SeekInfo
{
  ScenarioAction action;

  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType start_type;
  GstClockTime start;
  GstSeekType stop_type;
  GstClockTime stop;

} SeekInfo;

typedef struct _PauseInfo
{
  ScenarioAction action;

  GstClockTime duration;
} PauseInfo;

typedef struct _EosInfo
{
  ScenarioAction action;
} EosInfo;

struct _GstValidateScenarioPrivate
{
  GstElement *pipeline;
  GstValidateRunner *runner;

  GList *actions;
  gint64 seeked_position;       /* last seeked position */
  GstClockTime seek_pos_tol;

  guint num_actions;

  /* markup parser context */
  gboolean in_scenario;
  gboolean in_actions;

  guint get_pos_id;
};

/* Some helper method that are missing iin Json itscenario */
static guint
get_flags_from_string (GType type, const gchar * str_flags)
{
  guint i;
  gint flags = 0;
  GFlagsClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_flags, class->values[i].value_nick)) {
      flags |= class->values[i].value;
    }
  }
  g_type_class_unref (class);

  return flags;
}

static void
get_enum_from_string (GType type, const gchar * str_enum, guint * enum_value)
{
  guint i;
  GEnumClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_enum, class->values[i].value_nick)) {
      *enum_value = class->values[i].value;
      break;
    }
  }

  g_type_class_unref (class);
}

static void
_scenario_action_init (ScenarioAction * act)
{
  act->name = NULL;
  act->playback_time = GST_CLOCK_TIME_NONE;
  act->type = SCENARIO_ACTION_UNKNOWN;
}

static SeekInfo *
_new_seek_info (void)
{
  SeekInfo *info = g_slice_new0 (SeekInfo);

  _scenario_action_init (&info->action);
  info->action.type = SCENARIO_ACTION_SEEK;
  info->rate = 1.0;
  info->format = GST_FORMAT_TIME;
  info->start_type = GST_SEEK_TYPE_SET;
  info->stop_type = GST_SEEK_TYPE_SET;
  info->flags = GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;
  info->start = 0;
  info->stop = GST_CLOCK_TIME_NONE;

  return info;
}

static PauseInfo *
_new_pause_info (void)
{
  PauseInfo *pause = g_slice_new0 (PauseInfo);

  _scenario_action_init (SCENARIO_ACTION (pause));
  pause->action.type = SCENARIO_ACTION_PAUSE;
  pause->duration = 0;

  return pause;
}

static EosInfo *
_new_eos_info (void)
{
  EosInfo *eos = g_slice_new0 (EosInfo);

  _scenario_action_init (SCENARIO_ACTION (eos));
  eos->action.type = SCENARIO_ACTION_EOS;

  return eos;
}

static void
_scenario_action_clear (ScenarioAction * act)
{
  g_free (act->name);
}

static void
_free_seek_info (SeekInfo * info)
{
  _scenario_action_clear (SCENARIO_ACTION (info));
  g_slice_free (SeekInfo, info);
}

static void
_free_pause_info (PauseInfo * info)
{
  _scenario_action_clear (SCENARIO_ACTION (info));
  g_slice_free (PauseInfo, info);
}

static void
_free_eos_info (EosInfo * info)
{
  _scenario_action_clear (SCENARIO_ACTION (info));
  g_slice_free (EosInfo, info);
}

static void
_free_scenario_action (ScenarioAction * act)
{
  switch (act->type) {
    case SCENARIO_ACTION_SEEK:
      _free_seek_info ((SeekInfo *) act);
      break;
    case SCENARIO_ACTION_PAUSE:
      _free_pause_info ((PauseInfo *) act);
      break;
    case SCENARIO_ACTION_EOS:
      _free_eos_info ((EosInfo *) act);
      break;
    default:
      g_assert_not_reached ();
      _scenario_action_clear (act);
      break;
  }
}

static inline void
_parse_seek (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GstValidateScenario * scenario, GError ** error)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const char *format, *rate, *flags, *start_type, *start, *stop_type, *stop;
  const char *playback_time = NULL;

  SeekInfo *info = _new_seek_info ();

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL, "name",
          &info->action.name,
          G_MARKUP_COLLECT_STRING, "playback_time",
          &playback_time,
          G_MARKUP_COLLECT_STRING, "format", &format,
          G_MARKUP_COLLECT_STRING, "rate", &rate,
          G_MARKUP_COLLECT_STRING, "flags", &flags,
          G_MARKUP_COLLECT_STRING, "start_type", &start_type,
          G_MARKUP_COLLECT_STRING, "start", &start,
          G_MARKUP_COLLECT_STRING, "stop_type", &stop_type,
          G_MARKUP_COLLECT_STRING, "stop", &stop, G_MARKUP_COLLECT_INVALID))
    return;

  get_enum_from_string (GST_TYPE_FORMAT, format, &info->format);

  if (playback_time)
    info->action.playback_time = g_ascii_strtoull (playback_time, NULL, 10);
  info->rate = g_ascii_strtod (rate, NULL);
  info->flags = get_flags_from_string (GST_TYPE_SEEK_FLAGS, flags);
  get_enum_from_string (GST_TYPE_SEEK_TYPE, start_type, &info->start_type);
  info->start = g_ascii_strtoull (start, NULL, 10);
  get_enum_from_string (GST_TYPE_SEEK_TYPE, stop_type, &info->stop_type);
  info->stop = g_ascii_strtoull (stop, NULL, 10);
  info->action.action_number = priv->num_actions++;

  priv->actions = g_list_append (priv->actions, info);
}

static inline void
_parse_pause (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GstValidateScenario * scenario, GError ** error)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const char *duration = NULL;
  const char *playback_time = NULL;

  PauseInfo *info = _new_pause_info ();

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL, "name",
          &info->action.name, G_MARKUP_COLLECT_STRING, "playback_time",
          &playback_time, G_MARKUP_COLLECT_STRING, "duration", &duration,
          G_MARKUP_COLLECT_INVALID))
    return;

  if (playback_time)
    info->action.playback_time = g_ascii_strtoull (playback_time, NULL, 10);
  info->duration = g_ascii_strtoull (duration, NULL, 10);

  info->action.action_number = priv->num_actions++;

  priv->actions = g_list_append (priv->actions, info);
}

static inline void
_parse_eos (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    GstValidateScenario * scenario, GError ** error)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const char *playback_time = NULL;

  EosInfo *info = _new_eos_info ();

  if (!g_markup_collect_attributes (element_name, attribute_names,
          attribute_values, error,
          G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL, "name",
          &info->action.name, G_MARKUP_COLLECT_STRING, "playback_time",
          &playback_time, G_MARKUP_COLLECT_INVALID))
    return;

  if (playback_time)
    info->action.playback_time = g_ascii_strtoull (playback_time, NULL, 10);

  info->action.action_number = priv->num_actions++;

  priv->actions = g_list_append (priv->actions, info);
}

static void
_parse_element_start (GMarkupParseContext * context, const gchar * element_name,
    const gchar ** attribute_names, const gchar ** attribute_values,
    gpointer udata, GError ** error)
{
  GstValidateScenario *scenario = udata;
  GstValidateScenarioPrivate *priv =
      GST_VALIDATE_SCENARIO_GET_PRIVATE (scenario);

  if (strcmp (element_name, "scenario") == 0) {
    priv->in_scenario = TRUE;
    return;
  }

  if (priv->in_scenario) {
    if (strcmp (element_name, "actions") == 0) {
      priv->in_actions = TRUE;
      return;
    }

    if (priv->in_actions) {
      if (g_strcmp0 (element_name, "seek") == 0) {
        _parse_seek (context, element_name, attribute_names,
            attribute_values, scenario, error);
      } else if (g_strcmp0 (element_name, "pause") == 0) {
        _parse_pause (context, element_name, attribute_names,
            attribute_values, scenario, error);
      } else if (g_strcmp0 (element_name, "eos") == 0) {
        _parse_eos (context, element_name, attribute_names,
            attribute_values, scenario, error);
      }
    }
  }

}

static void
_parse_element_end (GMarkupParseContext * context, const gchar * element_name,
    gpointer udata, GError ** error)
{
  GstValidateScenario *scenario = udata;
  GstValidateScenarioPrivate *priv =
      GST_VALIDATE_SCENARIO_GET_PRIVATE (scenario);

  if (strcmp (element_name, "actions") == 0) {
    priv->in_actions = FALSE;
  } else if (strcmp (element_name, "scenario") == 0) {
    priv->in_scenario = FALSE;
  }
}

static gboolean
_pause_action_restore_playing (GstValidateScenario * scenario)
{
  GstElement *pipeline = scenario->priv->pipeline;

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, GST_VALIDATE_ISSUE_ID_STATE_CHANGE_FAILURE,
        "Failed to set state to playing");
  }

  return FALSE;
}

static void
_execute_action (GstValidateScenario * scenario, ScenarioAction * act)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = scenario->priv->pipeline;

  if (act->type == SCENARIO_ACTION_SEEK) {
    SeekInfo *seek = (SeekInfo *) act;
    GST_DEBUG ("%s (num %u), seeking to: %" GST_TIME_FORMAT " stop: %"
        GST_TIME_FORMAT " Rate %lf", SCENARIO_ACTION (seek)->name,
        SCENARIO_ACTION (seek)->action_number, GST_TIME_ARGS (seek->start),
        GST_TIME_ARGS (seek->stop), seek->rate);

    priv->seeked_position = (seek->rate > 0) ? seek->start : seek->stop;
    if (gst_element_seek (pipeline, seek->rate,
            seek->format, seek->flags,
            seek->start_type, seek->start,
            seek->stop_type, seek->stop) == FALSE) {
      GST_VALIDATE_REPORT (scenario,
          GST_VALIDATE_ISSUE_ID_EVENT_SEEK_NOT_HANDLED,
          "Could not seek to position %" GST_TIME_FORMAT,
          GST_TIME_ARGS (priv->seeked_position));
    }

  } else if (act->type == SCENARIO_ACTION_PAUSE) {
    PauseInfo *pause = (PauseInfo *) act;

    GST_DEBUG ("Pausing for %" GST_TIME_FORMAT,
        GST_TIME_ARGS (pause->duration));

    if (gst_element_set_state (pipeline, GST_STATE_PAUSED) ==
        GST_STATE_CHANGE_FAILURE) {
      GST_VALIDATE_REPORT (scenario, GST_VALIDATE_ISSUE_ID_STATE_CHANGE_FAILURE,
          "Failed to set state to paused");
    }
    gst_element_get_state (pipeline, NULL, NULL, -1);
    g_timeout_add (pause->duration / GST_MSECOND,
        (GSourceFunc) _pause_action_restore_playing, scenario);
  } else if (act->type == SCENARIO_ACTION_EOS) {
    GST_DEBUG ("Sending eos to pipeline at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (act->playback_time));
    gst_element_send_event (priv->pipeline, gst_event_new_eos ());
  }
}

static gboolean
get_position (GstValidateScenario * scenario)
{
  GList *tmp;
  gint64 position;
  GstFormat format = GST_FORMAT_TIME;
  ScenarioAction *act;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = scenario->priv->pipeline;

  if (scenario->priv->actions == NULL) {
    GST_DEBUG_OBJECT (scenario,
        "No more actions to execute, stop calling  get_position");
    return FALSE;
  }

  act = scenario->priv->actions->data;
  gst_element_query_position (pipeline, format, &position);

  GST_LOG ("Current position: %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  if (((position >= MAX (0,
                  ((gint64) (act->playback_time - priv->seek_pos_tol))))
          && (position <= (act->playback_time + priv->seek_pos_tol)))) {

    /* TODO what about non flushing seeks? */
    /* TODO why is this inside the action time if ? */
    if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position))
      GST_VALIDATE_REPORT (scenario,
          GST_VALIDATE_ISSUE_ID_EVENT_SEEK_NOT_HANDLED,
          "Previous seek to %" GST_TIME_FORMAT " was not handled",
          GST_TIME_ARGS (priv->seeked_position));

    _execute_action (scenario, act);

    tmp = priv->actions;
    priv->actions = g_list_remove_link (priv->actions, tmp);
    _free_scenario_action (act);
    g_list_free (tmp);
  }

  return TRUE;
}

static gboolean
async_done_cb (GstBus * bus, GstMessage * message,
    GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position)) {
    gint64 position;
    GstFormat format = GST_FORMAT_TIME;

    gst_element_query_position (priv->pipeline, format, &position);
    if (position > (priv->seeked_position + priv->seek_pos_tol) ||
        position < (MAX (0,
                ((gint64) (priv->seeked_position - priv->seek_pos_tol))))) {

      GST_VALIDATE_REPORT (scenario,
          GST_VALIDATE_ISSUE_ID_EVENT_SEEK_RESULT_POSITION_WRONG,
          "Seeked position %" GST_TIME_FORMAT "not in the expected range [%"
          GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
          GST_TIME_ARGS (((MAX (0,
                          ((gint64) (priv->seeked_position -
                                  priv->seek_pos_tol)))))),
          GST_TIME_ARGS ((priv->seeked_position + priv->seek_pos_tol)));
    }
    priv->seeked_position = GST_CLOCK_TIME_NONE;
  }

  if (priv->get_pos_id == 0) {
    get_position (scenario);
    priv->get_pos_id = g_timeout_add (50, (GSourceFunc) get_position, scenario);
  }


  return TRUE;
}

static gboolean
_load_scenario_file (GstValidateScenario * scenario,
    const gchar * scenario_file)
{
  gsize xmlsize;
  GFile *file = NULL;
  GError *err = NULL;
  gboolean ret = TRUE;
  gchar *xmlcontent = NULL;
  GMarkupParseContext *parsecontext = NULL;
  GstValidateScenarioClass *self_class =
      GST_VALIDATE_SCENARIO_GET_CLASS (scenario);
  gchar *uri = gst_filename_to_uri (scenario_file, &err);

  if (uri == NULL)
    goto failed;

  GST_DEBUG ("Trying to load %s", scenario_file);
  if ((file = g_file_new_for_path (scenario_file)) == NULL)
    goto wrong_uri;

  /* TODO Handle GCancellable */
  if (!g_file_load_contents (file, NULL, &xmlcontent, &xmlsize, NULL, &err))
    goto failed;

  if (g_strcmp0 (xmlcontent, "") == 0)
    goto failed;

  parsecontext = g_markup_parse_context_new (&self_class->content_parser,
      G_MARKUP_TREAT_CDATA_AS_TEXT, scenario, NULL);

  if (g_markup_parse_context_parse (parsecontext, xmlcontent, xmlsize,
          &err) == FALSE)
    goto failed;

done:
  if (xmlcontent)
    g_free (xmlcontent);

  if (file)
    gst_object_unref (file);

  if (parsecontext) {
    g_markup_parse_context_free (parsecontext);
    parsecontext = NULL;
  }

  return ret;

wrong_uri:
  GST_WARNING ("%s wrong uri", scenario_file);

  ret = FALSE;
  goto done;

failed:
  ret = FALSE;
  if (err) {
    GST_WARNING ("Failed to load contents: %d %s", err->code, err->message);
    g_error_free (err);
  }
  goto done;
}

static gboolean
gst_validate_scenario_load (GstValidateScenario * scenario,
    const gchar * scenario_name)
{
  gboolean ret = TRUE;
  gchar *lfilename = NULL, *tldir = NULL;

  if (!scenario_name)
    goto invalid_name;

  lfilename =
      g_strdup_printf ("%s" GST_VALIDATE_SCENARIO_SUFFIX, scenario_name);

  /* Try from local profiles */
  tldir =
      g_build_filename (g_get_user_data_dir (), "gstreamer-" GST_API_VERSION,
      GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);

  if (!(ret = _load_scenario_file (scenario, tldir))) {
    g_free (tldir);
    /* Try from system-wide profiles */
    tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
        GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

  /* Hack to make it work uninstalled */
  if (ret == FALSE) {
    g_free (tldir);

    tldir = g_build_filename ("data/", lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

done:
  if (tldir)
    g_free (tldir);
  if (lfilename)
    g_free (lfilename);

  return ret;

invalid_name:
  {
    GST_ERROR ("Invalid name for scenario '%s'", scenario_name);
    ret = FALSE;
    goto done;
  }
}


static void
gst_validate_scenario_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    default:
      break;
  }
}

static void
gst_validate_scenario_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
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
      g_param_spec_object ("qa-runner", "VALIDATE Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));

  klass->content_parser.start_element = _parse_element_start;
  klass->content_parser.end_element = _parse_element_end;
}

static void
gst_validate_scenario_init (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv =
      GST_VALIDATE_SCENARIO_GET_PRIVATE (scenario);


  priv->seeked_position = GST_CLOCK_TIME_NONE;
  priv->seek_pos_tol = DEFAULT_SEEK_TOLERANCE;
}

static void
gst_validate_scenario_dispose (GObject * object)
{
  GstValidateScenarioPrivate *priv = GST_VALIDATE_SCENARIO (object)->priv;

  if (priv->pipeline)
    gst_object_unref (priv->pipeline);
  g_list_free_full (priv->actions, (GDestroyNotify) _free_scenario_action);

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->dispose (object);
}

static void
gst_validate_scenario_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->finalize (object);
}

GstValidateScenario *
gst_validate_scenario_factory_create (GstValidateRunner * runner,
    GstElement * pipeline, const gchar * scenario_name)
{
  GstBus *bus;
  GstValidateScenario *scenario =
      g_object_new (GST_TYPE_VALIDATE_SCENARIO, "qa-runner",
      runner, NULL);

  GST_LOG ("Creating scenario %s", scenario_name);
  if (!gst_validate_scenario_load (scenario, scenario_name)) {
    g_object_unref (scenario);

    return NULL;
  }

  scenario->priv->pipeline = gst_object_ref (pipeline);
  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (scenario),
      g_strdup (scenario_name));

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::async-done", (GCallback) async_done_cb,
      scenario);
  gst_object_unref (bus);

  g_print ("\n=========================================\n"
      "Running scenario %s on pipeline %s"
      "\n=========================================\n", scenario_name,
      GST_OBJECT_NAME (pipeline));

  return scenario;
}

static void
_list_scenarios_in_dir (GFile * dir)
{
  GFileEnumerator *fenum;
  GFileInfo *info;

  fenum = g_file_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_NAME,
      G_FILE_QUERY_INFO_NONE, NULL, NULL);

  if (fenum == NULL)
    return;

  for (info = g_file_enumerator_next_file (fenum, NULL, NULL);
      info; info = g_file_enumerator_next_file (fenum, NULL, NULL)) {
    if (g_str_has_suffix (g_file_info_get_name (info),
            GST_VALIDATE_SCENARIO_SUFFIX)) {
      gchar **name = g_strsplit (g_file_info_get_name (info),
          GST_VALIDATE_SCENARIO_SUFFIX, 0);

      g_print ("Scenario %s \n", name[0]);

      g_strfreev (name);
    }
  }
}

void
gst_validate_list_scenarios (void)
{
  gchar *tldir = g_build_filename (g_get_user_data_dir (),
      "gstreamer-" GST_API_VERSION, GST_VALIDATE_SCENARIO_DIRECTORY,
      NULL);
  GFile *dir = g_file_new_for_path (tldir);

  g_print ("====================\n"
      "Avalaible scenarios:\n" "====================\n");
  _list_scenarios_in_dir (dir);
  g_object_unref (dir);
  g_free (tldir);

  /* Hack to make it work uninstalled */
  dir = g_file_new_for_path ("data/");
  _list_scenarios_in_dir (dir);
  g_object_unref (dir);

}
