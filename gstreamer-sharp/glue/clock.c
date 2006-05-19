
#include <glib.h>
#include <gst/gstclock.h>


gint64 gstsharp_gst_clock_get_gst_second ()
{
	return GST_SECOND;
}

gint64 gstsharp_gst_clock_get_gst_msecond ()
{
	return GST_MSECOND;
}

gint64 gstsharp_gst_clock_get_gst_usecond ()
{
	return GST_USECOND;
}

gint64 gstsharp_gst_clock_get_gst_nsecond ()
{
	return GST_NSECOND;
}
