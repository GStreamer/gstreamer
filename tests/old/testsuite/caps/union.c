#include <gst/gst.h>

/* these caps all have a non empty intersection */
GST_CAPS_FACTORY (sinkcaps,
  GST_CAPS_NEW (
    "mpeg2dec_sink",
    "video/mpeg",
      "mpegtype", GST_PROPS_INT (1),
      "foo1",     GST_PROPS_INT_RANGE (20,40),
      "foo2",     GST_PROPS_INT_RANGE (20,40),
      "foo3",     GST_PROPS_INT_RANGE (10,20)
  )
);

GST_CAPS_FACTORY (mp1parsecaps,
  GST_CAPS_NEW (
    "mp1parse_src",
    "video/mpeg",
      "mpegtype", GST_PROPS_INT (1),
      "foo1",     GST_PROPS_INT (30),
      "foo2",     GST_PROPS_INT_RANGE (20,30),
      "foo3",     GST_PROPS_INT_RANGE (20,30)
  )
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

  caps = gst_caps_union (GST_CAPS_GET (sinkcaps), GST_CAPS_GET (mp1parsecaps));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (caps, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
