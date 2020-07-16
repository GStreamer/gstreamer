#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *e;

  gst_init (&argc, &argv);

  e = gst_element_factory_make ("pipeline", NULL);
  g_assert_nonnull (e);
  g_object_unref (e);

}
