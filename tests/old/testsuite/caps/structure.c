
#include <gst/gst.h>


void
test1 (void)
{
  GstStructure *structure;

  g_print ("type is %d\n", (int) gst_structure_get_type ());

  structure = gst_structure_empty_new ("moo");
  g_assert (structure != NULL);
  g_assert (GST_IS_STRUCTURE (structure));
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);

  test1 ();

  return 0;
}
