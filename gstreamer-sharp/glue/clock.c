#include <gst/gst.h>

uint
gstsharp_gst_clock_get_entries_changed_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstClock, entries_changed);
}

uint
gstsharp_gst_clock_get_slave_lock_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstClock, slave_lock);
}

uint
gstsharp_gst_clock_get_entries_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstClock, entries);
}

uint
gstsharp_gst_clock_get_times_offset (void) {
  return (uint) G_STRUCT_OFFSET (GstClock, times);
}

