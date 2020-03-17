#include <gst/gst.h>
#include <gst/gstinitstaticplugins.h>

int
main (int argc, char *argv[])
{
  GstElement *e;

  gst_init (&argc, &argv);
  gst_init_static_plugins ();

  e = gst_element_factory_make ("identity", NULL);
  g_assert_nonnull (e);
  g_object_unref (e);

  e = gst_element_factory_make ("alpha", NULL);
  g_assert_nonnull (e);
  g_object_unref (e);

  return 0;
}
