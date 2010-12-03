#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>

/* Helper functions */
#include <gst/gst.h>

/* Object header */
#include "gstaudioflingersink.h"
	
static gboolean plugin_init (GstPlugin * plugin)
{
  gboolean ret = TRUE;
 
  ret &= gst_audioflinger_sink_plugin_init (plugin);

  return ret;
}

/* Version number of package */
#define VERSION "0.0.1"
/* package name */
#define PACKAGE "Android ST-ERICSSON"


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "audioflinger",
    "Android audioflinger library for gstreamer",
    plugin_init, VERSION, "LGPL", "libgstaudioflinger.so", "http://www.stericsson.com")

