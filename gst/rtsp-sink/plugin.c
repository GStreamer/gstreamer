#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtspclientsink.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "rtspclientsink", GST_RANK_NONE,
          GST_TYPE_RTSP_CLIENT_SINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rtspclientsink,
    "RTSP client sink element",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
