#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  GstElement *mad;
  GstElement *mp3parse;
  GstElement *queue;
  GstPad *sinkpad;
  GstPad *srcpad;
  GstPad *qsinkpad;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  gst_init (&argc, &argv);

  mad = gst_elementfactory_make ("mad", "mad");
  g_assert (mad != NULL);

  sinkpad = gst_element_get_pad (mad, "sink");
  g_assert (sinkpad != NULL);

  queue = gst_elementfactory_make ("queue", "queue");
  g_assert (queue != NULL);
  
  srcpad = gst_element_get_pad (queue, "src");
  g_assert (srcpad != NULL);
  qsinkpad = gst_element_get_pad (queue, "sink");
  g_assert (qsinkpad != NULL);
  
  parent = xmlNewChild (doc->xmlRootNode, NULL, "mad caps", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (sinkpad), parent);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "queue caps", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (srcpad), parent);

  gst_pad_connect (srcpad, sinkpad);
  
  parent = xmlNewChild (doc->xmlRootNode, NULL, "queue caps after connect src", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (srcpad), parent);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "queue caps after connect sink", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (qsinkpad), parent);

  mp3parse = gst_elementfactory_make ("mp3parse", "mp3parse");
  g_assert (mp3parse != NULL);

  gst_pad_connect (gst_element_get_pad (mp3parse, "src"), qsinkpad);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "queue caps after connect sink", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (qsinkpad), parent);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "mad caps after connect sink", NULL);
  gst_caps_save_thyself (gst_pad_get_caps (sinkpad), parent);

  xmlDocDump(stdout, doc);

  return 0;
}
