#include <gst/gst.h>

/* these caps all have a non empty intersection */
GstStaticCaps sinkcaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "mpegtype:int=1, "
    "foo1:int=[20,40], "
    "foo2:int=[20,40], "
    "foo3:int=[10,20]"
);

GstStaticCaps mp1parsecaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "mpegtype:int=1, "
    "foo1:int=30, "
    "foo2:int=[20,30], "
    "foo3:int=[20,30]"
);

int 
main (int argc, char *argv[]) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  GstCaps *caps;

  gst_init (&argc, &argv);

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  caps = gst_caps_union (gst_static_caps_get (&sinkcaps),
      gst_static_caps_get (&mp1parsecaps));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (caps, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
