#include <glib.h>
#include <gst/gstclock.h>

guint64 gstsharp_gst_clock_get_gst_second()
{
    return GST_SECOND;
}

guint64 gstsharp_gst_clock_get_gst_msecond()
{
    return GST_MSECOND;
}

guint64 gstsharp_gst_clock_get_gst_usecond()
{
    return GST_USECOND;
}

guint64 gstsharp_gst_clock_get_gst_nsecond()
{
    return GST_NSECOND;
}

guint64 gstsharp_gst_clock_get_time_none()
{
	return GST_CLOCK_TIME_NONE;
}

gboolean gstsharp_gst_clock_time_is_valid(GstClockTime time)
{
	return GST_CLOCK_TIME_IS_VALID(time);
}

