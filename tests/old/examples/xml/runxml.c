#include <stdlib.h>
#include <gst/gst.h>

gboolean playing;

/* eos will be called when the src element has an end of stream */
void eos(GstElement *element, gpointer data)
{
  g_print("have eos, quitting\n");

  playing = FALSE;
}

int main(int argc,char *argv[]) 
{
  GstXML *xml;
  GstElement *bin;
  GstElement *disk;

  gst_init(&argc,&argv);

  xml = gst_xml_new("xmlTest.gst", NULL);

  bin = gst_xml_get_element(xml, "bin");
  
  gst_element_set_state(bin, GST_STATE_PLAYING);

  playing = TRUE;

  while (playing) {
    gst_bin_iterate(GST_BIN(bin));
  }

  gst_element_set_state(bin, GST_STATE_NULL);

  exit(0);
}

