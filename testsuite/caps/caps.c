
#include <gst/gst.h>


void test1(void)
{
  GstCaps *caps;
  GstCaps *caps2;

  g_print("type is %d\n", (int)gst_caps_get_type());

  caps = gst_caps_new_empty();
  g_assert(caps != NULL);
  gst_caps_free(caps);

  caps = gst_caps_new_any();
  g_assert(caps != NULL);
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw",
      "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==TRUE);
  g_assert(gst_caps_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw",
      "_double", G_TYPE_DOUBLE, 100.0, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==TRUE);
  g_assert(gst_caps_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw",
      "_fourcc", GST_TYPE_FOURCC, GST_MAKE_FOURCC('a','b','c','d'), NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==TRUE);
  g_assert(gst_caps_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw",
      "_boolean", G_TYPE_BOOLEAN, TRUE, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==TRUE);
  g_assert(gst_caps_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw2", "_int", G_TYPE_INT, 100, NULL),
      NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==FALSE);
  g_assert(gst_caps_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  caps2 = gst_caps_copy(caps);
  g_assert(caps2 != NULL);
  g_assert(gst_caps_is_empty(caps2)==FALSE);
  g_assert(gst_caps_is_any(caps2)==FALSE);
  g_assert(gst_caps_is_simple(caps2)==TRUE);
  g_assert(gst_caps_is_fixed(caps2)==TRUE);
  g_print("%s\n", gst_caps_to_string(caps));
  g_print("%s\n", gst_caps_to_string(caps2));
  gst_caps_free(caps);
  gst_caps_free(caps2);

  caps = gst_caps_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  gst_caps_append (caps, 
      gst_caps_new_simple("audio/raw", "_int", G_TYPE_INT, 200, NULL));
  g_assert(caps != NULL);
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==FALSE);
  g_assert(gst_caps_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);

  caps = gst_caps_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  gst_caps_append_structure (caps, 
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 200, NULL));
  g_assert(gst_caps_is_empty(caps)==FALSE);
  g_assert(gst_caps_is_any(caps)==FALSE);
  g_assert(gst_caps_is_simple(caps)==FALSE);
  g_assert(gst_caps_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);
}

void test2(void)
{
  GstCaps *caps1;
  GstCaps *caps2;
  GstCaps *caps;

  caps1 = gst_caps_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 200, NULL),
      NULL);
  caps2 = gst_caps_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 300, NULL),
      NULL);
  caps = gst_caps_intersect(caps1, caps2);
  g_print("%s\n", gst_caps_to_string(caps));
  gst_caps_free(caps);
  gst_caps_free(caps1);
  gst_caps_free(caps2);

}

int main(int argc, char *argv[])
{
  gst_init(&argc, &argv);

  test1();
  test2();

  return 0;
}

