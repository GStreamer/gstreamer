/* let's get gstreamer to work with swig and do perl */
#include <gst/gst.h>

void init ()
{
  int argc = 3;
  char **argv = NULL;

  g_module_open ("libgst.so", G_MODULE_BIND_LAZY);

  argv = (char **) malloc (sizeof (char *) * 3);
  argv[0] = (char *) malloc (80);
  argv[1] = (char *) malloc (80);
  argv[2] = (char *) malloc (80);
  strcpy (argv[0], "swigged gst");
  strcpy (argv[1], "--gst-debug-mask=0");
  strcpy (argv[2], "--gst-info-mask=0");
  
  gst_init (&argc, &argv);
}

// FIXME: find a way to get the actual macro from gobject2gtk in here
void gobject_set (GstElement* element, const gchar *first_arg_name, const gchar *first_arg_value)
{
  gtk_object_set ((GtkObject*) element, first_arg_name, first_arg_value, NULL);
}

// FIXME: the typecast should be done using typemaps or some other way

void wrap_gst_bin_add (GstElement *bin, GstElement *element)
{
  gst_bin_add ((GstBin *) bin, element);
}

// FIXME: no wrapper, the actual enums should be used
gint gst_element_set_state_play (GstElement *bin)
{
  gst_element_set_state (bin, GST_STATE_PLAYING);
}

// FIXME: no wrapper, typeconversion should be automatic
gboolean wrap_gst_bin_iterate (GstElement *bin)
{
  gst_bin_iterate (bin);
}
