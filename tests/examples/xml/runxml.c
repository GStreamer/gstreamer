#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

G_GNUC_UNUSED static void
xml_loaded (GstXML * xml, GstObject * object, xmlNodePtr self, gpointer data)
{
  xmlNodePtr children = self->xmlChildrenNode;

  while (children) {
    if (!strcmp ((const char *) children->name, "comment")) {
      xmlNodePtr nodes = children->xmlChildrenNode;

      while (nodes) {
        if (!strcmp ((const char *) nodes->name, "text")) {
          gchar *name = g_strdup ((gchar *) xmlNodeGetContent (nodes));
          gchar *obj_name = gst_object_get_name (object);

          g_print ("object %s loaded with comment '%s'\n", obj_name, name);

          g_free (obj_name);
          g_free (name);
        }
        nodes = nodes->next;
      }
    }
    children = children->next;
  }
}

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
  GstXML *xml;
  GstElement *bin;
  gboolean ret;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <xml pipeline description>\n", argv[0]);
    exit (-1);
  }

  xml = gst_xml_new ();

/*  g_signal_connect (G_OBJECT (xml), "object_loaded", */
/*                  G_CALLBACK (xml_loaded), xml); */

  ret = gst_xml_parse_file (xml, (xmlChar *) argv[1], NULL);
  g_assert (ret == TRUE);

  bin = gst_xml_get_element (xml, (xmlChar *) "pipeline");
  g_assert (bin != NULL);

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bin);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  exit (0);
}
