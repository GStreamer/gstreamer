#include <gst/gst.h>

int main(int argc,char *argv[]) 
{
  xmlDocPtr doc;

  unlink("/etc/gstreamer/reg.xml");

  gst_init(&argc,&argv);

  doc = xmlNewDoc("1.0");
  doc->root = xmlNewDocNode(doc, NULL, "GST-PluginRegistry", NULL);
  gst_plugin_save_thyself(doc->root);
  xmlSaveFile("/etc/gstreamer/reg.xml", doc);
  exit(0);
}

