#include <gst/gst.h>

GstStaticCaps2 rawcaps1 = GST_STATIC_CAPS(
  "video/x-raw-yuv, "
    "fourcc:fourcc=\"YUYV\", "
    "height:int=640, "
    "width:int=480, "
    "framerate:float=30.0; "
  "video/x-raw-yuv, "
    "fourcc:fourcc=\"I420\", "
    "height:int=640, "
    "width:int=480, "
    "framerate:float=30.0"
);

int 
main (int argc, char *argv[]) 
{
  GstCaps2 *caps1;
  GstCaps2 *caps2;
  GstCaps2 *caps;

  gst_init (&argc, &argv);

  caps1 = gst_caps2_copy( gst_static_caps2_get (&rawcaps1));
  caps2 = gst_caps2_copy_1 (gst_static_caps2_get (&rawcaps1));

#if 0
  gst_caps2_set(caps1, "height", GST_PROPS_INT(640));
  gst_caps2_set(caps1, "width", GST_PROPS_INT(480));
  gst_caps2_set(caps1, "framerate", GST_PROPS_FLOAT(30.0));
#endif

  caps = gst_caps2_intersect(caps1, caps2);

  g_print("caps %s\n", gst_caps2_to_string(caps));

  if (gst_caps2_is_empty (caps)) return 1;
  return 0;
}
