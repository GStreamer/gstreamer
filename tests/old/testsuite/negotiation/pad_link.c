#include <gst/gst.h>

/* this test checks that gst_pad_link takes into account all available 
 * information when trying to link two pads.
 * Because identity proxies caps, the caps in the first and second link 
 * must be compatible for this pipeline to work.
 * Since they are not, the second linkig attempt should fail.
 */

gint
main (int argc, gchar ** argv)
{
  GstElement *src, *identity, *sink;
  GstCaps *one, *two;

  gst_init (&argc, &argv);

  /* create incompatible caps */
  src = gst_element_factory_make ("fakesrc", NULL);
  g_assert (src);
  identity = gst_element_factory_make ("identity", NULL);
  g_assert (identity);
  sink = gst_element_factory_make ("fakesink", NULL);
  g_assert (sink);

  one = gst_caps_from_string ("some/mime");
  two = gst_caps_from_string ("other/mime");

  g_assert (GST_PAD_LINK_SUCCESSFUL (gst_pad_link_filtered (gst_element_get_pad
              (src, "src"), gst_element_get_pad (identity, "sink"), one)));
  g_assert (!GST_PAD_LINK_SUCCESSFUL (gst_pad_link_filtered (gst_element_get_pad
              (identity, "src"), gst_element_get_pad (sink, "sink"), two)));

  return 0;
}
