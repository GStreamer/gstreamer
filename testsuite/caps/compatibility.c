#include <gst/gst.h>

/* these caps all have a non empty intersection */
GstStaticCaps sinkcaps = GST_STATIC_CAPS ("video/mpeg, " "mpegtype=(int)[1,2]");

GstStaticCaps mp1parsecaps = GST_STATIC_CAPS ("video/mpeg, " "mpegtype=(int)1");

GstStaticCaps rawcaps = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){YV12,YUY2}, "
    "width=(int)[16,4096], " "height=(int)[16,4096]");

GstStaticCaps rawcaps2 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc)YUY2, " "height=(int)[16,256]");

GstStaticCaps rawcaps3 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){YV12,YUY2}, " "height=(int)[16,4096]");

#if 0
/* these caps aren't used yet */
GstStaticCaps rawcaps4 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YV12\", \"YUYV\"}, " "height=(int)[16,4096]");

GstStaticCaps rawcaps4 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YUYV\", \"YUY2\"}, " "height=(int)[16,4096]");
#endif

GstStaticCaps rawcaps6 = GST_STATIC_CAPS ("video/raw, "
    "format=(fourcc)\"I420\"; " "video/raw, " "format=(fourcc)\"YUYV\"");

GstStaticCaps rawcaps7 = GST_STATIC_CAPS ("video/raw, "
    "format=(fourcc)\"I420\"; " "video/raw, " "format=(fourcc)\"YV12\"");


int
main (int argc, char *argv[])
{
  gboolean testret;
  gint ret = 0;

  gst_init (&argc, &argv);

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&mp1parsecaps),
      gst_static_caps_get (&rawcaps));
  g_print ("4 <-> 2 == %d (invalid, wrong major type)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&mp1parsecaps),
      gst_static_caps_get (&sinkcaps));
  g_print ("4 <-> 1 == %d (valid, subset)\n", testret);
  ret = ret + (testret == TRUE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&sinkcaps),
      gst_static_caps_get (&mp1parsecaps));
  g_print ("1 <-> 4 == %d (invalid, superset)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps),
      gst_static_caps_get (&rawcaps2));
  g_print ("2 <-> 3 == %d (invalid, ranges)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps),
      gst_static_caps_get (&rawcaps3));
  g_print ("2 <-> 5 == %d (valid)\n", testret);
  ret = ret + (testret == TRUE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps3),
      gst_static_caps_get (&rawcaps));
  g_print ("5 <-> 2 == %d (invalid)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps2),
      gst_static_caps_get (&rawcaps3));
  g_print ("3 <-> 5 == %d (valid)\n", testret);
  ret = ret + (testret == TRUE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps2),
      gst_static_caps_get (&rawcaps));
  g_print ("3 <-> 2 == %d (invalid, property missing in source)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps),
      gst_static_caps_get (&rawcaps));
  g_print ("2 <-> 2 == %d (valid, same caps)\n", testret);
  ret = ret + (testret == TRUE) ? 0 : 1;

  testret = gst_caps_is_always_compatible (gst_static_caps_get (&rawcaps6),
      gst_static_caps_get (&rawcaps7));
  g_print ("6 <-> 7 == %d (invalid, second caps doesn't fit)\n", testret);
  ret = ret + (testret == FALSE) ? 0 : 1;

  return ret;
}
