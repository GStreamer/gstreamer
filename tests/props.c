#include <gst/gst.h>

static GstProps* mpeg2dec_sink_props_register (void) {
  return gst_props_new (
    "mpegtype", GST_PROPS_LIST (
                     GST_PROPS_INT(1),
                     GST_PROPS_INT(2)
		),
    NULL);
}

static GstProps* mpeg2dec_src_props_register (void) {
  return gst_props_new (
    "fourcc",	GST_PROPS_LIST (
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2'))
			),
    "width",	GST_PROPS_INT_RANGE (16, 4096),
    "height",	GST_PROPS_INT_RANGE (16, 4096),
    NULL);
}

static GstProps *sinkprops = NULL,
                *rawprops = NULL,
                *testprops = NULL;

int main(int argc,char *argv[])
{
  xmlDocPtr doc;
  xmlNodePtr parent;
  gint i;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Properties", NULL);

  g_thread_init (NULL);
  _gst_props_initialize ();

  sinkprops = mpeg2dec_sink_props_register ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Props1", NULL);
  gst_props_save_thyself (sinkprops, parent);

  rawprops  = mpeg2dec_src_props_register ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Props2", NULL);
  gst_props_save_thyself (rawprops, parent);

  i=argc;

  testprops  = gst_props_new ("layer", GST_PROPS_INT (i),
		              "bitrate", GST_PROPS_INT_RANGE (i*300, i*10000),
			      NULL);
  if (i==3) {
    testprops  = gst_props_merge (testprops,
		      gst_props_new ("framed", GST_PROPS_BOOLEAN (TRUE),
		                     "mpegtest", GST_PROPS_BOOLEAN (FALSE),
		                     "hello", GST_PROPS_LIST (
                        		     GST_PROPS_FOURCC (GST_MAKE_FOURCC (0,0,0x55,0x55)),
                        		     GST_PROPS_FOURCC (GST_MAKE_FOURCC (0,0,0x66,0x66))
					     ),
			             NULL));
  }

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Props3", NULL);
  gst_props_save_thyself (testprops, parent);

  sinkprops = gst_props_set (sinkprops, "mpegtype", GST_PROPS_INT (1));
  sinkprops = gst_props_set (sinkprops, "foobar", GST_PROPS_FOURCC (GST_MAKE_FOURCC (0x56, 0x56,0x56,0x56)));

  g_print ("%08lx\n", gst_props_get_fourcc_int (sinkprops, "foobar"));
  g_print ("%d\n", gst_props_get_int (sinkprops, "mpegtype"));

  parent = xmlNewChild (doc->xmlRootNode, NULL, "Props4", NULL);
  gst_props_save_thyself (sinkprops, parent);
  
  xmlDocDump(stdout, doc);

  return 0;
}
