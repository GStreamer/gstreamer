#include <gst/gst.h>

static GstElement*
autoplug_caps (GstAutoplug *autoplug, gchar *mime1, gchar *mime2)
{
  GstCaps *caps1, *caps2;

  caps1 = gst_caps_new ("tescaps1", mime1, NULL);
  caps2 = gst_caps_new ("tescaps2", mime2, NULL);

  return gst_autoplug_to_caps (autoplug, caps1, caps2, NULL);
}

int
main (int argc, char *argv[])
{
  GstElement *element;
  GstAutoplug *autoplug;

  gst_init(&argc,&argv);

  autoplug = gst_autoplug_factory_make ("static");
  
  g_print ("Autoplugging between audio/x-mp3 and audio/raw ...\n");
  if ((element = autoplug_caps (autoplug, "audio/x-mp3", "audio/raw")) == NULL)
    g_print ("Could not autoplug between audio/x-mp3 and audio/raw !\n");
  xmlSaveFile ("autoplug2_1.gst", gst_xml_write (element));

  g_print ("Autoplugging between video/mpeg and audio/raw ...\n");
  if ((element = autoplug_caps (autoplug, "video/mpeg", "audio/raw")) == NULL)
    g_print ("Could not autoplug between video/mpeg and audio/raw !\n");
  else
    xmlSaveFile ("autoplug2_2.gst", gst_xml_write (element));

  if ((element = gst_autoplug_to_caps (autoplug,
		  gst_caps_new(
			  "testcaps3",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL)),
		  gst_caps_new("testcaps4","audio/raw", NULL),
		  NULL)) == NULL)
    g_print ("Could not autoplug between video/mpeg system stream and audio/raw !\n");
  else
    xmlSaveFile ("autoplug2_3.gst", gst_xml_write (element));

  element = gst_autoplug_to_caps (autoplug,
		  gst_caps_new(
			  "testcaps5",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (FALSE),
			      NULL)),
		  gst_caps_new("testcaps6", "video/raw", NULL),
		  NULL);
  g_assert (GST_IS_ELEMENT (element));
  xmlSaveFile ("autoplug2_4.gst", gst_xml_write (element));

  element = gst_autoplug_to_caps (autoplug,
		  gst_caps_new(
			  "testcaps7",
			  "video/x-msvideo", NULL),
		  gst_caps_new("testcaps8", "video/raw", NULL),
		  gst_caps_new("testcaps9", "audio/raw", NULL),
		  NULL);
  g_assert (GST_IS_ELEMENT (element));
  xmlSaveFile ("autoplug2_5.gst", gst_xml_write (element));

  element = gst_autoplug_to_caps (autoplug,
		  gst_caps_new(
			  "testcaps10",
			  "video/mpeg",
			  gst_props_new (
			      "mpegversion",  GST_PROPS_INT (1),
			      "systemstream", GST_PROPS_BOOLEAN (TRUE),
			      NULL)),
		  gst_caps_new("testcaps10", "video/raw", NULL),
		  gst_caps_new("testcaps11", "audio/raw", NULL),
		  NULL);
  g_assert (GST_IS_ELEMENT (element));
  xmlSaveFile ("autoplug2_6.gst", gst_xml_write (element));

  exit (0);
}
