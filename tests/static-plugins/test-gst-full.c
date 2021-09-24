#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *e;

  gst_init (&argc, &argv);

  /* -Bsymbolic option is introducing a regression where this variable
   * were duplicated over the use in a dynamical use of libgstreamer-full.so */
  g_assert_nonnull (_gst_caps_features_any);
  g_assert_nonnull (_gst_caps_features_memory_system_memory);

  e = gst_element_factory_make ("pipeline", NULL);
  g_assert_nonnull (e);
  g_object_unref (e);
}
