#include <gst/gst.h>

void
type_found (GstElement *typefind, const GstCaps * caps) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  
  doc = xmlNewDoc ("1.0");  
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Caps1", NULL);
  /* FIXME */
  //gst_caps_save_thyself (caps, parent);

  xmlDocDump (stdout, doc);
}

int 
main(int argc, char *argv[]) 
{
  GstElement *bin, *filesrc, *typefind;

  gst_init(&argc,&argv);

  if (argc != 2) {
    g_print("usage: %s <filename>\n", argv[0]);
    exit(-1);
  }

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new("bin");
  g_assert(bin != NULL);

  /* create a file reader */
  filesrc = gst_element_factory_make("filesrc", "file_source");
  g_assert(filesrc != NULL);
  g_object_set(G_OBJECT(filesrc),"location", argv[1],NULL);

  typefind = gst_element_factory_make("typefind", "typefind");
  g_assert(typefind != NULL);

  /* add objects to the main pipeline */
  gst_bin_add(GST_BIN(bin), filesrc);
  gst_bin_add(GST_BIN(bin), typefind);

  g_signal_connect (G_OBJECT (typefind), "have_type", 
		    G_CALLBACK (type_found), NULL);

  gst_element_link (filesrc, typefind);

  /* start playing */
  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_PLAYING);

  gst_bin_iterate(GST_BIN(bin));

  gst_element_set_state(GST_ELEMENT(bin), GST_STATE_NULL);

  exit(0);
}

