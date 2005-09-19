#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <locale.h>

/* blocking */
static gboolean
event_loop (GstElement * pipeline)
{
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_return_val_if_fail (message != NULL, TRUE);

    switch (GST_MESSAGE_TYPE (message)) {
      case GST_MESSAGE_EOS:
        gst_message_unref (message);
        return FALSE;
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_message_unref (message);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        g_error_free (gerror);
        g_free (debug);
        return TRUE;
      }
      default:
        gst_message_unref (message);
        break;
    }
  }

  g_assert_not_reached ();
  return TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline = NULL;
  GError *error = NULL;
  GstElement *md5sink;
  gchar **argvn;
  gchar *md5string = g_malloc0 (33);

  free (malloc (8));            /* -lefence */

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);

  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &error);
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
    g_print ("ERROR: pipeline has no element named md5sink0.\n");
    g_print ("Did you forget to put an md5sink in the pipeline?\n");
    return 1;
  }

  if (!pipeline) {
    if (error) {
      g_warning ("pipeline could not be constructed: %s\n", error->message);
      g_error_free (error);
    } else
      g_warning ("pipeline could not be constructed\n");
    return 1;
  }

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_SUCCESS) {
    g_warning ("pipeline doesn't want to play\n");
    return 1;
  }

  event_loop (pipeline);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* print out md5sink here */
  md5sink = gst_bin_get_by_name (GST_BIN (pipeline), "md5sink0");
  g_assert (md5sink);
  g_object_get (G_OBJECT (md5sink), "md5", &md5string, NULL);
  printf ("%s\n", md5string);

  gst_object_unref (pipeline);

  return 0;
}
