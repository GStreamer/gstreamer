/* let's get gstreamer to work with swig and do perl */
#include <gst/gst.h>

void init ()
{
  int argc = 2;
  char **argv = NULL;

  argv = (char **) malloc (sizeof (char *) * 2);
  argv[0] = (char *) malloc (80);
  argv[1] = (char *) malloc (80);
  strcpy (argv[0], "swigged gst");
  strcpy (argv[1], "--gst-debug-mask=-1");
  
  gst_init (&argc, &argv);
}

void gobject_set (GtkObject* object, const gchar *first_arg_name, const gchar *first_arg_value)
{
  gtk_object_set (object, first_arg_name, first_arg_value);
}
