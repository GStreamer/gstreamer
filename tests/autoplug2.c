#include <gst/gst.h>

static GstElement*
autoplug_caps (gchar *mime1, gchar *mime2)
{
  GList *caps1, *caps2;

  caps1 = g_list_append (NULL, gst_caps_new ("tescaps1", mime1));
  caps2 = g_list_append (NULL, gst_caps_new ("tescaps2", mime2));

  return gst_autoplug_caps_list (caps1, caps2, NULL);
}

int
main (int argc, char *argv[])
{
  GstElement *element;

  gst_init(&argc,&argv);

  element = autoplug_caps ("audio/mp3", "audio/raw");
  xmlSaveFile ("autoplug2_1.gst", gst_xml_write (element));

  element = autoplug_caps ("video/mpeg", "audio/raw");
  xmlSaveFile ("autoplug2_2.gst", gst_xml_write (element));

  element = gst_autoplug_caps_list (
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps3",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL))),
		  g_list_append (NULL, gst_caps_new("testcaps4","audio/raw")),
		  NULL);
  xmlSaveFile ("autoplug2_3.gst", gst_xml_write (element));

  element = gst_autoplug_caps_list (
		  g_list_append (NULL, gst_caps_new_with_props(
			  "testcaps5",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (FALSE),
			      NULL))),
		  g_list_append (NULL, gst_caps_new("testcaps6", "video/raw")),
		  NULL);
  xmlSaveFile ("autoplug2_4.gst", gst_xml_write (element));

  exit (0);
}
