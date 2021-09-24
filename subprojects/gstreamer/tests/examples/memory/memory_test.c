#include <gst/gst.h>

#include "my-memory.h"
#include "my-vidmem.h"

int
main (int argc, char **argv)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstAllocationParams params;
  GstMapInfo info;
  guint f, w, h;

  gst_init (&argc, &argv);

  /* memory using the default API */
  my_memory_init ();

  alloc = gst_allocator_find ("MyMemory");

  gst_allocation_params_init (&params);
  mem = gst_allocator_alloc (alloc, 1024, &params);

  gst_memory_map (mem, &info, GST_MAP_READ);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  gst_object_unref (alloc);

  /* allocator with custom alloc API */
  my_vidmem_init ();

  /* we can get the allocator but we can only make objects from it when we know
   * the API */
  alloc = gst_allocator_find ("MyVidmem");

  /* use custom api to alloc */
  mem = my_vidmem_alloc (0, 640, 480);
  g_assert (my_is_vidmem (mem));

  my_vidmem_get_format (mem, &f, &w, &h);
  g_assert (f == 0);
  g_assert (w == 640);
  g_assert (h == 480);

  gst_memory_map (mem, &info, GST_MAP_READ);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);
  gst_object_unref (alloc);

  return 0;
}
