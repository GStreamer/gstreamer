#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

static void
xml_loaded (GstXML *xml, GstObject *object, gpointer data)
{
  if (GST_IS_PAD (object)) {
    g_print ("pad loaded '%s'\n", gst_pad_get_name (GST_PAD (object)));
  }
  else if (GST_IS_ELEMENT (object)) {
    g_print ("element loaded '%s'\n", gst_element_get_name (GST_ELEMENT (object)));
  }
}

int main(int argc,char *argv[])
{
  GstXML *xml;
  GstElement *bin;
  gboolean ret;

  gst_init(&argc,&argv);

  xml = gst_xml_new ();

  gtk_signal_connect (GTK_OBJECT (xml), "object_loaded", xml_loaded, NULL);

  ret = gst_xml_parse_file(xml, "xmlTest.gst", NULL);
  g_assert (ret == TRUE);

  bin = gst_xml_get_element(xml, "bin");
  g_assert (bin != NULL);

  gst_element_set_state(bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (gst_bin_iterate(GST_BIN(bin)));

  gst_element_set_state(bin, GST_STATE_NULL);

  exit(0);
}

