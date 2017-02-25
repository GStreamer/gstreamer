#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstrawaudioparse.h"
#include "gstrawvideoparse.h"
#include "gstunalignedaudioparse.h"
#include "gstunalignedvideoparse.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;

  ret &= gst_element_register (plugin, "unalignedaudioparse", GST_RANK_MARGINAL,
      gst_unaligned_audio_parse_get_type ());
  ret &= gst_element_register (plugin, "unalignedvideoparse", GST_RANK_MARGINAL,
      gst_unaligned_video_parse_get_type ());
  ret &= gst_element_register (plugin, "rawaudioparse", GST_RANK_NONE,
      gst_raw_audio_parse_get_type ());
  ret &= gst_element_register (plugin, "rawvideoparse", GST_RANK_NONE,
      gst_raw_video_parse_get_type ());

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rawparse,
    "Parses byte streams into raw frames",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
