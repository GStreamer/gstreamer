#include <stdlib.h>
#include <gst/gst.h>

extern gboolean _gst_plugin_spew;

int main(int argc,char *argv[]) 
{
  xmlDocPtr doc;

  unlink("/etc/gstreamer/reg.xml");

  _gst_plugin_spew = TRUE;
  gst_init(&argc,&argv);

  doc = xmlNewDoc("1.0");
  doc->root = xmlNewDocNode(doc, NULL, "GST-PluginRegistry", NULL);
  gst_plugin_save_thyself(doc->root);
  xmlSaveFile("/etc/gstreamer/reg.xml", doc);
  exit(0);
}

