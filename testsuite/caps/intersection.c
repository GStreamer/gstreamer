#include <gst/gst.h>

/* these caps all have a non empty intersection */
GstStaticCaps sinkcaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "mpegtype=(int)1, "
    "foo1=(int)[20,40], "
    "foo2=(int)[20,40], "
    "foo3=(int)[10,20]"
);

GstStaticCaps mp1parsecaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "mpegtype=(int)1, "
    "foo1=(int)30, "
    "foo2=(int)[20,30], "
    "foo3=(int)[20,30]"
);



GstStaticCaps rawcaps = GST_STATIC_CAPS (
  "video/raw, "
    "width=(int)[16,4096], "
    "height=(int)[16,4096]"
);

GstStaticCaps rawcaps2 = GST_STATIC_CAPS (
  "video/raw, "
    "height=(int)[16,256], "
    "depth=(int)16"
);

GstStaticCaps rawcaps3 = GST_STATIC_CAPS (
  "video/raw, "
    "fourcc=(fourcc){\"YUY2\", \"YV12\" }, "
    "height=(int)[16,4096]"
);

GstStaticCaps rawcaps4 = GST_STATIC_CAPS (
  "video/raw, "
    "fourcc=(fourcc){\"YUY2\",\"YV12\",\"YUYV\" }, "
    "height=(int)[16,4096]"
);

GstStaticCaps rawcaps5 = GST_STATIC_CAPS (
  "video/raw, "
    "fourcc=(fourcc){\"YUYV\",\"YUY2\"}, "
    "height=(int)[16,4096]"
);

GstStaticCaps rawcaps6 = GST_STATIC_CAPS (
  "video/raw, "
    "fourcc=(fourcc)\"YUYV\", "
    "height=(int)640, "
    "width=(int)480, "
    "framerate=(double)30.0; "
  "video/raw, "
    "fourcc=(fourcc)\"I420\", "
    "height=(int)640, "
    "width=(int)480, "
    "framerate=(double)30.0"
);

GstStaticCaps rawcaps7 = GST_STATIC_CAPS (
    "video/x-raw-yuv, format=(fourcc)YUY2, width=(int)[1,2147483647], height=(int)[1,2147483647], framerate=(double)[0,1.79769e+308]"
);

GstStaticCaps rawcaps8 = GST_STATIC_CAPS (
    "video/x-raw-yuv, format=(fourcc){ I420, YV12, YUY2 }, width=(int)[16,4096], height=(int)[16,4096], framerate=(double)[0,1.79769e+308]"
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

  /*
  g_mem_chunk_info ();
  for (i = 0; i<100000; i++) {
    caps = gst_caps_intersect (gst_static_caps_get (rawcaps3), GST_CAPS_GET (rawcaps4));
    gst_caps_unref (caps);
  }
  g_mem_chunk_info ();
  */

  caps = gst_caps_intersect (gst_static_caps_get (&sinkcaps),
      gst_static_caps_get (&mp1parsecaps));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (gst_static_caps_get (&rawcaps),
      gst_static_caps_get (&rawcaps2));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities2", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (gst_static_caps_get (&rawcaps3),
      gst_static_caps_get (&rawcaps4));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities3", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (gst_static_caps_get (&rawcaps3),
      gst_static_caps_get (&rawcaps5));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities4", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (gst_static_caps_get (&rawcaps6),
      gst_caps_new_full (gst_structure_copy (
          gst_caps_get_structure (gst_static_caps_get (&rawcaps6), 0)), NULL));
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities5", NULL);
  gst_caps_save_thyself (caps, parent);

  caps = gst_caps_intersect (gst_static_caps_get (&rawcaps7),
      gst_static_caps_get (&rawcaps8));
  g_print("intersection: %s\n", gst_caps_to_string (caps));

  xmlDocDump(stdout, doc);

  return 0;
}
