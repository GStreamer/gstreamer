#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *element;
  GstElement *videosink, *audiosink;
  GList *testcaps;

  gst_init(&argc,&argv);

  audiosink = gst_elementfactory_make ("audiosink", "audiosink");
  g_assert (audiosink != NULL);
  videosink = gst_elementfactory_make ("videosink", "videosink");
  g_assert (videosink != NULL);

  testcaps = g_list_append (NULL,
				gst_caps_new_with_props ("test_caps",
							 "video/mpeg",
							 gst_props_new (
							   "mpegversion",  GST_PROPS_INT (1),
							   "systemstream", GST_PROPS_BOOLEAN (TRUE),
							   NULL)));


  element = gst_autoplug_caps_list (testcaps, gst_pad_get_caps_list (gst_element_get_pad (audiosink, "sink")),
		  gst_pad_get_caps_list (gst_element_get_pad (videosink, "sink")), NULL);
  g_assert (element != NULL);

  xmlDocDump (stdout, gst_xml_write (element));

  exit (0);
}
