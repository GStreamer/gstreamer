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
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

static gint ret = 0;
static GMainLoop *mainloop;
static GstElement *pipeline;

static gboolean buffering = FALSE;
static gboolean is_live = FALSE;
static guint print_pos_srcid = 0;

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
print_position (void)
{
  GstQuery *query;
  gint64 position, duration;

  gdouble rate = 1.0;
  GstFormat format = GST_FORMAT_TIME;

  gst_element_query_position (pipeline, format, &position);

  format = GST_FORMAT_TIME;
  gst_element_query_duration (pipeline, format, &duration);

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (pipeline, query))
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);
  gst_query_unref (query);

  g_print ("<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " speed: %f />\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
      rate);

  return TRUE;
}

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;
      ret = -1;
      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s -- Setting returncode to -1\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ASYNC_DONE:
      if (print_pos_srcid == 0)
        print_pos_srcid =
            g_timeout_add (50, (GSourceFunc) print_position, NULL);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
        GstState oldstate, newstate, pending;

        gst_message_parse_state_changed (message, &oldstate, &newstate,
            &pending);

        GST_DEBUG ("State changed (old: %s, new: %s, pending: %s)",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate),
            gst_element_state_get_name (pending));

        if (newstate == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.playing");
        }
      }

      break;
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
          if (print_pos_srcid) {
            if (g_source_remove (print_pos_srcid))
              print_pos_srcid = 0;
          }
          buffering = TRUE;
        }
      }
      break;
    }
    default:
      break;
  }

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
      "the env var GST_DEBUG=validate:2 and it will be printed "
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
    if (gst_validate_list_scenarios (output_file))
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

  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

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

  return ret;
}
