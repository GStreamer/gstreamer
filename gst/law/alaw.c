#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "alaw-encode.h"
#include "alaw-decode.h"

static GstCaps*
alaw_factory (void)
{
  return
   gst_caps_new (
  	"test_src",
    	"audio/x-alaw",
	gst_props_new (
    	  "rate",     GST_PROPS_INT_RANGE (8000, 192000),
          "channels", GST_PROPS_INT_RANGE (1, 2),
	  NULL));
}

static GstCaps*
linear_factory (void)
{
  return
   gst_caps_new (
  	"test_sink",
    	"audio/x-raw-int",
	gst_props_new (
      	    "width",      GST_PROPS_INT(16),
      	    "depth",      GST_PROPS_INT(16),
      	    "endianness", GST_PROPS_INT(G_BYTE_ORDER),
      	    "signed",     GST_PROPS_BOOLEAN(TRUE),
            "rate",       GST_PROPS_INT_RANGE (8000, 192000),
            "channels",   GST_PROPS_INT_RANGE (1, 2),
	    NULL));
}

GstPadTemplate *alawenc_src_template, *alawenc_sink_template;
GstPadTemplate *alawdec_src_template, *alawdec_sink_template;

static gboolean
plugin_init (GstPlugin *plugin)
{
  GstCaps* alaw_caps, *linear_caps;

  alaw_caps = alaw_factory ();
  linear_caps = linear_factory ();

  alawenc_src_template = gst_pad_template_new ("src",GST_PAD_SRC,GST_PAD_ALWAYS,alaw_caps, NULL);
  alawenc_sink_template = gst_pad_template_new ("sink",GST_PAD_SINK,GST_PAD_ALWAYS,linear_caps, NULL);

  alawdec_src_template = gst_pad_template_new ("src",GST_PAD_SRC,GST_PAD_ALWAYS,linear_caps, NULL);
  alawdec_sink_template = gst_pad_template_new ("sink",GST_PAD_SINK,GST_PAD_ALWAYS,alaw_caps, NULL);

  if (!gst_element_register (plugin, "alawenc",
			     GST_RANK_NONE, GST_TYPE_ALAWENC) ||
      !gst_element_register (plugin, "alawdec",
			     GST_RANK_PRIMARY, GST_TYPE_ALAWENC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "alaw",
  "ALaw audio conversion routines",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
