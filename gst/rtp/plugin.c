#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpsink.h"
#include "gstrtpsrc.h"


static gboolean
plugin_init (GstPlugin * plugin)
{

  gboolean ret = FALSE;

  ret |= gst_element_register (plugin, "rtpsrc",
      GST_RANK_PRIMARY + 1, GST_TYPE_RTP_SRC);

  ret |= gst_element_register (plugin, "rtpsink",
      GST_RANK_PRIMARY + 1, GST_TYPE_RTP_SINK);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rtpmanagerbad,
    "GStreamer RTP Plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
