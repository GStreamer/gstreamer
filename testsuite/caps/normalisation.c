#include <gst/gst.h>

/* these caps all have a non empty intersection */
GstStaticCaps sinkcaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}, "
    "foo1=(int)[20,40], "
    "foo2=(int)[20,40], "
    "foo3=(int)[10,20]"
);

GstStaticCaps mp1parsecaps = GST_STATIC_CAPS (
  "video/mpeg, "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}, "
    "foo4=(fourcc){\"YV12\",\"YUY2\"}"
);

GstStaticCaps rawcaps = GST_STATIC_CAPS (
  "video/raw, "
    "width=(int)[16,4096], "
    "height=(int)[16,4096], "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}"
);

GstStaticCaps rawcaps2 = GST_STATIC_CAPS (
  "video/raw, "
    "width=(int)[16,256], "
    "height=(int)16; "
  "video/raw, "
    "width=(int)[16,256], "
    "height=(int)16"
);

GstStaticCaps rawcaps3 = GST_STATIC_CAPS (
  "video/raw, "
    "width=(int)[16,256], "
    "height=(int)16; "
  "video/raw, "
    "width=(int)[16,256], "
    "height=(int)16; "
  "video/raw, "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}, "
    "height=(int)[16,4096]"
);

GstStaticCaps rawcaps4 = GST_STATIC_CAPS (
  "x, "
    "y=(int){1,2}, "
    "z=(int){3,4}; "
  "a, "
    "b=(int){5,6}, "
    "c=(int){7,8}"
);

/* defined, not used
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
*/

int 
main (int argc, char *argv[]) 
{
  GstCaps *caps;

  gst_init (&argc, &argv);

  caps = gst_caps_normalize (gst_static_caps_get (&sinkcaps));
  g_print ("\n%s\n", gst_caps_to_string (caps));

  caps = gst_caps_normalize (gst_static_caps_get (&mp1parsecaps));
  g_print ("\n%s\n", gst_caps_to_string (caps));

  caps = gst_caps_normalize (gst_static_caps_get (&rawcaps));
  g_print ("\n%s\n", gst_caps_to_string (caps));

  caps = gst_caps_normalize (gst_static_caps_get (&rawcaps2));
  g_print ("\n%s\n", gst_caps_to_string (caps));

  caps = gst_caps_normalize (gst_static_caps_get (&rawcaps3));
  g_print ("\n%s\n", gst_caps_to_string (caps));

  caps = gst_caps_normalize (gst_static_caps_get (&rawcaps4));
  g_assert (gst_caps_get_size (caps) == 8);
  g_print ("\n%s\n", gst_caps_to_string (caps));

  return 0;
}
