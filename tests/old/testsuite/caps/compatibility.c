#include <gst/gst.h>

/* these caps all have a non empty intersection */
GST_CAPS_FACTORY (sinkcaps,
  GST_CAPS_NEW (
    "mpeg2dec_sink",
    "video/mpeg",
      "mpegtype", GST_PROPS_LIST (
      		    GST_PROPS_INT (1),
      		    GST_PROPS_INT (2)
		  )
  )
);

GST_CAPS_FACTORY (mp1parsecaps,
  GST_CAPS_NEW (
    "mp1parse_src",
    "video/mpeg",
      "mpegtype", GST_PROPS_LIST (
      		    GST_PROPS_INT (1)
		  )
  )
);



GST_CAPS_FACTORY (rawcaps,
  GST_CAPS_NEW (
    "mpeg2dec_src",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2"))
		  ),
      "width",	GST_PROPS_INT_RANGE (16, 4096),
      "height",	GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_CAPS_FACTORY (rawcaps2,
  GST_CAPS_NEW (
    "raw_sink_caps",
    "video/raw",
      "fourcc", GST_PROPS_LIST (
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12"))
	        ),
      "height",	GST_PROPS_INT_RANGE (16, 256)
  )
);

GST_CAPS_FACTORY (rawcaps3,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc", GST_PROPS_LIST (
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")) 
	        ),
      "height", GST_PROPS_INT_RANGE (16, 4096)
  )
);

/* these caps aren't used yet
GST_CAPS_FACTORY (rawcaps4,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUYV")) 
		  ),
      "height",	  GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_CAPS_FACTORY (rawcaps5,
  GST_CAPS_NEW (
    "raw2_sink_caps",
    "video/raw",
      "fourcc",   GST_PROPS_LIST (
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUYV")),
                    GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2"))
		  ),
      "height",   GST_PROPS_INT_RANGE (16, 4096)
  )
);
*/

int 
main (int argc, char *argv[]) 
{
  gboolean testret;

  gst_init (&argc, &argv);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (mp1parsecaps), GST_CAPS_GET (rawcaps));
  g_print ("4 <-> 2 == %d (invalid, wrong major type)\n", testret);
  
  testret = gst_caps_check_compatibility (GST_CAPS_GET (mp1parsecaps), GST_CAPS_GET (sinkcaps));
  g_print ("4 <-> 1 == %d (valid, subset)\n", testret);
  
  testret = gst_caps_check_compatibility (GST_CAPS_GET (sinkcaps), GST_CAPS_GET (mp1parsecaps));
  g_print ("1 <-> 4 == %d (invalid, superset)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps), GST_CAPS_GET (rawcaps2));
  g_print ("2 <-> 3 == %d (invalid, ranges)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps), GST_CAPS_GET (rawcaps3));
  g_print ("2 <-> 5 == %d (valid)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps3), GST_CAPS_GET (rawcaps));
  g_print ("5 <-> 2 == %d (invalid)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps2), GST_CAPS_GET (rawcaps3));
  g_print ("3 <-> 5 == %d (valid)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps2), GST_CAPS_GET (rawcaps));
  g_print ("3 <-> 2 == %d (invalid, property missing in source)\n", testret);

  testret = gst_caps_check_compatibility (GST_CAPS_GET (rawcaps), GST_CAPS_GET (rawcaps));
  g_print ("2 <-> 2 == %d (valid, same caps)\n", testret);

  return 0;
}
