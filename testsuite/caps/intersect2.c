#include <gst/gst.h>

GstStaticCaps rawcaps1 = GST_STATIC_CAPS ("video/x-raw-yuv, "
    "fourcc:fourcc=\"YUYV\", "
    "height:int=640, "
    "width:int=480, "
    "framerate:float=30.0; "
    "video/x-raw-yuv, "
    "fourcc:fourcc=\"I420\", "
    "height:int=640, " "width:int=480, " "framerate:float=30.0");

GstStaticCaps rawcaps2 = GST_STATIC_CAPS ("video/x-raw-yuv");

GstStaticCaps rawcaps3 =
GST_STATIC_CAPS ("video/x-raw-yuv, height=(int) [ 0, MAX ]");

GstStaticCaps rawcaps4 =
    GST_STATIC_CAPS
    ("video/x-raw-yuv, format=(fourcc)YUY2; video/x-raw-yuv, format=(fourcc)UYVY");

GstStaticCaps rawcaps5 =
    GST_STATIC_CAPS
    ("video/x-raw-yuv, format=(fourcc)YUY2, framerate=(double)[0,1.79769e+308], width=(int)[0,2147483647], height=(int)[0,2147483647]; video/x-raw-yuv, format=(fourcc)UYVY, framerate=(double)[0,1.79769e+308], width=(int)[0,2147483647], height=(int)[0,2147483647]");

GstStaticCaps rawcaps6 =
    GST_STATIC_CAPS
    ("video/x-raw-yuv, format=(fourcc)YUY2, width=(int)320, height=(int)240");

GstStaticCaps rawcaps7 =
    GST_STATIC_CAPS
    ("video/x-raw-yuv, format=(fourcc)YUY2, width=(int)[0,2147483647], height=(int)[0,2147483647], framerate=(double)[0,1.79769e+308]");

GstStaticCaps rawcaps8 =
    GST_STATIC_CAPS
    ("video/x-raw-yuv, format=(fourcc)YUY2, width=(int)320, height=(int)240");

GstStaticCaps rawcaps9 =
    GST_STATIC_CAPS
    ("audio/x-raw-float, "
    "channel-positions=(int)< "
    "{ 1, 2, 3, 4, 5, 6 }, "
    "{ 1, 2 }, "
    "{ 1, 2, 3, 4, 5, 6 }, " "{ 1, 2, 3, 4, 5, 6 }, " "{ 4, 5, 6 }, " "6 >");

GstStaticCaps rawcaps10 =
    GST_STATIC_CAPS
    ("audio/x-raw-float, "
    "channel-positions=(int)< 1, { 2, 3, 4, 5, 6 }, 3, 4, {4, 5, 6 }, "
    "{ 4, 5, 6 } >");


int
main (int argc, char *argv[])
{
  GstCaps *caps1;
  GstCaps *caps2;
  GstCaps *caps3;
  GstCaps *caps4;
  GstCaps *caps;

  gst_init (&argc, &argv);

  caps1 = gst_caps_copy (gst_static_caps_get (&rawcaps1));
  caps2 =
      gst_caps_new_full (gst_structure_copy (gst_caps_get_structure
          (gst_static_caps_get (&rawcaps1), 0)), NULL);

#if 0
  gst_caps_set (caps1, "height", GST_PROPS_INT (640));
  gst_caps_set (caps1, "width", GST_PROPS_INT (480));
  gst_caps_set (caps1, "framerate", GST_PROPS_FLOAT (30.0));
#endif

  caps = gst_caps_intersect (caps1, caps2);
  g_print ("caps %s\n", gst_caps_to_string (caps));
  if (gst_caps_is_empty (caps))
    return 1;
  gst_caps_free (caps1);
  gst_caps_free (caps2);

  caps1 = gst_caps_copy (gst_static_caps_get (&rawcaps2));
  caps2 = gst_caps_copy (gst_static_caps_get (&rawcaps3));
  caps = gst_caps_intersect (caps1, caps2);
  g_print ("caps %s\n", gst_caps_to_string (caps));
  if (gst_caps_is_empty (caps))
    return 1;
  gst_caps_free (caps1);
  gst_caps_free (caps2);

  caps1 = gst_caps_copy (gst_static_caps_get (&rawcaps4));
  caps2 = gst_caps_copy (gst_static_caps_get (&rawcaps5));
  caps3 = gst_caps_copy (gst_static_caps_get (&rawcaps6));
  caps4 = gst_caps_intersect (caps1, caps2);
  caps = gst_caps_intersect (caps3, caps4);
  g_print ("caps4 %s\n", gst_caps_to_string (caps4));
  g_print ("caps %s\n", gst_caps_to_string (caps));
  gst_caps_free (caps1);
  gst_caps_free (caps2);
  gst_caps_free (caps3);
  gst_caps_free (caps4);

  caps1 = gst_caps_copy (gst_static_caps_get (&rawcaps7));
  caps2 = gst_caps_copy (gst_static_caps_get (&rawcaps8));
  caps = gst_caps_intersect (caps1, caps2);
  g_print ("caps %s\n", gst_caps_to_string (caps));
  if (gst_caps_is_empty (caps))
    return 1;
  gst_caps_free (caps1);
  gst_caps_free (caps2);

  caps1 = gst_caps_copy (gst_static_caps_get (&rawcaps9));
  caps2 = gst_caps_copy (gst_static_caps_get (&rawcaps10));
  caps = gst_caps_intersect (caps1, caps2);
  g_print ("caps %s\n", gst_caps_to_string (caps));
  if (gst_caps_is_empty (caps))
    return 1;
  gst_caps_free (caps1);
  gst_caps_free (caps2);

  return 0;
}
