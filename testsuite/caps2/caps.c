
#include <gst/gst.h>


void test1(void)
{
  GstCaps2 *caps;
  GstCaps2 *caps2;

  g_print("type is %d\n", (int)gst_caps2_get_type());

  caps = gst_caps2_new_empty();
  g_assert(caps != NULL);
  gst_caps2_free(caps);

  caps = gst_caps2_new_any();
  g_assert(caps != NULL);
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw",
      "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==FALSE);
  g_assert(gst_caps2_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw",
      "_double", G_TYPE_DOUBLE, 100.0, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==FALSE);
  g_assert(gst_caps2_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw",
      "_fourcc", GST_TYPE_FOURCC, GST_MAKE_FOURCC('a','b','c','d'), NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==FALSE);
  g_assert(gst_caps2_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw",
      "_boolean", G_TYPE_BOOLEAN, TRUE, NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==FALSE);
  g_assert(gst_caps2_is_fixed(caps)==TRUE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw2", "_int", G_TYPE_INT, 100, NULL),
      NULL);
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==TRUE);
  g_assert(gst_caps2_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  caps2 = gst_caps2_copy(caps);
  g_assert(caps2 != NULL);
  g_assert(gst_caps2_is_empty(caps2)==FALSE);
  g_assert(gst_caps2_is_any(caps2)==FALSE);
  g_assert(gst_caps2_is_chained(caps2)==FALSE);
  g_assert(gst_caps2_is_fixed(caps2)==TRUE);
  g_print("%s\n", gst_caps2_to_string(caps));
  g_print("%s\n", gst_caps2_to_string(caps2));
  gst_caps2_free(caps);
  gst_caps2_free(caps2);

  caps = gst_caps2_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  gst_caps2_append (caps, 
      gst_caps2_new_simple("audio/raw", "_int", G_TYPE_INT, 200, NULL));
  g_assert(caps != NULL);
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==TRUE);
  g_assert(gst_caps2_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);

  caps = gst_caps2_new_simple("audio/raw", "_int", G_TYPE_INT, 100, NULL);
  g_assert(caps != NULL);
  gst_caps2_append_cap (caps, 
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 200, NULL));
  g_assert(gst_caps2_is_empty(caps)==FALSE);
  g_assert(gst_caps2_is_any(caps)==FALSE);
  g_assert(gst_caps2_is_chained(caps)==TRUE);
  g_assert(gst_caps2_is_fixed(caps)==FALSE);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);
}

void test2(void)
{
  GstCaps2 *caps1;
  GstCaps2 *caps2;
  GstCaps2 *caps;

  caps1 = gst_caps2_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 200, NULL),
      NULL);
  caps2 = gst_caps2_new_full(
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 100, NULL),
      gst_structure_new("audio/raw", "_int", G_TYPE_INT, 300, NULL),
      NULL);
  caps = gst_caps2_intersect(caps1, caps2);
  g_print("%s\n", gst_caps2_to_string(caps));
  gst_caps2_free(caps);
  gst_caps2_free(caps1);
  gst_caps2_free(caps2);

}

int main(int argc, char *argv[])
{
  gst_init(&argc, &argv);

  test1();
  test2();

  return 0;
}

