#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *element;
  GstElement *sink1, *sink2;
  GstAutoplug *autoplug;
  GstAutoplug *autoplug2;

  gst_init(&argc,&argv);

  sink1 = gst_elementfactory_make ("videosink", "videosink");
  sink2 = gst_elementfactory_make ("osssink", "osssink");

  autoplug = gst_autoplugfactory_make ("staticrender");
  autoplug2 = gst_autoplugfactory_make ("static");
  
  element = gst_autoplug_to_renderers (autoplug, 
		  g_list_append (NULL, gst_caps_new ("mp3caps", "audio/mp3")), sink2, NULL);
  xmlSaveFile ("autoplug3_1.gst", gst_xml_write (element));

  element = gst_autoplug_to_renderers (autoplug, 
		  g_list_append (NULL, gst_caps_new ("mpeg1caps", "video/mpeg")), sink1, NULL);
  if (element) {
    xmlSaveFile ("autoplug3_2.gst", gst_xml_write (element));
  }

  element = gst_autoplug_to_caps (autoplug2,
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps3",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL))),
		  g_list_append (NULL, gst_caps_new("testcaps4","audio/raw")),
		  NULL);
  if (element) {
    xmlSaveFile ("autoplug3_3.gst", gst_xml_write (element));
  }

  element = gst_autoplug_to_caps (autoplug2,
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps5",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (FALSE),
			      NULL))),
		  g_list_append (NULL, gst_caps_new("testcaps6", "video/raw")),
		  NULL);
  if (element) {
    xmlSaveFile ("autoplug3_4.gst", gst_xml_write (element));
  }

  element = gst_autoplug_to_caps (autoplug2,
		  g_list_append (NULL, gst_caps_new(
			  "testcaps7",
			  "video/avi")),
		  g_list_append (NULL, gst_caps_new("testcaps8", "video/raw")),
		  g_list_append (NULL, gst_caps_new("testcaps9", "audio/raw")),
		  NULL);
  if (element) {
    xmlSaveFile ("autoplug3_5.gst", gst_xml_write (element));
  }

  element = gst_autoplug_to_caps (autoplug2,
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps10",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL))),
		  g_list_append (NULL, gst_caps_new("testcaps10", "video/raw")),
		  g_list_append (NULL, gst_caps_new("testcaps11", "audio/raw")),
		  NULL);
  if (element) {
    xmlSaveFile ("autoplug3_6.gst", gst_xml_write (element));
  }

  sink1 = gst_elementfactory_make ("videosink", "videosink");
  sink2 = gst_elementfactory_make ("osssink", "osssink");
  
  element = gst_autoplug_to_renderers (autoplug,
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps10",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL))),
		  sink1,
		  sink2,
		  NULL);
  if (element) {
    xmlSaveFile ("autoplug3_7.gst", gst_xml_write (element));
  }

  exit (0);
}
