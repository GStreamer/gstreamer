#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <gst/gst.h>

/*
 * find the type of a media file and display it's properties
 **/

gboolean FOUND = FALSE;
int iterations;
int max_iterations = 100;

void
gst_caps_print (const char *filename, GstCaps *caps)
{
  gchar *caps_str = gst_caps_to_string (caps);
  g_print ("%s - %s\n", filename, caps_str);
  g_free (caps_str);
}

void
have_type_handler (GstElement *typefind, guint probability, GstCaps *caps, gpointer filename)
{
  gst_caps_print (filename, caps);
  FOUND = TRUE;
}

int
main (int argc, char *argv[])
{
  guint i = 1;
  GstElement *pipeline;
  GstElement *source, *typefind;

  setlocale (LC_ALL, "");

  gst_init (&argc, &argv);

  if (argc < 2) { 
    g_print ("Please give a filename to typefind\n\n");
    return 1;
  }
  while (i < argc) {
    FOUND = FALSE;
    iterations = 0;
    pipeline = gst_pipeline_new (NULL);
    source = gst_element_factory_make ("filesrc", "source");
    g_assert (GST_IS_ELEMENT (source));
    g_object_set (source, "location", argv[i], NULL);
    typefind = gst_element_factory_make ("typefind", "typefind");
    g_assert (GST_IS_ELEMENT (typefind));
    gst_bin_add_many (GST_BIN (pipeline), source, typefind, NULL);
    gst_element_link (source, typefind);
    g_signal_connect (G_OBJECT (typefind), "have-type", 
		      G_CALLBACK (have_type_handler), argv[i]);

    /* set to play */
    gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

    while (!FOUND){
      if (!gst_bin_iterate (GST_BIN (pipeline)))
	break;
      iterations++;
      if(iterations >= max_iterations){
	break;
      }
    }
    if (!FOUND) {
      g_print ("%s - No type found\n", argv[i]);
    }
    i++;
    /* g_object_unref (pipeline); */
  }
  return 0;
}
