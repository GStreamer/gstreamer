#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

static void
event_loop (GstElement * pipe)
{
  GstBus *bus;
  GstMessage *message = NULL;

  bus = gst_element_get_bus (GST_ELEMENT (pipe));

  while (TRUE) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, -1);

    g_assert (message != NULL);

    switch (message->type) {
      case GST_MESSAGE_EOS:
        gst_message_unref (message);
        return;
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_ERROR:{
        GError *gerror;
        gchar *debug;

        gst_message_parse_error (message, &gerror, &debug);
        gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
        gst_message_unref (message);
        g_error_free (gerror);
        g_free (debug);
        return;
      }
      default:
        gst_message_unref (message);
        break;
    }
  }
}

int
main (int argc, char *argv[])
{
  GstElement *bin;
  GstElement *filesrc;
  GError *error = NULL;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <mp3 file>\n", argv[0]);
    exit (-1);
  }

  bin = (GstElement *)
      gst_parse_launch ("filesrc name=my_filesrc ! mad ! osssink", &error);
  if (!bin) {
    fprintf (stderr, "Parse error: %s", error->message);
    exit (-1);
  }

  filesrc = gst_bin_get_by_name (GST_BIN (bin), "my_filesrc");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bin);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  exit (0);
}
