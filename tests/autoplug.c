#include <gst/gst.h>

static void
new_object_added (GstAutoplug *autoplug, GstObject *object)
{
  g_print ("added new object \"%s\"\n", gst_object_get_name (object));
}

int
main (int argc, char *argv[])
{
  GstElement *element;
  GstElement *videosink, *osssink;
  GstAutoplug *autoplugger;
  GstCaps *testcaps;

  gst_init(&argc,&argv);

  osssink = gst_elementfactory_make ("osssink", "osssink");
  g_assert (osssink != NULL);
  videosink = gst_elementfactory_make ("xvideosink", "videosink");
  g_assert (videosink != NULL);

  testcaps = gst_caps_new ("test_caps",
			 "video/mpeg",
			 gst_props_new (
			   "mpegversion",  GST_PROPS_INT (1),
			   "systemstream", GST_PROPS_BOOLEAN (TRUE),
			   NULL));

  autoplugger = gst_autoplugfactory_make ("static");

  g_signal_connect (G_OBJECT (autoplugger), "new_object", new_object_added, NULL);

  element = gst_autoplug_to_caps (autoplugger, testcaps,
		  gst_pad_get_caps (gst_element_get_pad (osssink, "sink")),
		  gst_pad_get_caps (gst_element_get_pad (videosink, "sink")),
		  NULL);
  g_assert (element != NULL);

  xmlDocDump (stdout, gst_xml_write (element));

  exit (0);
}
