#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

G_GNUC_UNUSED static void
xml_loaded (GstXML * xml, GstObject * object, xmlNodePtr self, gpointer data)
{
  xmlNodePtr children = self->xmlChildrenNode;

  while (children) {
    if (!strcmp (children->name, "comment")) {
      xmlNodePtr nodes = children->xmlChildrenNode;

      while (nodes) {
	if (!strcmp (nodes->name, "text")) {
	  gchar *name = g_strdup (xmlNodeGetContent (nodes));

	  g_print ("object %s loaded with comment '%s'\n",
	      gst_object_get_name (object), name);
	}
	nodes = nodes->next;
      }
    }
    children = children->next;
  }
}

int
main (int argc, char *argv[])
{
  GstXML *xml;
  GstElement *pipeline;
  gboolean ret;

  gst_init (&argc, &argv);

  xml = gst_xml_new ();

/*  g_signal_connect (G_OBJECT (xml), "object_loaded", */
/*		    G_CALLBACK (xml_loaded), xml); */

  if (argc == 2)
    ret = gst_xml_parse_file (xml, argv[1], NULL);
  else
    ret = gst_xml_parse_file (xml, "xmlTest.gst", NULL);

  g_assert (ret == TRUE);

  pipeline = gst_xml_get_element (xml, "pipeline");
  g_assert (pipeline != NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)));

  gst_element_set_state (pipeline, GST_STATE_NULL);

  exit (0);
}
