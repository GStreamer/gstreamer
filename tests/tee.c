#include <gst/gst.h>

int
main(int argc, char *argv[])
{
  GstElement *element, *mp3parse;
  GstPadTemplate *templ;
  GstPad *pad;
  xmlDocPtr doc;
  xmlNodePtr parent;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  gst_init(&argc,&argv);

  element = gst_elementfactory_make("tee","element");
  mp3parse = gst_elementfactory_make("mp3parse","mp3parse");

  pad = gst_element_request_pad_by_name (element, "src%d");
  g_print ("new pad %s\n", gst_pad_get_name (pad));

  templ = gst_element_get_padtemplate_by_name (mp3parse, "sink");

  templ = gst_padtemplate_create ("src%d", GST_PAD_SRC, GST_PAD_REQUEST, templ->caps);
  pad = gst_element_request_pad (element, templ);
  g_print ("new pad %s\n", gst_pad_get_name (pad));

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Padtemplate", NULL);

  gst_padtemplate_save_thyself (pad->padtemplate, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
