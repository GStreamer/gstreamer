#include <gst/gst.h>

static GstCaps* 
mpeg2dec_sink_caps (void) 
{
  return 
    gst_caps_new (
  	"mpeg2dec_sink",
  	"video/mpeg",
	gst_props_new (
  	  "mpegtype", GST_PROPS_LIST (
           	          GST_PROPS_INT(1),
                	  GST_PROPS_INT(2)
		      ),
	  NULL
	)
    );
}

static GstCaps* 
mp1parse_src_caps (void) 
{
  return 
    gst_caps_new (
  	"mp1parse_src",
  	"video/mpeg",
	gst_props_new (
  	  "mpegtype", GST_PROPS_LIST (
          	           GST_PROPS_INT(1)
		      ),
	  NULL
	)
    );
}

static GstCaps* 
mpeg2dec_src_caps (void) 
{
  return 
    gst_caps_new (
  	"mpeg2dec_src",
  	"video/raw",
	gst_props_new (
  	  "fourcc", 	GST_PROPS_LIST (
                        	GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')), 
                	        GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2'))
			),
  	  "width",	GST_PROPS_INT_RANGE (16, 4096),
  	  "height",	GST_PROPS_INT_RANGE (16, 4096),
	  NULL
	)
    );
}

static GstCaps* 
raw_sink_caps (void) 
{
  return 
    gst_caps_new (
  	"raw_sink_caps",
  	"video/raw",
	gst_props_new (
  	  "fourcc", 	GST_PROPS_LIST (
        	                GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')) 
				),
  	  "height",	GST_PROPS_INT_RANGE (16, 256),
	  NULL
	)
    );
}

static GstCaps* 
raw2_sink_caps (void)
{
  return 
    gst_caps_new (
  	"raw2_sink_caps",
  	"video/raw",
	gst_props_new (
  	  "fourcc", GST_PROPS_LIST (
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')) 
		    ),
  	  "height", GST_PROPS_INT_RANGE (16, 4096),
	  NULL
	)
    );
}


static GstCaps*
get_testcaps (void) 
{
  return 
    gst_caps_new (
  	"raw2_sink_caps",
  	"video/raw",
  	gst_props_new (
  	  "fourcc", 	GST_PROPS_LIST (
                          GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
                          GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','V')) 
			),
  	  "height",	GST_PROPS_INT_RANGE (16, 4096),
  	  NULL
	)
    );
}

static GstCaps *sinkcaps = NULL, 
               *rawcaps = NULL, 
               *rawcaps2 = NULL, 
               *rawcaps3 = NULL, 
	       *mp1parsecaps = NULL;

int 
main (int argc, char *argv[]) 
{
  gboolean testret;
  xmlDocPtr doc;
  xmlNodePtr parent;

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "Capabilities", NULL);

  g_thread_init (NULL);
  _gst_type_initialize ();
  _gst_props_initialize ();
  _gst_caps_initialize ();

  sinkcaps = mpeg2dec_sink_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities1", NULL);
  gst_caps_save_thyself (sinkcaps, parent);

  rawcaps  = mpeg2dec_src_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities2", NULL);
  gst_caps_save_thyself (rawcaps, parent);

  rawcaps2  = raw_sink_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities3", NULL);
  gst_caps_save_thyself (rawcaps2, parent);

  mp1parsecaps  = mp1parse_src_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities4", NULL);
  gst_caps_save_thyself (mp1parsecaps, parent);

  rawcaps3  = raw2_sink_caps ();
  parent = xmlNewChild (doc->xmlRootNode, NULL, "Capabilities5", NULL);
  gst_caps_save_thyself (rawcaps3, parent);

  xmlDocDump(stdout, doc);

  testret = gst_caps_check_compatibility (mp1parsecaps, rawcaps);
  g_print ("4 <-> 2 == %d (invalid, wrong major type)\n", testret);
  
  testret = gst_caps_check_compatibility (mp1parsecaps, sinkcaps);
  g_print ("4 <-> 1 == %d (valid, subset)\n", testret);
  
  testret = gst_caps_check_compatibility (sinkcaps, mp1parsecaps);
  g_print ("1 <-> 4 == %d (invalid, superset)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps, rawcaps2);
  g_print ("2 <-> 3 == %d (invalid, ranges)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps, rawcaps3);
  g_print ("2 <-> 5 == %d (valid)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps3, rawcaps);
  g_print ("5 <-> 2 == %d (invalid)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps2, rawcaps3);
  g_print ("3 <-> 5 == %d (valid)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps2, rawcaps);
  g_print ("3 <-> 2 == %d (invalid, property missing in source)\n", testret);

  testret = gst_caps_check_compatibility (rawcaps, rawcaps);
  g_print ("2 <-> 2 == %d (valid, same caps)\n", testret);

  return 0;
}
