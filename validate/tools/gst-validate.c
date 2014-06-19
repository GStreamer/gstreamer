/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate.c - Validate CLI launch line tool
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

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gio/gio.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

static GMainLoop *mainloop;
static GstElement *pipeline;
static gboolean buffering = FALSE;

static gboolean is_live = FALSE;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  g_main_loop_quit (mainloop);

  /* remove signal handler */
  return FALSE;
}
#endif /* G_OS_UNIX */

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.error");

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ASYNC_DONE:
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
        GstState oldstate, newstate, pending;
        gchar *dump_name;
        gchar *state_transition_name;

        gst_message_parse_state_changed (message, &oldstate, &newstate,
            &pending);

        GST_DEBUG ("State changed (old: %s, new: %s, pending: %s)",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate),
            gst_element_state_get_name (pending));

        state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate));
        dump_name = g_strconcat ("ges-launch.", state_transition_name, NULL);


        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);

        g_free (dump_name);
        g_free (state_transition_name);
      }

      break;
    case GST_MESSAGE_WARNING:{
      GError *gerror;
      gchar *debug;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

      /* dump graph on warning */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.warning");

      gst_message_parse_warning (message, &gerror, &debug);
      g_print ("WARNING: from element %s: %s\n", name, gerror->message);
      if (debug)
        g_print ("Additional debug info:\n%s\n", debug);

      g_error_free (gerror);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      if (!buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!buffering) {
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
          buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      GstState state;

      gst_message_parse_request_state (message, &state);

      if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
          && state == GST_STATE_NULL) {
        gst_validate_printf (GST_MESSAGE_SRC (message),
            "State change request NULL, " "quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
_is_playbin_pipeline (int argc, gchar ** argv)
{
  gint i;

  for (i = 0; i < argc; i++) {
    if (g_strcmp0 (argv[i], "playbin") == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
_execute_set_subtitles (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gchar *uri, *fname;
  GFile *tmpfile, *folder;
  const gchar *subtitle_file, *subtitle_dir;

  subtitle_file = gst_structure_get_string (action->structure, "subtitle-file");
  g_return_val_if_fail (subtitle_file != NULL, FALSE);
  subtitle_dir = gst_structure_get_string (action->structure, "subtitle-dir");

  g_object_get (scenario->pipeline, "current-uri", &uri, NULL);
  tmpfile = g_file_new_for_uri (uri);
  g_free (uri);

  folder = g_file_get_parent (tmpfile);

  fname = g_strdup_printf ("%s%s%s%s",
      subtitle_dir ? subtitle_dir : "",
      subtitle_dir ? G_DIR_SEPARATOR_S : "",
      g_file_get_basename (tmpfile), subtitle_file);
  gst_object_unref (tmpfile);

  tmpfile = g_file_get_child (folder, fname);
  g_free (fname);
  gst_object_unref (folder);

  uri = g_file_get_uri (tmpfile);
  g_object_set (scenario->pipeline, "suburi", uri, NULL);
  g_free (uri);

  return TRUE;
}

static gboolean
_execute_switch_track (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gint index, n;
  GstPad *oldpad, *newpad;
  gboolean relative = FALSE, disabling = FALSE;
  const gchar *type, *str_index;

  gint flags, current, tflag;
  gchar *tmp, *current_txt;

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  tflag =
      gst_validate_utils_flags_from_str (g_type_from_name ("GstPlayFlags"),
      type);
  current_txt = g_strdup_printf ("current-%s", type);

  tmp = g_strdup_printf ("n-%s", type);
  g_object_get (scenario->pipeline, "flags", &flags, tmp, &n,
      current_txt, &current, NULL);

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
    index = current + index;
    if (current >= n)
      index = -2;
  }

  if (!disabling) {
    tmp = g_strdup_printf ("get-%s-pad", type);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, current,
        &oldpad);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, index, &newpad);

    gst_validate_printf (action, "Switching to track number: %i,"
        " (from %s:%s to %s:%s)\n", index, GST_DEBUG_PAD_NAME (oldpad),
        GST_DEBUG_PAD_NAME (newpad));
    flags |= tflag;
  } else {
    gst_validate_printf (action, "Disabling track type %s", type);
  }

  g_object_set (scenario->pipeline, "flags", flags, current_txt, index, NULL);
  g_free (current_txt);

  return TRUE;
}

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  const gchar *scenario = NULL, *configs = NULL;
  gboolean list_scenarios = FALSE;
  GstStateChangeReturn sret;
  gchar *output_file = NULL;
  gint ret = 0;

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif
  int rep_err;

  GOptionEntry options[] = {
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it will override the GST_VALIDATE_SCENARIO "
          "environment variable", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the avalaible scenarios that can be run", NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenario",
        NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Let you set a config scenario, the scenario needs to be set as 'config"
          "' you can specify a list of scenario separated by ':'"
          " it will override the GST_VALIDATE_SCENARIO environment variable,",
        NULL},
    {NULL}
  };
  GOptionContext *ctx;
  gchar **argvn;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GstBus *bus;

  g_set_prgname ("gst-validate-" GST_API_VERSION);
  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_set_summary (ctx, "Runs a gst launch pipeline, adding "
      "monitors to it to identify issues in the used elements. At the end"
      " a report will be printed. To view issues as they are created, set"
      " the env var GST_DEBUG=validate:2 and it will be printed "
      "as gstreamer debugging");

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    exit (1);
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      return 1;
    return 0;
  }

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  /* Create the pipeline */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err);
  g_free (argvn);
  if (!pipeline) {
    g_print ("Failed to create pipeline: %s\n",
        err ? err->message : "unknown reason");
    exit (1);
  }
  if (!GST_IS_PIPELINE (pipeline)) {
    GstElement *new_pipeline = gst_pipeline_new ("");

    gst_bin_add (GST_BIN (new_pipeline), pipeline);
    pipeline = new_pipeline;
  }
#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  if (_is_playbin_pipeline (argc, argv + 1)) {
    const gchar *sub_mandatory_fields[] = { "subtitle-file", NULL };

    gst_validate_add_action_type ("set-subtitle", _execute_set_subtitles,
        sub_mandatory_fields,
        "Action to wait set the subtitle file to use on a playbin pipeline. "
        "The subtitles file that will be use should will be specified "
        "relatively to the playbin URI in use thanks to the subtitle-file "
        " action property. You can also specify a folder with subtitle-dir\n"
        "For example if playbin.uri='file://some/uri.mov"
        " and action looks like 'set-subtitle, subtitle-file=en.srt'"
        " the subtitle URI will be set to 'file:///some/uri.mov.en.srt'",
        FALSE);

    /* Overriding default implementation */
    gst_validate_add_action_type ("switch-track", _execute_switch_track, NULL,
        "The 'switch-track' command can be used to switch tracks.\n"
        "The 'type' argument selects which track type to change (can be 'audio', 'video',"
        " or 'text'). The 'index' argument selects which track of this type"
        " to use: it can be either a number, which will be the Nth track of"
        " the given type, or a number with a '+' or '-' prefix, which means"
        " a relative change (eg, '+1' means 'next track', '-1' means 'previous"
        " track'), note that you need to state that it is a string in the scenario file"
        " prefixing it with (string). You can also disable the track type"
        " setting the 'disable' field (to anything)", FALSE);
  }

  runner = gst_validate_runner_new ();
  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  mainloop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      g_print ("Pipeline failed to go to PLAYING state\n");
      ret = -1;
      goto exit;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live.\n");
      is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Prerolling...\r");
      break;
    default:
      break;
  }

  g_print ("Pipeline started\n");
  g_main_loop_run (mainloop);

  rep_err = gst_validate_runner_printf (runner);
  if (ret == 0) {
    ret = rep_err;
    if (rep_err != 0)
      g_print ("Returning %d as error where found", rep_err);
  }

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (runner);
  g_object_unref (monitor);
#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
