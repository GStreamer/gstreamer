#include <gst/gst.h>

static GstPropsFactory mpeg2dec_sink_props = {
  "mpegtype", GST_PROPS_LIST (
                     GST_PROPS_INT(1),
                     GST_PROPS_INT(2)
		),
  NULL
};

static GstPropsFactory mpeg2dec_src_props = {
  "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC ('Y','V','1','2'), 
 			GST_PROPS_FOURCC_INT (0x56595559)
			),
  "width",	GST_PROPS_INT_RANGE (16, 4096),
  "height",	GST_PROPS_INT_RANGE (16, 4096),
  NULL
};

static GstProps *sinkprops = NULL, 
                *rawprops = NULL,
                *testprops = NULL;

int main(int argc,char *argv[]) 
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  gint i;

  doc = xmlNewDoc ("1.0");
  doc->root = xmlNewDocNode (doc, NULL, "Properties", NULL);

  _gst_type_initialize ();

  sinkprops = gst_props_register (mpeg2dec_sink_props);
  parent = xmlNewChild (doc->root, NULL, "Props1", NULL);
  gst_props_save_thyself (sinkprops, parent);

  rawprops  = gst_props_register (mpeg2dec_src_props);
  parent = xmlNewChild (doc->root, NULL, "Props2", NULL);
  gst_props_save_thyself (rawprops, parent);

  i=argc;

  testprops  = gst_props_new ("layer", GST_PROPS_INT (i), 
		              "bitrate", GST_PROPS_INT_RANGE (i*300, i*10000),
			      NULL);
  if (i==3) {
    testprops  = gst_props_merge (testprops,
		      gst_props_new ("framed", GST_PROPS_BOOLEAN (TRUE), 
			             NULL));
  }

  parent = xmlNewChild (doc->root, NULL, "Props3", NULL);
  gst_props_save_thyself (testprops, parent);

  xmlDocDump(stdout, doc);

  return 0;
}
