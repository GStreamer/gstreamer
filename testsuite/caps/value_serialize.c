
#include <gst/gst.h>
#include <glib.h>

void
test1 (void)
{
  GValue value = { 0 };
  gboolean ret;

  g_value_init (&value, GST_TYPE_BUFFER);
  ret = gst_value_deserialize (&value, "1234567890abcdef");
  g_assert (ret);

}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  test1 ();

  return 0;

}
