#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/times.h>

#include <gst/gst.h>

#include "my-memory.h"

int
main (int argc, char **argv)
{
  GstAllocator *alloc;
  GstMemory *mem;
  GstAllocationParams params;
  GstMapInfo info;

  gst_init (&argc, &argv);

  my_memory_init ();

  alloc = gst_allocator_find ("MyMemory");

  gst_allocation_params_init (&params);
  mem = gst_allocator_alloc (alloc, 1024, &params);

  gst_memory_map (mem, &info, GST_MAP_READ);
  gst_memory_unmap (mem, &info);

  gst_memory_unref (mem);

  return 0;
}
