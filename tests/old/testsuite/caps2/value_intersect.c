
#include <gst/gst.h>
#include <glib.h>

void test1(void)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GValue value3 = { 0 };
  gboolean ret;

  g_value_init(&value1, G_TYPE_INT);
  g_value_set_int(&value1, 10);
  g_value_init(&value2, G_TYPE_INT);
  g_value_set_int(&value2, 20);
  ret = gst_value_intersect(&value3, &value1, &value2);
  g_assert(ret==FALSE);
  ret = gst_value_intersect(&value3, &value1, &value1);
  g_assert(ret==TRUE);
  g_value_unset(&value1);
  g_value_unset(&value2);
  g_value_unset(&value3);

  g_value_init(&value1, G_TYPE_DOUBLE);
  g_value_set_double(&value1, 10);
  g_value_init(&value2, G_TYPE_DOUBLE);
  g_value_set_double(&value2, 20);
  ret = gst_value_intersect(&value3, &value1, &value2);
  g_assert(ret==FALSE);
  ret = gst_value_intersect(&value3, &value1, &value1);
  g_assert(ret==TRUE);
  g_value_unset(&value1);
  g_value_unset(&value2);
  g_value_unset(&value3);

  g_value_init(&value1, G_TYPE_STRING);
  g_value_set_string(&value1, "a");
  g_value_init(&value2, G_TYPE_STRING);
  g_value_set_string(&value2, "b");
  ret = gst_value_intersect(&value3, &value1, &value2);
  g_assert(ret==FALSE);
  ret = gst_value_intersect(&value3, &value1, &value1);
  g_assert(ret==TRUE);
  g_value_unset(&value1);
  g_value_unset(&value2);
  g_value_unset(&value3);

  g_value_init(&value1, GST_TYPE_FOURCC);
  gst_value_set_fourcc(&value1, GST_MAKE_FOURCC('a','b','c','d'));
  g_value_init(&value2, GST_TYPE_FOURCC);
  gst_value_set_fourcc(&value2, GST_MAKE_FOURCC('1','2','3','4'));
  ret = gst_value_intersect(&value3, &value1, &value2);
  g_assert(ret==FALSE);
  ret = gst_value_intersect(&value3, &value1, &value1);
  g_assert(ret==TRUE);
  g_value_unset(&value1);
  g_value_unset(&value2);
  g_value_unset(&value3);
}

void test2(void)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GValue value3 = { 0 };
  GValue value4 = { 0 };
  GValue dest = { 0 };
  gboolean ret;

  g_value_init(&value1, GST_TYPE_INT_RANGE);
  gst_value_set_int_range(&value1, 10, 30);
  g_value_init(&value2, GST_TYPE_INT_RANGE);
  gst_value_set_int_range(&value2, 20, 40);
  g_value_init(&value3, GST_TYPE_INT_RANGE);
  gst_value_set_int_range(&value3, 30, 50);
  g_value_init(&value4, GST_TYPE_INT_RANGE);
  gst_value_set_int_range(&value4, 40, 60);
  ret = gst_value_intersect(&dest, &value1, &value2);
  g_assert(ret==TRUE);
  g_value_unset(&dest);
  ret = gst_value_intersect(&dest, &value1, &value3);
  g_assert(ret==TRUE);
  g_value_unset(&dest);
  ret = gst_value_intersect(&dest, &value1, &value4);
  g_assert(ret==FALSE);

  g_value_unset(&value1);
  g_value_unset(&value2);
  g_value_unset(&value3);
  g_value_unset(&value4);

}

int main(int argc, char *argv[])
{

  gst_init(&argc, &argv);

  test1();
  test2();

  return 0;

}


