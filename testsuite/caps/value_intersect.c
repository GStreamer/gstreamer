
#include <gst/gst.h>

void test1(void)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  gboolean ret;

  g_value_init(&src1, G_TYPE_INT);
  g_value_set_int(&src1, 10);
  g_value_init(&src2, G_TYPE_INT);
  g_value_set_int(&src1, 20);
  ret = gst_value_intersect(&dest, &src1, &src2);
  g_assert(ret == 0);
  g_print("ret = %d\n",ret);
}

void test2(void)
{
  GValue dest = { 0 };
  GValue src1 = { 0 };
  GValue src2 = { 0 };
  GValue item = { 0 };
  gboolean ret;

  g_value_init(&src1, GST_TYPE_FOURCC);
  gst_value_set_fourcc(&src1, GST_MAKE_FOURCC('Y','U','Y','2'));
  g_value_init(&src2, GST_TYPE_LIST);
  g_value_init(&item, GST_TYPE_FOURCC);
  gst_value_set_fourcc(&item, GST_MAKE_FOURCC('Y','U','Y','2'));
  gst_value_list_append_value (&src2, &item);
  gst_value_set_fourcc(&item, GST_MAKE_FOURCC('I','4','2','0'));
  gst_value_list_append_value (&src2, &item);
  gst_value_set_fourcc(&item, GST_MAKE_FOURCC('A','B','C','D'));
  gst_value_list_append_value (&src2, &item);
  ret = gst_value_intersect(&dest, &src1, &src2);
  g_print("ret = %d\n",ret);

  g_print("type = %s\n", g_type_name(G_VALUE_TYPE(&dest)));
  g_print("value = %s\n", g_strdup_value_contents(&dest));
}

int main(int argc, char *argv[])
{

  gst_init(&argc, &argv);

  test1();
  test2();

  return 0;

}


