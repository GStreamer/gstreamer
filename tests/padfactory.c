#include <gst/gst.h>

static GstCaps*
mpeg2dec_sink_caps (void)
{
  static GstCaps *caps;

  if (!caps) {
    caps = gst_caps_new (
      "mpeg2deccaps",
      "video/mpeg",
      gst_props_new (
        "mpegtype", GST_PROPS_LIST (
                      GST_PROPS_INT(1),
                      GST_PROPS_INT(2)
		    ),
	NULL));
  }
  return caps;
}

GST_CAPS_FACTORY (mpeg2dec_src_caps,
  GST_CAPS_NEW (
    "mpeg2dec_src_caps",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC ( GST_MAKE_FOURCC ('Y','V','1','2')), 
		    GST_PROPS_FOURCC (0x56595559)
		  ),
      "width",	  GST_PROPS_INT_RANGE (16, 4096),
      "height",	  GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW(
    "mpeg2dec_src_caps",
    "video/raw",
      "foo",   GST_PROPS_BOOLEAN (TRUE)
  )
)

static GstPadTemplate*
pad_caps (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_padtemplate_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "videocaps",
        "video/raw",
	gst_props_new (
          "fourcc", 	GST_PROPS_LIST (
                          GST_PROPS_FOURCC (0x32315659),
                          GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','V')) 
			),
          "height",	GST_PROPS_INT_RANGE (16, 4096),
	  NULL)),
      gst_caps_new (
        "videocaps2",
        "video/raw",
	gst_props_new (
          "fourcc", 	GST_PROPS_LIST (
                          GST_PROPS_FOURCC (0x32315659)
			),
          "height",	GST_PROPS_INT_RANGE (16, 256),
	  NULL)),
      NULL);
  }
  return template;
}

GST_PADTEMPLATE_FACTORY (testtempl,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mycaps",
    "audio/raw",
      "format", GST_PROPS_INT (55),
      "foo",	GST_PROPS_STRING ("bar")
  ),
  GST_CAPS_NEW (
    "mycaps2",
    "audio/float",
      "format", GST_PROPS_INT (7),
      "baz",	GST_PROPS_STRING ("toe")
  )
)

static GstCaps *sinkcaps = NULL, 
               *rawcaps = NULL;

static GstPadTemplate *temp;

int main(int argc,char *argv[]) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  gst_init (&argc, &argv);

  sinkcaps = mpeg2dec_sink_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (sinkcaps, parent);

  rawcaps  = GST_CAPS_GET (mpeg2dec_src_caps);
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities2", NULL);
  gst_caps_save_thyself (rawcaps, parent);

  temp = pad_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Padtemplate", NULL);
  gst_padtemplate_save_thyself (temp, parent);

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Padtemplate2", NULL);
  gst_padtemplate_save_thyself (GST_PADTEMPLATE_GET (testtempl), parent);

  xmlDocDump(stdout, doc);

  return 0;
}
