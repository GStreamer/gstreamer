#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "mulaw-encode.h"
#include "mulaw-decode.h"

GstStaticPadTemplate mulaw_dec_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 8000, 192000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "width = (int) 16, " "signed = (boolean) True")
    );

GstStaticPadTemplate mulaw_dec_sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, "
        "rate = [ 8000 , 192000 ], " "channels = [ 1 , 2 ]")
    );

GstStaticPadTemplate mulaw_enc_sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 8000, 192000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "width = (int) 16, " "signed = (boolean) True")
    );

GstStaticPadTemplate mulaw_enc_src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, "
        "rate = [ 8000 , 192000 ], " "channels = [ 1 , 2 ]")
    );

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "mulawenc",
          GST_RANK_NONE, GST_TYPE_MULAWENC) ||
      !gst_element_register (plugin, "mulawdec",
          GST_RANK_PRIMARY, GST_TYPE_MULAWDEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mulaw",
    "MuLaw audio conversion routines",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
