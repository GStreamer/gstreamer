#include <gst/gst.h>

GST_CAPS_FACTORY (rawcaps1,
  GST_CAPS_NEW (
    "raw1_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_FOURCC (GST_STR_FOURCC ("YUYV")),
      "height",   GST_PROPS_INT (640),
      "width",    GST_PROPS_INT (480),
      "framerate",GST_PROPS_FLOAT (30.0)
  ),
  GST_CAPS_NEW (
    "raw1_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
      "height",   GST_PROPS_INT (640),
      "width",    GST_PROPS_INT (480),
      "framerate",GST_PROPS_FLOAT (30.0)
  )
);

int 
main (int argc, char *argv[]) 
{
  GstCaps *caps1;
  GstCaps *caps2;
  GstCaps *caps;

  gst_init (&argc, &argv);

  caps1 = GST_CAPS_GET (rawcaps1);
  caps2 = gst_caps_copy_1 (GST_CAPS_GET (rawcaps1));

  gst_caps_set(caps1, "height", GST_PROPS_INT(640));
  gst_caps_set(caps1, "width", GST_PROPS_INT(480));
  gst_caps_set(caps1, "framerate", GST_PROPS_FLOAT(30.0));

  caps = gst_caps_intersect(caps1, caps2);

  g_print("caps %s\n", gst_caps_to_string(caps));

  if(caps == NULL)return 1;
  return 0;
}
