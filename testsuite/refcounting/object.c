#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

int
main (int argc, gchar * argv[])
{
  GstObject *object, *object2;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting test with %d iterations\n", iters);
  usage1 = vmsize ();
  object = gst_object_new ();
  gst_object_unref (object);
  g_print ("create/unref new object %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_unref (object);

  }
  g_print ("create/unref %d object %ld\n", iters, vmsize () - usage1);

  object = gst_object_new ();
  g_assert (GST_OBJECT_FLOATING (object));
  gst_object_ref (object);
  gst_object_sink (object);
  g_assert (!GST_OBJECT_FLOATING (object));
  gst_object_unref (object);
  g_print ("create/ref/sink/unref new object %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_sink (object);
    gst_object_unref (object);
  }
  g_print ("create/ref/sink/unref %d object %ld\n", iters, vmsize () - usage1);

  object = gst_object_new ();
  g_assert (!GST_OBJECT_DESTROYED (object));
  gst_object_unref (object);
  g_assert (GST_OBJECT_DESTROYED (object));
  gst_object_unref (object);
  g_print ("create/destroy/unref new object %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("destroy/unref %d object %ld\n", iters, vmsize () - usage1);

  object = gst_object_new ();
  gst_object_ref (object);
  gst_object_unref (object);
  gst_object_unref (object);
  g_print ("create/ref/unref/unref new object %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("create/ref/unref/unref %d object %ld\n", iters, vmsize () - usage1);

  object = gst_object_new ();
  gst_object_ref (object);
  gst_object_unref (object);
  gst_object_unref (object);
  gst_object_unref (object);
  g_print ("create/ref/destroy/unref/unref new object %ld\n",
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_unref (object);
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("create/ref/destroy/unref/unref %d object %ld\n", iters,
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    gst_object_ref (object);
    gst_object_set_name (object, "testing123");
    gst_object_unref (object);
    gst_object_set_name (object, "testing123");
    gst_object_unref (object);
    gst_object_unref (object);
  }
  g_print ("create/ref/destroy/unref/unref %d object  with name %ld\n", iters,
      vmsize () - usage1);

  object = gst_object_new ();
  for (i = 0; i < iters; i++) {
    gst_object_set_name (object, "testing");
  }
  gst_object_unref (object);
  g_print ("create/set name/unref %d object %ld\n", iters, vmsize () - usage1);

  object = gst_object_new ();
  object2 = gst_object_new ();
  g_assert (GST_OBJECT_FLOATING (object));
  g_assert (GST_OBJECT_FLOATING (object2));

  gst_object_set_parent (object, object2);
  g_assert (GST_OBJECT_FLOATING (object2));
  g_assert (!GST_OBJECT_FLOATING (object));

  g_print ("parentage flags set_parent ok %ld\n", vmsize () - usage1);

  gst_object_ref (object);
  gst_object_unparent (object);
  g_assert (GST_OBJECT_FLOATING (object2));
  g_assert (!GST_OBJECT_FLOATING (object));
  g_assert (gst_object_get_parent (object) == NULL);

  g_print ("parentage flags unparent ok %ld\n", vmsize () - usage1);

  gst_object_set_parent (object, object2);
  g_assert (GST_OBJECT_FLOATING (object2));
  g_assert (!GST_OBJECT_FLOATING (object));
  g_assert (gst_object_get_parent (object) == object2);

  gst_object_unref (object);
  g_assert (GST_OBJECT_DESTROYED (object));
  g_assert (!GST_OBJECT_FLOATING (object));
  g_assert (gst_object_get_parent (object) == NULL);
  gst_object_unref (object);

  g_print ("parentage flags destroy ok %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    object = gst_object_new ();
    object2 = gst_object_new ();
    gst_object_set_parent (object2, object);
    gst_object_unref (object);
    gst_object_unref (object2);
  }
  g_print ("create/unref %d 2 parented objects %ld\n", iters,
      vmsize () - usage1);

  g_print ("leaked: %ld\n", vmsize () - usage1);

  return (vmsize () - usage1 ? -1 : 0);
}
