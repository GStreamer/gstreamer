#include <gst/gst.h>

#define ITERS 100000
#include "mem.h"

static void
print_object_props (GstObject *object)
{
  g_print ("name %s\n", gst_object_get_name (object));
  g_print ("flags 0x%08lx\n", GST_FLAGS (object));
}

int
main (int argc, gchar *argv[])
{
  GstObject *object;
  long usage1, usage2;
  gint i;

  gst_init (&argc, &argv);

  g_print ("creating new object\n");
  object = gst_object_new ();
  usage1 = vmsize();
  print_object_props (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);

  g_print ("creating new object\n");
  object = gst_object_new ();
  g_assert (GST_OBJECT_FLOATING (object));
  print_object_props (object);
  g_print ("sink new object %ld\n", vmsize()-usage1);
  gst_object_ref (object);
  gst_object_sink (object);
  g_assert (!GST_OBJECT_FLOATING (object));
  print_object_props (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);

  for (i=0; i<ITERS;i++) {
    object = gst_object_new ();
    gst_object_unref (object);
  }
  g_print ("unref 100000 object %ld\n", vmsize()-usage1);

  g_print ("creating new object\n");
  object = gst_object_new ();
  g_assert (!GST_OBJECT_DESTROYED (object));
  print_object_props (object);
  g_print ("destroy new object %ld\n", vmsize()-usage1);
  gst_object_destroy (object);
  g_assert (GST_OBJECT_DESTROYED (object));
  print_object_props (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);
  
  for (i=0; i<ITERS;i++) {
    object = gst_object_new ();
    gst_object_destroy (object);
    gst_object_unref (object);
  }
  g_print ("destroy/unref 100000 object %ld\n", vmsize()-usage1);

  g_print ("creating new object\n");
  object = gst_object_new ();
  gst_object_ref (object);
  print_object_props (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);
  
  for (i=0; i<ITERS;i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("destroy/unref 100000 object %ld\n", vmsize()-usage1);

  g_print ("creating new object\n");
  object = gst_object_new ();
  gst_object_ref (object);
  print_object_props (object);
  gst_object_destroy (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);
  g_print ("unref new object %ld\n", vmsize()-usage1);
  gst_object_unref (object);
  
  for (i=0; i<ITERS;i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_destroy (object);
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("destroy/unref 100000 object %ld\n", vmsize()-usage1);

  for (i=0; i<ITERS;i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_set_name (object, "testing123");
    gst_object_destroy (object);
    gst_object_set_name (object, "testing123");
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("destroy/unref 100000 object %ld\n", vmsize()-usage1);

  object = gst_object_new ();
  for (i=0; i<ITERS;i++) {
    gst_object_set_name (object, "testing");
  }
  gst_object_unref (object);
  g_print ("destroy/unref 100000 object %ld\n", vmsize()-usage1);

  g_print ("leaked: %ld\n", vmsize()-usage1);

  return 0;
}
