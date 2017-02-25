#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstaudioparse.h"
#include "gstvideoparse.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  ret = gst_element_register (plugin, "videoparse", GST_RANK_NONE,
      gst_video_parse_get_type ());
  ret &= gst_element_register (plugin, "audioparse", GST_RANK_NONE,
      gst_audio_parse_get_type ());

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    legacyrawparse,
    "Parses byte streams into raw frames",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
