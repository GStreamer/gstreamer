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
  GstElement *videosink, *audiosink;
  GstAutoplug *autoplugger;
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

  autoplugger = gst_autoplugfactory_make ("static");

  gtk_signal_connect (GTK_OBJECT (autoplugger), "new_object", new_object_added, NULL);

  element = gst_autoplug_caps_list (autoplugger, testcaps,
		  gst_pad_get_caps_list (gst_element_get_pad (audiosink, "sink")),
		  gst_pad_get_caps_list (gst_element_get_pad (videosink, "sink")),
		  NULL);
  g_assert (element != NULL);

  xmlDocDump (stdout, gst_xml_write (element));

  exit (0);
}
