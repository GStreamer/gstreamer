#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "alaw-encode.h"
#include "alaw-decode.h"

static GstCaps *
alaw_factory (void)
{
  return gst_caps_new_simple ("audio/x-alaw",
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
      "signed", G_TYPE_BOOLEAN, TRUE,
      "rate", GST_TYPE_INT_RANGE, 8000, 192000,
      "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
}

GstPadTemplate *alawenc_src_template, *alawenc_sink_template;
GstPadTemplate *alawdec_src_template, *alawdec_sink_template;

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstCaps *alaw_caps, *linear_caps;

  alaw_caps = alaw_factory ();
  linear_caps = linear_factory ();

  alawenc_src_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, alaw_caps);
  alawenc_sink_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, linear_caps);

  alawdec_src_template =
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, linear_caps);
  alawdec_sink_template =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, alaw_caps);

  if (!gst_element_register (plugin, "alawenc",
          GST_RANK_NONE, GST_TYPE_ALAWENC) ||
      !gst_element_register (plugin, "alawdec",
          GST_RANK_PRIMARY, GST_TYPE_ALAWDEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alaw",
    "ALaw audio conversion routines",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
