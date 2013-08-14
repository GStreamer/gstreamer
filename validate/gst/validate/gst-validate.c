/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/validate/validate.h>

static GMainLoop *mainloop;
static GstElement *pipeline;

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GError *err;
      gchar *debug;
      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  const gchar *scenario = NULL;
  guint count = -1;

  GOptionEntry options[] = {
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it will override the GST_VALIDATE_SCENARIO "
          "environment variable", NULL},
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
      "monitors to it to identify issues in the used elements");

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    exit (1);
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  if (scenario) {
    g_setenv ("GST_VALIDATE_SCENARIO", scenario, TRUE);
  }

  g_option_context_free (ctx);

  gst_init (&argc, &argv);
  gst_validate_init ();

  /* Create the pipeline */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err);
  g_free (argvn);

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
  gst_bus_add_watch (bus, bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    goto exit;
  g_main_loop_run (mainloop);

  count = gst_validate_runner_get_reports_count (runner);
  g_print ("Pipeline finished, issues found: %u\n", count);

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (monitor);
  g_object_unref (runner);
  g_object_unref (pipeline);
  if (count)
    return -1;
  return 0;
}
