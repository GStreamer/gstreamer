#include <gst/gst.h>

static GstTypeFactory mpegfactory = {
  "video/mpeg",		// major type
  ".mpg .mpeg",		// extenstions
  NULL,			// typefind function
};

static GstCapsFactory mpeg2dec_sink_caps = {
  "video/mpeg",
  "mpegtype", GST_PROPS_LIST (
                     GST_PROPS_INT(1),
                     GST_PROPS_INT(2)
		),
  NULL
};

static GstCapsFactory mp1parse_src_caps = {
  "video/mpeg",
  "mpegtype", GST_PROPS_LIST (
                     GST_PROPS_INT(1)
		),
  NULL
};

static GstCapsFactory mpeg2dec_src_caps = {
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
  "video/raw",
  "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC_INT (0x32315659)
			),
  "height",	GST_PROPS_INT_RANGE (16, 256),
  NULL
};

static GstCapsFactory raw2_sink_caps = {
  "video/raw",
  "fourcc", 	GST_PROPS_LIST (
                        GST_PROPS_FOURCC_INT (0x32315659),
                        GST_PROPS_FOURCC ('Y','U','Y','V') 
			),
  "height",	GST_PROPS_INT_RANGE (16, 4096),
  NULL
};

static GstCapsListFactory mpg123_sinklist_caps = 
{
  &raw2_sink_caps,
  &raw2_sink_caps,
  NULL
};

static GstCaps *sinkcaps = NULL, 
               *rawcaps = NULL, 
               *rawcaps2 = NULL, 
               *rawcaps3 = NULL, 
               *sinkcapslist = NULL, 
	       *mp1parsecaps = NULL;

int main(int argc,char *argv[]) 
{
  gboolean testret;

  _gst_type_initialize ();

  sinkcaps = gst_caps_register (mpeg2dec_sink_caps);
  g_print ("caps 1:\n");
  gst_caps_dump (sinkcaps);
  rawcaps  = gst_caps_register (mpeg2dec_src_caps);
  g_print ("caps 2:\n");
  gst_caps_dump (rawcaps);
  rawcaps2  = gst_caps_register (raw_sink_caps);
  g_print ("caps 3:\n");
  gst_caps_dump (rawcaps2);
  mp1parsecaps  = gst_caps_register (mp1parse_src_caps);
  g_print ("caps 4:\n");
  gst_caps_dump (mp1parsecaps);
  rawcaps3  = gst_caps_register (raw2_sink_caps);
  g_print ("caps 5:\n");
  gst_caps_dump (rawcaps3);

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
