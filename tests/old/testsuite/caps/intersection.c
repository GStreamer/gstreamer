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



GST_CAPS_FACTORY (rawcaps,
  GST_CAPS_NEW (
    "mpeg2dec_src",
    "video/raw",
      "width",	GST_PROPS_INT_RANGE (16, 4096),
      "height",	GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_CAPS_FACTORY (rawcaps2,
  GST_CAPS_NEW (
    "raw_sink_caps",
    "video/raw",
      "height",	  GST_PROPS_INT_RANGE (16, 256),
      "depth",	  GST_PROPS_INT (16)
  )
);

GST_CAPS_FACTORY (rawcaps3,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc", GST_PROPS_LIST (
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")),
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")) 
	        ),
      "height", GST_PROPS_INT_RANGE (16, 4096)
  )
);


GST_CAPS_FACTORY (rawcaps4,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUYV")) 
		  ),
      "height",	  GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_CAPS_FACTORY (rawcaps5,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUYV")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2"))
		  ),
      "height",   GST_PROPS_INT_RANGE (16, 4096)
  )
);
int 
main (int argc, char *argv[]) 
{
  gboolean testret;
  xmlDocPtr doc;
  xmlNodePtr parent;
  GstCaps *caps;
  gint i;

  gst_init (&argc, &argv);

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  /*
  g_mem_chunk_info ();
  for (i = 0; i<100000; i++) {
    caps = gst_caps_intersect (GST_CAPS_GET (rawcaps3), GST_CAPS_GET (rawcaps4));
    gst_caps_unref (caps);
  }
  g_mem_chunk_info ();
  */

  caps = gst_caps_intersect (GST_CAPS_GET (sinkcaps), GST_CAPS_GET (mp1parsecaps));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (GST_CAPS_GET (rawcaps), GST_CAPS_GET (rawcaps2));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities2", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (GST_CAPS_GET (rawcaps3), GST_CAPS_GET (rawcaps4));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities3", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (GST_CAPS_GET (rawcaps3), GST_CAPS_GET (rawcaps5));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities4", NULL);
  gst_caps_save_thyself (caps, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
