#include <gst/gst.h>

static GstTypeFactory mpegfactory = {
  "video/mpeg",		// major type
  ".mpg .mpeg",		// extenstions
  NULL,			// typefind function
};

static GstCapsFactory mpeg2dec_sink_caps = {
  "video/mpeg",
  "mpegtype", GST_CAPS_LIST (
                     GST_CAPS_INT(1),
                     GST_CAPS_INT(2)
		),
  NULL
};

static GstCapsFactory mp1parse_src_caps = {
  "video/mpeg",
  "mpegtype", GST_CAPS_LIST (
                     GST_CAPS_INT(1)
		),
  NULL
};

static GstCapsFactory mpeg2dec_src_caps = {
  "video/raw",
  "fourcc", 	GST_CAPS_LIST (
                        GST_CAPS_INT32 (0x32315659), 
 			GST_CAPS_INT32 (0x32314544)
			),
  "width",	GST_CAPS_INT_RANGE (16, 4096),
  "height",	GST_CAPS_INT_RANGE (16, 4096),
  NULL
};

static GstCapsFactory raw_sink_caps = {
  "video/raw",
  "fourcc", 	GST_CAPS_LIST (
                        GST_CAPS_INT32 (0x32315659)
			),
  "height",	GST_CAPS_INT_RANGE (16, 256),
  NULL
};

static GstCaps *sinkcaps = NULL, 
               *rawcaps = NULL, 
               *rawcaps2 = NULL, 
	       *mp1parsecaps = NULL;

int main(int argc,char *argv[]) 
{
  gboolean testret;

  _gst_type_initialize ();

  sinkcaps = gst_caps_register (mpeg2dec_sink_caps);
  gst_caps_dump (sinkcaps);
  rawcaps  = gst_caps_register (mpeg2dec_src_caps);
  gst_caps_dump (rawcaps);
  rawcaps2  = gst_caps_register (raw_sink_caps);
  gst_caps_dump (rawcaps2);
  mp1parsecaps  = gst_caps_register (mp1parse_src_caps);
  gst_caps_dump (mp1parsecaps);

  testret = gst_caps_check_compatibility (mp1parsecaps, rawcaps);
  g_print ("%d\n", testret);
  
  testret = gst_caps_check_compatibility (mp1parsecaps, sinkcaps);
  g_print ("%d\n", testret);
  
  testret = gst_caps_check_compatibility (sinkcaps, mp1parsecaps);
  g_print ("%d\n", testret);

  testret = gst_caps_check_compatibility (rawcaps, rawcaps2);
  g_print ("%d\n", testret);

  testret = gst_caps_check_compatibility (rawcaps2, rawcaps);
  g_print ("%d\n", testret);

  testret = gst_caps_check_compatibility (rawcaps, rawcaps);
  g_print ("%d\n", testret);
}
