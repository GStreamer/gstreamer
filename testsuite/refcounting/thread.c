#include <gst/gst.h>

#define ITERS 100000
#include <stdlib.h>
#include "mem.h"

static GstElement *
create_thread (void)
{
  GstElement *thread;
  GstElement *element;

  thread = gst_thread_new ("testthread");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  gst_bin_add (GST_BIN (thread), element);
  element = gst_element_new ();
  gst_element_set_name (element, "test2");
  gst_bin_add (GST_BIN (thread), element);

  return thread;
}

static GstElement *
create_thread_ghostpads (void)
{
  GstElement *thread;
  GstElement *element1, *element2;

  thread = gst_thread_new ("testthread");
  element1 = gst_element_new ();
  gst_element_set_name (element1, "test1");
  gst_element_add_pad (element1, gst_pad_new ("src1", GST_PAD_SRC));
  gst_bin_add (GST_BIN (thread), element1);
  element2 = gst_element_new ();
  gst_element_set_name (element2, "test2");
  gst_element_add_pad (element2, gst_pad_new ("sink1", GST_PAD_SINK));
  gst_bin_add (GST_BIN (thread), element2);
  gst_element_link (element1, "src1", element2, "sink1");
  gst_element_add_ghost_pad (thread, gst_element_get_pad (element2, "sink1"),
      "sink1");

  return thread;
}

static void
add_remove_test1 (void)
{
  GstElement *thread;
  GstElement *element;

  thread = gst_thread_new ("testthread");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (thread), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_bin_remove (GST_BIN (thread), element);

  gst_object_unref (GST_OBJECT (thread));
}

static void
add_remove_test2 (void)
{
  GstElement *thread;
  GstElement *element;

  thread = gst_thread_new ("testthread");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  gst_object_ref (GST_OBJECT (element));
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (thread), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  gst_bin_remove (GST_BIN (thread), element);
  g_assert (!GST_OBJECT_FLOATING (element));
  g_assert (!GST_OBJECT_DESTROYED (element));

  gst_object_unref (GST_OBJECT (element));
  g_assert (GST_OBJECT_DESTROYED (element));
  gst_object_unref (GST_OBJECT (element));

  gst_object_unref (GST_OBJECT (thread));
}

static void
add_remove_test3 (void)
{
  GstElement *thread;
  GstElement *element;

  thread = gst_thread_new ("testthread");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (thread), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  gst_object_unref (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (thread), "test1") == NULL);

  gst_object_unref (GST_OBJECT (thread));
}

static void
add_remove_test4 (void)
{
  GstElement *thread, *thread2;
  GstElement *element;

  thread = gst_thread_new ("testthread");
  element = gst_element_new ();
  gst_element_set_name (element, "test1");
  g_assert (GST_OBJECT_FLOATING (element));
  gst_bin_add (GST_BIN (thread), element);
  g_assert (!GST_OBJECT_FLOATING (element));

  thread2 = create_thread ();
  g_assert (GST_OBJECT_FLOATING (thread2));
  gst_bin_add (GST_BIN (thread), thread2);
  g_assert (!GST_OBJECT_FLOATING (thread2));

  gst_object_unref (GST_OBJECT (thread2));
  g_assert (gst_bin_get_by_name (GST_BIN (thread), "testthread") == NULL);
  gst_object_unref (GST_OBJECT (element));
  g_assert (gst_bin_get_by_name (GST_BIN (thread), "test1") == NULL);

  gst_object_unref (GST_OBJECT (thread));
}

int
main (int argc, gchar * argv[])
{
  GstElement *thread, *element;
  long usage1;
  gint i, iters;

  gst_init (&argc, &argv);

  if (argc == 2)
    iters = atoi (argv[1]);
  else
    iters = ITERS;

  g_print ("starting test\n");
  usage1 = vmsize ();

  thread = gst_thread_new ("somethread");
  gst_object_unref (GST_OBJECT (thread));
  g_print ("create/unref new thread %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/unref %d threads %ld\n", iters, vmsize () - usage1);

  thread = gst_thread_new ("somethread");
  g_assert (GST_OBJECT_FLOATING (thread));
  gst_object_ref (GST_OBJECT (thread));
  gst_object_sink (GST_OBJECT (thread));
  g_assert (!GST_OBJECT_FLOATING (thread));
  gst_object_unref (GST_OBJECT (thread));
  g_print ("create/ref/sink/unref new thread %ld\n", vmsize () - usage1);


  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_ref (GST_OBJECT (thread));
    gst_object_sink (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/ref/sink/unref %d threads %ld\n", iters, vmsize () - usage1);

  thread = gst_thread_new ("somethread");
  g_assert (!GST_OBJECT_DESTROYED (thread));
  gst_object_unref (GST_OBJECT (thread));
  g_assert (GST_OBJECT_DESTROYED (thread));
  gst_object_unref (GST_OBJECT (thread));
  g_print ("create/destroy/unref new thread %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_unref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/destroy/unref %d thread %ld\n", iters, vmsize () - usage1);

  thread = gst_thread_new ("somethread");
  gst_object_ref (GST_OBJECT (thread));
  gst_object_unref (GST_OBJECT (thread));
  gst_object_unref (GST_OBJECT (thread));
  g_print ("create/ref/unref/unref new thread %ld\n", vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_ref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/ref/unref/unref %d thread %ld\n", iters, vmsize () - usage1);

  thread = gst_thread_new ("somethread");
  gst_object_ref (GST_OBJECT (thread));
  gst_object_unref (GST_OBJECT (thread));
  gst_object_unref (GST_OBJECT (thread));
  gst_object_unref (GST_OBJECT (thread));
  g_print ("craete/ref/destroy/unref/unref new thread %ld\n",
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_ref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("craete/ref/destroy/unref/unref %d threads %ld\n", iters,
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    gst_object_ref (GST_OBJECT (thread));
    gst_element_set_name (thread, "testing123");
    gst_object_unref (GST_OBJECT (thread));
    gst_element_set_name (thread, "testing123");
    gst_object_unref (GST_OBJECT (thread));
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("craete/ref/destroy/unref/unref %d threads with name %ld\n", iters,
      vmsize () - usage1);

  thread = gst_thread_new ("somethread");
  for (i = 0; i < iters; i++) {
    gst_element_set_name (thread, "testing");
  }
  gst_object_unref (GST_OBJECT (thread));
  g_print ("set name %d times %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = gst_thread_new ("somethread");
    element = gst_element_new ();
    gst_element_set_name (element, "test1");
    gst_bin_add (GST_BIN (thread), element);
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/unref %d thread with one element %ld\n", iters,
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    thread = create_thread ();
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/unref %d thread with children %ld\n", iters,
      vmsize () - usage1);

  for (i = 0; i < iters / 2; i++) {
    thread = create_thread_ghostpads ();
    gst_object_unref (GST_OBJECT (thread));
  }
  g_print ("create/unref %d thread with children and ghostpads %ld\n",
      iters / 2, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test1 ();
  }
  g_print ("add/remove test1 %d in thread %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test2 ();
  }
  g_print ("add/remove test2 %d in thread %ld\n", iters, vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test3 ();
  }
  g_print ("add/destroy/remove test3 %d in thread %ld\n", iters,
      vmsize () - usage1);

  for (i = 0; i < iters; i++) {
    add_remove_test4 ();
  }
  g_print ("add/destroy/remove test4 %d in thread %ld\n", iters,
      vmsize () - usage1);

  g_print ("leaked: %ld\n", vmsize () - usage1);

  return (vmsize () - usage1 ? -1 : 0);
}
