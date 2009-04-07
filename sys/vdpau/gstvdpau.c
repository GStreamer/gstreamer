#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif


#include <gst/gst.h>

#include "gstvdpaumpegdecoder.h"
#include "gstvdpauvideoyuv.h"
#include "gstvdpauyuvvideo.h"

static gboolean
vdpau_init (GstPlugin * vdpaumpegdecoder)
{
  gst_element_register (vdpaumpegdecoder, "vdpaumpegdecoder",
      GST_RANK_NONE, GST_TYPE_VDPAU_MPEG_DECODER);
  gst_element_register (vdpaumpegdecoder, "vdpauvideoyuv",
      GST_RANK_NONE, GST_TYPE_VDPAU_VIDEO_YUV);
  gst_element_register (vdpaumpegdecoder, "vdpauyuvvideo",
      GST_RANK_NONE, GST_TYPE_VDPAU_YUV_VIDEO);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vdpau",
    "Various elements utilizing VDPAU",
    vdpau_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
