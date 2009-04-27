#include <gst/gst.h>

GstClockCallback
gstsharp_gst_clock_entry_get_func (GstClockEntry * entry)
{
  return entry->func;
}

void
gstsharp_gst_clock_entry_set_func (GstClockEntry * entry, GstClockCallback func)
{
  entry->func = func;
}
