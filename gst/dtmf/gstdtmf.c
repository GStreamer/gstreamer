
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdtmfdetect.h"
#include "gstdtmfsrc.h"
#include "gstrtpdtmfsrc.h"
#include "gstrtpdtmfdepay.h"


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_dtmf_detect_plugin_init (plugin))
    return FALSE;

  if (!gst_dtmf_src_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_dtmf_src_plugin_init (plugin))
    return FALSE;

  if (!gst_rtp_dtmf_depay_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dtmf", "DTMF plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
