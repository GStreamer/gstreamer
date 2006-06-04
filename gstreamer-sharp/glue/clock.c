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
