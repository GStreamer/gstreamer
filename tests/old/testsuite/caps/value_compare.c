
#include <gst/gst.h>
#include <glib.h>

void
test1 (void)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  //GValue value3 = { 0 };
  //gboolean ret;

  g_value_init (&value1, G_TYPE_INT);
  g_value_set_int (&value1, 10);
  g_value_init (&value2, G_TYPE_INT);
  g_value_set_int (&value2, 20);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, G_TYPE_DOUBLE);
  g_value_set_double (&value1, 10);
  g_value_init (&value2, G_TYPE_DOUBLE);
  g_value_set_double (&value2, 20);
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, G_TYPE_STRING);
  g_value_set_string (&value1, "a");
  g_value_init (&value2, G_TYPE_STRING);
  g_value_set_string (&value2, "b");
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_LESS_THAN);
  g_assert (gst_value_compare (&value2, &value1) == GST_VALUE_GREATER_THAN);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

  g_value_init (&value1, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&value1, GST_MAKE_FOURCC ('a', 'b', 'c', 'd'));
  g_value_init (&value2, GST_TYPE_FOURCC);
  gst_value_set_fourcc (&value2, GST_MAKE_FOURCC ('1', '2', '3', '4'));
  g_assert (gst_value_compare (&value1, &value2) == GST_VALUE_UNORDERED);
  g_assert (gst_value_compare (&value1, &value1) == GST_VALUE_EQUAL);
  g_value_unset (&value1);
  g_value_unset (&value2);

}

int
main (int argc, char *argv[])
{

  gst_init (&argc, &argv);

  test1 ();

  return 0;

}
