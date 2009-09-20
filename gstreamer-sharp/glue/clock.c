#include <gst/gst.h>

guint
gstsharp_gst_clock_get_entries_changed_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstClock, entries_changed);
}

guint
gstsharp_gst_clock_get_slave_lock_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstClock, slave_lock);
}

guint
gstsharp_gst_clock_get_entries_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstClock, entries);
}

guint
gstsharp_gst_clock_get_times_offset (void)
{
  return (guint) G_STRUCT_OFFSET (GstClock, times);
}
