/*
 * test with names
 * create a bunch of elements with NULL as name
 * make sure they get created with a decent name
 */

#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *element = NULL;
  int i = 0;

  /* init */
  gst_init (&argc, &argv);

  for (i = 0; i < 50; ++i) {
    /* create element */
    element = gst_element_factory_make ("identity", NULL);
    g_assert (GST_IS_ELEMENT (element));
    g_assert (gst_element_get_name (element) != NULL);
    g_print ("Created identity element with name %s\n",
        gst_element_get_name (element));
  }
  g_print ("Done !\n");
  return 0;
}
