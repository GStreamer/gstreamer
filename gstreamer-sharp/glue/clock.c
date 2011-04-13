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

void
gstsharp_g_cond_wait (GCond * cond, GMutex * mutex)
{
  g_cond_wait (cond, mutex);
}

gboolean
gstsharp_g_cond_timed_wait (GCond *cond, GMutex *mutex, GTimeVal *abs_time)
{
  return g_cond_timed_wait (cond, mutex, abs_time);
}

void
gstsharp_g_cond_broadcast (GCond *cond)
{
  g_cond_broadcast (cond);
}

void
gstsharp_g_mutex_lock (GMutex *mutex)
{
  g_mutex_lock (mutex);
}

void
gstsharp_g_mutex_unlock (GMutex *mutex)
{
  g_mutex_unlock (mutex);
}

gboolean
gstsharp_g_mutex_trylock (GMutex *mutex)
{
  return g_mutex_trylock (mutex);
}

