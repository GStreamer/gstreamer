#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "mulaw-encode.h"
#include "mulaw-decode.h"

static GstCaps*
mulaw_factory (void)
{
  return
    gst_caps_new (
  	"test_src",
    	"audio/x-mulaw",
	gst_props_new (
    	    "width",    GST_PROPS_INT(8),
    	    "depth",    GST_PROPS_INT(8),
    	    "signed",   GST_PROPS_BOOLEAN(FALSE),
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
      	    "signed",     GST_PROPS_BOOLEAN(TRUE),
      	    "endianness", GST_PROPS_INT(G_BYTE_ORDER),
    	    "rate",       GST_PROPS_INT_RANGE (8000, 192000),
            "channels",   GST_PROPS_INT_RANGE (1, 2),
	    NULL));
}

GstPadTemplate *mulawenc_src_template, *mulawenc_sink_template;
GstPadTemplate *mulawdec_src_template, *mulawdec_sink_template;

static gboolean
plugin_init (GstPlugin *plugin)
{
  GstCaps* mulaw_caps, *linear_caps;

  mulaw_caps = mulaw_factory ();
  linear_caps = linear_factory ();

  mulawenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
		   		               mulaw_caps, NULL);
  mulawenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
		   			        linear_caps, NULL);

  mulawdec_src_template = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
		  				linear_caps, NULL);
  mulawdec_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
		   				mulaw_caps, NULL);

  if (!gst_element_register (plugin, "mulawenc",
			     GST_RANK_NONE, GST_TYPE_MULAWENC) ||
      !gst_element_register (plugin, "mulawdec",
			     GST_RANK_PRIMARY, GST_TYPE_MULAWENC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mulaw",
  "MuLaw audio conversion routines",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
