#include <gst/gst.h>

/* This tests that gst_init() doesn't segfault when passed two NULLs as
 * parameters, and that it doesn't fail if gst_init() happens to get called
 * a second time. */
int
main (int argc, char *argv[])
{
  gst_init (NULL, NULL);
  gst_init (&argc, &argv);

  return 0;
}
