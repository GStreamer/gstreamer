#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpvp8pay.h"
#include "gstrtpvp8depay.h"

static gboolean
plugin_init (GstPlugin *plugin)
{
  gst_rtp_vp8_depay_plugin_init (plugin);
  gst_rtp_vp8_pay_plugin_init (plugin);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rtpvp8",
    "rtpvp8",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
