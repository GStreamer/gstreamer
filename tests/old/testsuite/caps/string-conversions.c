#include <gst/gst.h>
#include <string.h>

GST_CAPS_FACTORY (caps1,
  GST_CAPS_NEW (
    "mpeg2dec_sink",
    "video/mpeg",
      "mpegtype", GST_PROPS_LIST (
      		    GST_PROPS_INT (1),
      		    GST_PROPS_INT (2)
		  )
  )
);
GST_CAPS_FACTORY (caps2,
  GST_CAPS_NEW (
    "mp1parse_src",
    "video/mpeg",
      "mpegtype", GST_PROPS_LIST (
      		    GST_PROPS_INT (1)
		  )
  )
);
GST_CAPS_FACTORY (caps3,
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
GST_CAPS_FACTORY (caps4,
  GST_CAPS_NEW (
    "raw_sink_caps",
    "video/raw",
      "fourcc", GST_PROPS_LIST (
                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12"))
	        ),
      "height",	GST_PROPS_INT_RANGE (16, 256)
  )
);
GST_CAPS_FACTORY (caps5,
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
GST_CAPS_FACTORY (caps6,
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
GST_CAPS_FACTORY (caps7,
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
GST_CAPS_FACTORY(caps8,
      GST_CAPS_NEW (
        "videotestsrc_src",
        "video/raw",
          "format",		GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0'))
      ),
      GST_CAPS_NEW (
        "videotestsrc_src",
        "video/raw",
          "format",		GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','Y','V'))
      )
)
GST_CAPS_FACTORY(caps9,
      GST_CAPS_NEW (
        "xvideosink_sink",
        "video/raw",
          "format",		GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0'))
      ),
      GST_CAPS_NEW (
        "xvideosink_sink",
        "video/raw",
          "format",		GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','V','1','2'))
      )
)
GST_CAPS_FACTORY(caps10,
      GST_CAPS_NEW (
	"my_caps",
	"video/x-jpeg", 
	NULL
      )
)

static gint test = 0;
static gint failures = 0;

#define TEST_START g_print ("%3d, START\n", ++test)
#define TEST_FAIL g_print ("%3d, FAIL     : failure %d\n", test, ++failures)
#define TEST_SUCCESS g_print ("%3d, SUCCESS\n", test)
#define TEST_END(result) G_STMT_START{ \
  if (result) { \
    TEST_SUCCESS; \
  } else { \
    TEST_FAIL; \
  } \
}G_STMT_END
static void
test_caps_func (GstCaps *caps)
{
  gchar *str1, *str2;  
  gboolean ret = FALSE;

  str1 = gst_caps_to_string (caps);
  gst_caps_unref (caps);
  caps = gst_caps_from_string (str1);
  if (!caps) {
    g_print ("%3d, INFO     : no caps from  %s\n", test, str1);
    TEST_END (ret);
    return;
  }
  str2 = gst_caps_to_string (caps);
  gst_caps_unref (caps);
  g_print ("%3d, INFO     : %s <==> %s\n", test, str1, str2);
  ret = strcmp (str1, str2) == 0;
  g_free (str1);
  g_free (str2);
  TEST_END (ret);
}
static void
test_caps (GstCaps *caps)
{
  TEST_START;
  test_caps_func (caps);
}
static void
test_string (gchar *str)
{
  GstCaps *caps;

  TEST_START;
  g_print ("%3d, INFO     : checking  %s\n", test, str);
  caps = gst_caps_from_string (str);
  if (!caps) {
    g_print ("%3d, INFO     : no caps from  %s\n", test, str);
    TEST_FAIL;
    return;
  }
  test_caps_func (caps);
}
static void
test_string_fail (gchar *str)
{
  GstCaps *caps;

  TEST_START;
  g_print ("%3d, INFO     : checking  %s  for failure\n", test, str);
  caps = gst_caps_from_string (str);
  TEST_END (caps == NULL);
}
int 
main (int argc, char *argv[]) 
{
  gst_init (&argc, &argv);
goto bla;
bla:
  /* stupidity tests */
  test_caps (gst_caps_new ("no_props", "audio/raw", NULL));

  /* all sorts of caps */
  test_caps (GST_PAD_TEMPLATE_GET (caps1));
  test_caps (GST_PAD_TEMPLATE_GET (caps2));
  test_caps (GST_PAD_TEMPLATE_GET (caps3));
  test_caps (GST_PAD_TEMPLATE_GET (caps4));
  test_caps (GST_PAD_TEMPLATE_GET (caps5));
  test_caps (GST_PAD_TEMPLATE_GET (caps6));
  test_caps (GST_PAD_TEMPLATE_GET (caps7));
  test_caps (GST_PAD_TEMPLATE_GET (caps8));
  test_caps (GST_PAD_TEMPLATE_GET (caps9));
  test_caps (GST_PAD_TEMPLATE_GET (caps10));

  /* mime types */
  test_string ("audio/raw");
  test_string ("\"audio/raw\"");
  test_string ("'audio/raw'");
  test_string ("\\a\\u\\d\\i\\o/\\r\\a\\w");

  /* fixed props entries */
  test_string ("audio/raw  ,test:int=1");
  test_string ("audio/raw ,test:float= 1");
  test_string ("audio/raw, test:fourcc =1");
  test_string ("audio/raw  ,test:i=1");
  test_string ("audio/raw ,test:f= 1");
  test_string ("audio/raw, test:4 =1");
  test_string ("audio/raw,test: fourcc = 0x0000001");
  test_string ("audio/raw , test:fourcc=   RGB");
  test_string ("audio/raw,test :fourcc=  \"RGB \"");
  test_string ("audio/raw    ,   test: string=1");
  test_string ("audio/raw,test= 1");
  test_string ("audio/raw,test   = 1.0");
  test_string ("audio/raw ,test= '1.0'");
  test_string ("audio/raw,test: str= \"1\"");
  test_string ("audio/raw  ,test:b=False");
  test_string ("audio/raw  ,test :bool= trUE");
  test_string ("audio/raw  ,test:b = yes");
  test_string ("audio/raw  ,test : boolean=no");

  /* unfixed props entries */
  test_string ("audio/raw, test= [ 1, 2 ]");
  test_string ("audio/raw, test= [ 1.0 , 2]");
  test_string ("audio/raw, test   = [1, 2.5 ]");
  test_string ("audio/raw, test= [1.3, 2.1 ]");
  test_string ("audio/raw, test :int = [1,2]");
  test_string ("audio/raw, test :float = [1,2]");
  test_string ("audio/raw, test= [int = 1, 2 ]");
  test_string ("audio/raw, test:f= [ float=1.0 , 2]");
  test_string ("audio/raw, test   = [int =1, float = 2.5 ]");
  test_string ("audio/raw, test:float= [1.3, float=2.1 ]");
  test_string ("audio/raw, test :i= [int=1,2]");
  test_string ("audio/raw, test:l= (int=1,2)");
  test_string ("audio/raw, test:list= (int=1 ,2,3    ,int=   4   , 5   ,6 , int  =7  ,8  , int =   9, 10)");
  test_string ("audio/raw, test= (1.0)");
  test_string ("audio/raw, test:list= (\"hi\", 'i dig ya', dude)");
  test_string ("audio/raw, test:l= (int=1,2)");
  test_string ("audio/raw, test:list= (int=1,2)");
  
  /* prop concatenations */
  test_string ("audio/raw, test:float= [1.3, float=2.1 ], test2= [ 1, 2 ]");
  test_string ("audio/raw , test:fourcc=   RGB,test2:int=1");
  test_string ("audio/raw, test= [int = 1, 2 ]      ,test2 :fourcc=  \"RGB \"");
  test_string ("audio/raw, test= [1.3, 2.1 ] , test2= (1.0)");
  test_string ("audio/raw, test:list= (int=1 ,2,3    ,int=   4   , 5   ,6 , int  =7  ,8  , int =   9, 10), test2   = [1, 2.5 ]    ,   test3: string=1  ,test4:i=1");

  /* caps concatenations */
  test_string ("audio/raw, test= [int = 1, 2 ]      ,test2 :fourcc=  \"RGB \";\"audio/raw\"");
  test_string ("audio/raw, test :float = [1,2]    ;  audio/raw, test:fourcc =1 ;'audio/raw', test:list= (\"hi\", 'i dig ya', dude)");
  test_string ("audio/raw, test:float= [1.3, float=2.1 ];audio/raw, test :i= [int=1,2]");


  /* mimes */
  test_string_fail ("audio/raw\\");
  test_string_fail ("'audio/raw");
  test_string_fail ("'audio/raw\"");
  /* wrong type */
  test_string_fail ("audio/raw, test:int = [1.0,2]");
  test_string_fail ("audio/raw, test:int = [1 ,0.2]");
  test_string_fail ("audio/raw, test:int = [1.0, 2.000]");
  /* unmatched */
  test_string_fail ("audio/raw, test:int = [");
  test_string_fail ("audio/raw, test:l = (");
  test_string_fail ("audio/raw, test = \"dood'");
  test_string_fail ("audio/raw, test= '");

  if (failures) {
    g_print ("\n     FAILURES : %d\n", failures);  
  } else {
    g_print ("\n     DONE\n");  
  }
  return failures;
}
