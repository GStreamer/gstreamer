#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mpegtsmux.h"
#include "atscmux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_mpegts_initialize ();

  if (!gst_element_register (plugin, "mpegtsmux", GST_RANK_PRIMARY,
          mpegtsmux_get_type ()))
    return FALSE;

  if (!gst_element_register (plugin, "atscmux", GST_RANK_PRIMARY,
          atscmux_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    mpegtsmux, "MPEG-TS muxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
