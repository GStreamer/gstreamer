/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 */

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/qa/qa.h>

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
  GOptionEntry options[] = {
    {NULL}
  };
  GOptionContext *ctx;
  GMainLoop *mainloop;
  gchar **argvn;
  GstQaRunner *runner;
  GstElement *pipeline;
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
