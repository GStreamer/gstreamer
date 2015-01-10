#include <stdlib.h>
#include <gst/gst.h>
#include <gst/net/gstnetclientclock.h>

static gboolean
handle_bus_message (GstBus * bus, GstMessage * message, GstClock * client_clock)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT) {
    const GstStructure *s = gst_message_get_structure (message);
    gchar *str;

    if (s == NULL)
      return TRUE;
    str = gst_structure_to_string (s);
    g_print ("%s\n", str);
    g_free (str);
  }
  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GMainLoop *loop;
  gchar *host;
  guint16 port;
  GstClock *client_clock;
  GstBus *bus;

  gst_init (&argc, &argv);

  if (argc < 3) {
    g_print ("Usage: netclock-client <host> <port>\n");
    return 1;
  }

  host = argv[1];
  port = atoi (argv[2]);

  client_clock = gst_net_client_clock_new (NULL, host, port, 0);
  if (client_clock == NULL) {
    g_printerr ("Failed to create network clock client\n");
    return 1;
  }

  bus = gst_bus_new ();
  gst_bus_add_watch (bus, (GstBusFunc) handle_bus_message, client_clock);
  g_object_set (G_OBJECT (client_clock), "bus", bus, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  /* cleanup */
  g_main_loop_unref (loop);
  g_object_unref (client_clock);

  return 0;
}
