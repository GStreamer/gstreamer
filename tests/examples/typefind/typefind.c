#include <gst/gst.h>

static void
type_found (GstElement * typefind, const GstCaps * caps)
{
  xmlDocPtr doc;
  xmlNodePtr parent;

  doc = xmlNewDoc ((xmlChar *) "1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, (xmlChar *) "Capabilities",
      NULL);

  parent = xmlNewChild (doc->xmlRootNode, NULL, (xmlChar *) "Caps1", NULL);
  /* FIXME */
  //gst_caps_save_thyself (caps, parent);

  xmlDocDump (stdout, doc);
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
  GstElement *bin, *filesrc, *typefind;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    exit (-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("bin");
  g_assert (bin != NULL);

  /* create a file reader */
  filesrc = gst_element_factory_make ("filesrc", "file_source");
  g_assert (filesrc != NULL);
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  typefind = gst_element_factory_make ("typefind", "typefind");
  g_assert (typefind != NULL);

  /* add objects to the main pipeline */
  gst_bin_add (GST_BIN (bin), filesrc);
  gst_bin_add (GST_BIN (bin), typefind);

  g_signal_connect (G_OBJECT (typefind), "have_type",
      G_CALLBACK (type_found), NULL);

  gst_element_link (filesrc, typefind);

  /* start playing */
  gst_element_set_state (bin, GST_STATE_PLAYING);

  /* Run event loop listening for bus messages until EOS or ERROR */
  event_loop (bin);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);

  exit (0);
}
