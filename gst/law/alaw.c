#include "alaw-encode.h"
#include "alaw-decode.h"

static GstElementDetails alawenc_details = {
  "PCM to A Law conversion",
  "Filter/Effect",
  "Convert 16bit PCM to 8bit A law",
  VERSION,
  "Zaheer Merali <zaheer@bellworldwide.net>",
  "(C) 2001"
};

static GstElementDetails alawdec_details = {
  "A Law to PCM conversion",
  "Filter/Effect",
  "Convert 8bit A law to 16bit PCM",
  VERSION,
  "Zaheer Merali <zaheer@bellworldwide.net>",
  "(C) 2001"
};

static GstCaps*
alaw_factory (void)
{
  return
   gst_caps_new (
  	"test_src",
    	"audio/raw",
	gst_props_new (
    	  "format",  GST_PROPS_STRING ("int"),
    	    "law",   GST_PROPS_INT (2),
    	    "width", GST_PROPS_INT(8),
    	    "depth", GST_PROPS_INT(8),
    	    "signed", GST_PROPS_BOOLEAN(FALSE),
	    NULL));
}

static GstCaps*
linear_factory (void)
{
  return
   gst_caps_new (
  	"test_sink",
    	"audio/raw",
	gst_props_new (
    	  "format",     GST_PROPS_STRING ("int"),
      	    "law",      GST_PROPS_INT(0),
      	    "width",    GST_PROPS_INT(16),
      	    "depth",    GST_PROPS_INT(16),
      	    "signed",   GST_PROPS_BOOLEAN(TRUE),
	    NULL));
}

GstPadTemplate *alawenc_src_template, *alawenc_sink_template; 
GstPadTemplate *alawdec_src_template, *alawdec_sink_template;

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *alawenc_factory, *alawdec_factory;
  GstCaps* alaw_caps, *linear_caps;

  alawenc_factory = gst_element_factory_new("alawencode",GST_TYPE_ALAWENC,
                                            &alawenc_details);
  g_return_val_if_fail(alawenc_factory != NULL, FALSE);
  alawdec_factory = gst_element_factory_new("alawdecode",GST_TYPE_ALAWDEC,
					    &alawdec_details);
  g_return_val_if_fail(alawdec_factory != NULL, FALSE);

  alaw_caps = alaw_factory ();
  linear_caps = linear_factory ();
 
  alawenc_src_template = gst_pad_template_new ("src",GST_PAD_SRC,GST_PAD_ALWAYS,alaw_caps, NULL);
  alawenc_sink_template = gst_pad_template_new ("sink",GST_PAD_SINK,GST_PAD_ALWAYS,linear_caps, NULL);
  gst_element_factory_add_pad_template (alawenc_factory, alawenc_src_template);
  gst_element_factory_add_pad_template (alawenc_factory, alawenc_sink_template);

  alawdec_src_template = gst_pad_template_new ("src",GST_PAD_SRC,GST_PAD_ALWAYS,linear_caps, NULL);
  alawdec_sink_template = gst_pad_template_new ("sink",GST_PAD_SINK,GST_PAD_ALWAYS,alaw_caps, NULL);
  
  gst_element_factory_add_pad_template (alawdec_factory, alawdec_src_template);
  gst_element_factory_add_pad_template (alawdec_factory, alawdec_sink_template);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (alawenc_factory));
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (alawdec_factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "alaw",
  plugin_init
};

