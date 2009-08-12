#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <gst/gst.h>

#include "gstvdpmpegdec.h"
#include "gstvdpvideoyuv.h"
#include "gstvdpyuvvideo.h"

static gboolean
vdpau_init (GstPlugin * vdpau_plugin)
{
  /* Before giving these elements a rank again, make sure they pass at
   * least the generic/states test when there's no device available */
  gst_element_register (vdpau_plugin, "vdpaumpegdec",
      GST_RANK_NONE, GST_TYPE_VDP_MPEG_DEC);
  gst_element_register (vdpau_plugin, "vdpauvideoyuv",
      GST_RANK_NONE, GST_TYPE_VDP_VIDEO_YUV);
  gst_element_register (vdpau_plugin, "vdpauyuvvideo",
      GST_RANK_NONE, GST_TYPE_VDP_YUV_VIDEO);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vdpau",
    "Various elements utilizing VDPAU",
    vdpau_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
