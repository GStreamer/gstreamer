#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <gst/gst.h>

/*
 * find the type of a media file and display it's properties
 **/

gboolean FOUND = FALSE;

void
gst_caps_print (GstCaps *caps)
{
  while (caps) {
    g_print ("%s (%s)\n", caps->name, gst_caps_get_mime (caps));
    if (caps->properties) {
	    g_print ("has properties\n");
    }
    caps = caps->next;
  }
}

void
have_type_handler (GstElement *typefind, gpointer data)
{
  GstCaps *caps = (GstCaps *) data;
  gst_caps_print (caps);
  FOUND = TRUE;
}

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstElement *source, *typefind;

  gst_init (&argc, &argv);

  if (argc < 2) { 
    g_print ("Please give a filename to typefind\n\n");
    exit (1);
  }
  pipeline = gst_pipeline_new (NULL);
  source = gst_element_factory_make ("filesrc", "source");
  g_assert (GST_IS_ELEMENT (source));
  g_object_set (source, "location", argv[1], NULL);
  typefind = gst_element_factory_make ("typefind", "typefind");
  g_assert (GST_IS_ELEMENT (typefind));
  gst_bin_add_many (GST_BIN (pipeline), source, typefind, NULL);
  gst_element_link (source, typefind);
  g_signal_connect (G_OBJECT (typefind), "have-type", 
		    G_CALLBACK (have_type_handler), NULL);

  /* set to play */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  while (gst_bin_iterate (GST_BIN (pipeline)) && !FOUND) ;
  if (!FOUND) {
    g_print ("No type found\n");
    return 1;
  }
  return 0;
}
