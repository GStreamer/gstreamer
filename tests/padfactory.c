#include <gst/gst.h>

static GstCapsFactory mpeg2dec_sink_caps = {
  "mpeg2deccaps",
  "video/mpeg",
  "mpegtype", GST_PROPS_LIST (
                     GST_PROPS_INT(1),
                     GST_PROPS_INT(2)
		),
  NULL
};

static GstCapsFactory mpeg2dec_src_caps = {
  "name",
  "video/raw",
  "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC ('Y','V','1','2'), 
 			GST_PROPS_FOURCC_INT (0x56595559)
			),
  "width",	GST_PROPS_INT_RANGE (16, 4096),
  "height",	GST_PROPS_INT_RANGE (16, 4096),
  NULL
};

static GstCapsFactory raw_sink_caps = {
  NULL
};

static GstPadFactory pad_caps = {
  "src",
  GST_PAD_FACTORY_SRC,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS (
    "videocaps",
    "video/raw",
    "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC_INT (0x32315659),
                        GST_PROPS_FOURCC ('Y','U','Y','V') 
			),
    "height",	GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_PAD_FACTORY_CAPS (
    "videocaps2",
    "video/raw",
    "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC_INT (0x32315659)
			),
    "height",	GST_PROPS_INT_RANGE (16, 256)
  ),
  NULL
};


static GstCaps *sinkcaps = NULL, 
               *rawcaps = NULL;

static GstPadTemplate *temp;

int main(int argc,char *argv[]) 
{
  gboolean testret;
  xmlDocPtr doc;
  xmlNodePtr parent;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  _gst_type_initialize ();

  sinkcaps = gst_caps_register (&mpeg2dec_sink_caps);
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (sinkcaps, parent);

  rawcaps  = gst_caps_register (&mpeg2dec_src_caps);
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities2", NULL);
  gst_caps_save_thyself (rawcaps, parent);

  temp = gst_padtemplate_new (&pad_caps);
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Padtemplate", NULL);
  gst_padtemplate_save_thyself (temp, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
