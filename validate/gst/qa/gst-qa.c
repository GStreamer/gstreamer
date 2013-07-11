/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/qa/qa.h>

static gboolean seek_tests = FALSE;
static gboolean seek_done = FALSE;

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
    case GST_MESSAGE_STATE_CHANGED:
    {
      GstState new_state;
      if (GST_MESSAGE_SRC (message) == (GstObject *) pipeline) {
        gst_message_parse_state_changed (message, NULL, &new_state, NULL);
        if (new_state == GST_STATE_PLAYING) {
          /* pipeline has started, issue seeking */
          /* TODO define where to seek to with arguments? */
          if (seek_tests && !seek_done) {
            g_print ("Performing seek\n");
            seek_done = TRUE;
            gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
                GST_SEEK_FLAG_FLUSH, 5 * GST_SECOND);
          }
        }
      }
    }
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
  GOptionEntry options[] = {
    {"seek-test", '\0', 0, G_OPTION_ARG_NONE, &seek_tests,
        "Perform the seeking use case", NULL},
    {NULL}
  };
  GOptionContext *ctx;
  gchar **argvn;
  GstQaRunner *runner;
  GstBus *bus;

  ctx = g_option_context_new ("- runs QA tests for a pipeline.");
  g_option_context_add_main_entries (ctx, options, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  gst_init (&argc, &argv);

  /* Create the pipeline */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err);
  g_free (argvn);

  runner = gst_qa_runner_new (pipeline);
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!gst_qa_runner_setup (runner)) {
    g_printerr ("Failed to setup QA Runner\n");
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

  /* TODO get report from QA runner */

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (runner);
  return 0;
}
