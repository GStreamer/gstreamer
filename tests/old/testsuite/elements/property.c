/*
 * test for setting and getting of object properties
 * creates a fakesrc
 * sets silent (boolean), name (string), and sizemin (int)
 * then retrieves the set values and compares
 * thomas@apestaart.org
 * originally written for 0.4.0
 */

#include <string.h>
#include <gst/gst.h>

GstElement *
element_create (char *name, char *element)
  /*
   * create the element
   * print an error if it can't be created
   * return NULL if it couldn't be created
   * return element if it did work
   */
{
  GstElement *el = NULL;

  el = (GstElement *) gst_element_factory_make (element, name);
  if (el == NULL) {
    fprintf (stderr, "Could not create element %s (%s) !\n", name, element);
    return NULL;
  } else
    return el;
}

int
main (int argc, char *argv[])
{
  GstElement *src;
  gint retval = 0;
  gboolean silent_set, silent_get;
  gint sizemin_set, sizemin_get;
  gchar *name_set, *name_get;

  /* init */
  gst_init (&argc, &argv);

  /* create */
  g_print ("Creating element\n");
  if (!(src = element_create ("src", "fakesrc")))
    return 1;

  /* set */
  silent_set = TRUE;
  sizemin_set = 1;
  name_set = g_strdup_printf ("test");

  gst_element_set (src,
      "name", name_set, "sizemin", sizemin_set, "silent", silent_set, NULL);
  /* get */
  gst_element_get (src,
      "name", &name_get, "sizemin", &sizemin_get, "silent", &silent_get, NULL);

  /* compare */
  if (sizemin_set != sizemin_get) {
    g_print ("sizemin: set value %d differs from returned value %d\n",
	sizemin_set, sizemin_get);
    retval = 1;
  } else
    g_print ("name: set right\n");

  if (silent_set != silent_get) {
    g_print ("silent: set value %s differs from returned value %s\n",
	silent_set ? "TRUE" : "FALSE", silent_get ? "TRUE" : "FALSE");
    retval = 1;
  } else
    g_print ("silent: set right\n");

  if (strcmp (name_set, name_get) != 0) {
    g_print ("name: set value %s differs from returned value %s\n",
	name_set, name_get);
    retval = 1;
  } else
    g_print ("name: set right\n");

  return retval;
}
