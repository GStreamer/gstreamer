#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "mulaw-encode.h"
#include "mulaw-decode.h"

static GstCaps *
mulaw_factory (void)
{
  return gst_caps_new_simple ("audio/x-mulaw",
      "rate", GST_TYPE_INT_RANGE, 8000, 192000,
      "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
}

static GstCaps *
linear_factory (void)
{
  return gst_caps_new_simple ("audio/x-raw-int",
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", GST_TYPE_INT_RANGE, 8000, 192000,
      "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
}

GstPadTemplate *mulawenc_src_template, *mulawenc_sink_template;
GstPadTemplate *mulawdec_src_template, *mulawdec_sink_template;

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstCaps *mulaw_caps, *linear_caps;

  mulaw_caps = mulaw_factory ();
  linear_caps = linear_factory ();

  mulawenc_src_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, mulaw_caps);
  mulawenc_sink_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, linear_caps);

  mulawdec_src_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, linear_caps);
  mulawdec_sink_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, mulaw_caps);

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
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
