#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <locale.h>

static GMainLoop *loop;

static gboolean
message_received (GstBus * bus, GstMessage * message, GstPipeline * pipeline)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:
      gst_object_default_error (GST_MESSAGE_SRC (message),
          GST_MESSAGE_ERROR_GERROR (message),
          GST_MESSAGE_ERROR_DEBUG (message));
      if (g_main_loop_is_running (loop))
        g_main_loop_quit (loop);
      break;
    default:
      break;
  }
  gst_message_unref (message);

  return TRUE;
}


int
main (int argc, char *argv[])
{
  /* options */
  gboolean verbose = FALSE;
  gchar *exclude_args = NULL;
  struct poptOption options[] = {
    {"verbose", 'v', POPT_ARG_NONE | POPT_ARGFLAG_STRIP, &verbose, 0,
        "do not output status information", NULL},
    POPT_TABLEEND
  };

  GstElement *pipeline = NULL;
  gchar **argvn;
  GError *error = NULL;
  GstElement *md5sink;
  gchar *md5string = g_malloc0 (33);

  free (malloc (8));            /* -lefence */

  setlocale (LC_ALL, "");

  gst_init_with_popt_table (&argc, &argv, options);

  /* make a parseable argvn array */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));

  /* Check if we have an element already that is called md5sink0
     in the pipeline; if not, add one */
  //pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  if (!pipeline) {
    if (error) {
      g_warning ("pipeline could not be constructed: %s\n", error->message);
      g_error_free (error);
    } else
      g_warning ("pipeline could not be constructed\n");
    return 1;
  }

  md5sink = gst_bin_get_by_name (GST_BIN (pipeline), "md5sink0");
  if (md5sink == NULL) {
    g_print ("adding an md5sink element to the pipeline\n");
    /* make a null-terminated version of argv with ! md5sink appended
     * ! is stored in argvn[argc - 1], md5sink in argvn[argc],
     * NULL pointer in argvn[argc + 1] */
    g_free (argvn);
    argvn = g_new0 (char *, argc + 2);
    memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
    argvn[argc - 1] = g_strdup_printf ("!");
    argvn[argc] = g_strdup_printf ("md5sink");
    pipeline =
        (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
  }

  if (!pipeline) {
    if (error) {
      g_warning ("pipeline could not be constructed: %s\n", error->message);
      g_error_free (error);
    } else
      g_warning ("pipeline could not be constructed\n");
    return 1;
  }

  if (verbose) {
    gchar **exclude_list = exclude_args ? g_strsplit (exclude_args, ",", 0)
        : NULL;

    g_signal_connect (pipeline, "deep_notify",
        G_CALLBACK (gst_object_default_deep_notify), exclude_list);
  }
  //g_signal_connect (pipeline, "error",
  //    G_CALLBACK (gst_object_default_error), NULL);

  loop = g_main_loop_new (NULL, FALSE);
  gst_bus_add_watch (gst_element_get_bus (GST_ELEMENT (pipeline)),
      (GstBusHandler) message_received, pipeline);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS) {
    g_warning ("pipeline doesn't want to play\n");
    return 0;
  }

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* print out md5sink here */
  md5sink = gst_bin_get_by_name (GST_BIN (pipeline), "md5sink0");
  g_assert (md5sink);
  g_object_get (G_OBJECT (md5sink), "md5", &md5string, NULL);
  printf ("%s\n", md5string);

  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
