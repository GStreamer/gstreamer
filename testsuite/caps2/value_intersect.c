
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
  g_print("ret = %d\n",ret);

}

int main(int argc, char *argv[])
{

  gst_init(&argc, &argv);

  test1();

  return 0;

}


