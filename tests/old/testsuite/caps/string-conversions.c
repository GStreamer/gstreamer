#include <gst/gst.h>
#include <string.h>

GstStaticCaps caps1 = GST_STATIC_CAPS ("video/mpeg, " "mpegtype=(int){1,2}");

GstStaticCaps caps2 = GST_STATIC_CAPS ("video/mpeg, " "mpegtype=(int){1}");

GstStaticCaps caps3 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}, "
    "width=(int)[16,4096], " "height=(int)[16,4096]");

GstStaticCaps caps4 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc)\"YV12\", " "height=(int)[16,256]");

GstStaticCaps caps5 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YV12\",\"YUY2\"}, " "height=(int)[16,4096]");

GstStaticCaps caps6 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YV12\",\"YUYV\"}, " "height=(int)[16,4096]");

GstStaticCaps caps7 = GST_STATIC_CAPS ("video/raw, "
    "fourcc=(fourcc){\"YVYV\",\"YUY2\"}, " "height=(int)[16,4096]");

GstStaticCaps caps8 = GST_STATIC_CAPS ("video/raw, "
    "format=(fourcc)\"I420\"; " "video/raw, " "format=(fourcc)\"YUYV\"");

GstStaticCaps caps9 = GST_STATIC_CAPS ("video/raw, "
    "format=(fourcc)\"I420\"; " "video/raw, " "format=(fourcc)\"YV12\"");

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
test_caps_func (const GstCaps * caps)
{
  gchar *str1, *str2;
  gboolean ret = FALSE;

  str1 = gst_caps_to_string (caps);
  caps = gst_caps_from_string (str1);
  if (!caps) {
    g_print ("%3d, INFO     : no caps from  %s\n", test, str1);
    TEST_END (ret);
    return;
  }
  str2 = gst_caps_to_string (caps);
  g_print ("%3d, INFO     : %s <==> %s\n", test, str1, str2);
  ret = strcmp (str1, str2) == 0;
  g_free (str1);
  g_free (str2);
  TEST_END (ret);
}
static void
test_caps (const GstCaps * caps)
{
  TEST_START;
  test_caps_func (caps);
}
static void
test_string (gchar * str)
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
test_string_fail (gchar * str)
{
  GstCaps *caps;

  TEST_START;
  g_print ("%3d, INFO     : checking  %s  for failure\n", test, str);
  caps = gst_caps_from_string (str);
  g_print ("got %p\n", caps);
  TEST_END (caps == NULL);
}

int
main (int argc, char *argv[])
{
  gst_init (&argc, &argv);
  goto bla;
bla:
  /* stupidity tests */
  test_caps (gst_caps_new_simple ("audio/raw", NULL));

  /* all sorts of caps */
  test_caps (gst_static_caps_get (&caps1));
  test_caps (gst_static_caps_get (&caps2));
  test_caps (gst_static_caps_get (&caps3));
  test_caps (gst_static_caps_get (&caps4));
  test_caps (gst_static_caps_get (&caps5));
  test_caps (gst_static_caps_get (&caps6));
  test_caps (gst_static_caps_get (&caps7));
  test_caps (gst_static_caps_get (&caps8));
  test_caps (gst_static_caps_get (&caps9));

  /* mime types */
  test_string ("audio/raw");
  test_string ("\"audio/raw\"");

  /* fixed props entries */
  test_string ("audio/raw  ,test=(int)1");
  test_string ("audio/raw ,test=(double) 1");
  test_string ("audio/raw, test=(fourcc )1");
  test_string ("audio/raw  ,test=(i)1");
  test_string ("audio/raw ,test=(d) 1");
  test_string ("audio/raw, test=(4 )1");
  test_string ("audio/raw,test=( fourcc ) 0x0000001");
  test_string ("audio/raw,test =(fourcc)  \"RGB \"");
  test_string ("audio/raw    ,   test=( string)1");
  test_string ("audio/raw,test= 1");
  test_string ("audio/raw,test   = 1.0");
  test_string ("audio/raw ,test= \"1.0\"");
  test_string ("audio/raw,test=( str) \"1\"");
  test_string ("audio/raw  ,test=(b)False");
  test_string ("audio/raw  ,test =(bool) trUE");
  test_string ("audio/raw  ,test=(b ) yes");
  test_string ("audio/raw  ,test =( boolean)no");

  /* unfixed props entries */
  test_string ("audio/raw, test= [ 1, 2 ]");
  test_string_fail ("audio/raw, test= [ 1.0 , 2]");
  test_string_fail ("audio/raw, test   = [1, 2.5 ]");
  test_string ("audio/raw, test= [1.3, 2.1 ]");
  test_string ("audio/raw, test =(int ) [1,2]");
  test_string ("audio/raw, test =(double ) [1,2]");
  test_string ("audio/raw, test= [(int) 1, 2 ]");
  test_string ("audio/raw, test=(d) [ (double)1.0 , 2]");
  test_string ("audio/raw, test=(double) [1.3, (double)2.1 ]");
  test_string ("audio/raw, test =(i) [(int)1,2]");
  test_string ("audio/raw, test={(int)1,2}");
  test_string
      ("audio/raw, test= {(int)1 ,2,3    ,(int)   4   , 5   ,6 , (int  )7  ,8  , (int )   9, 10}");
  test_string ("audio/raw, test= {1.0}");
  test_string ("audio/raw, test= {\"hi\", \"i dig ya\", dude}");
  test_string ("audio/raw, test= {(int)1,2}");
  test_string ("audio/raw, test= {(int)1,2}");

  /* prop concatenations */
  test_string ("audio/raw, test=(double) [1.3, (double)2.1 ], test2= [ 1, 2 ]");
  test_string ("audio/raw , test=(fourcc) \"RGB \",test2=(int)1");
  test_string
      ("audio/raw, test= [(int ) 1, 2 ]      ,test2 =(fourcc)  \"RGB \"");
  test_string ("audio/raw, test= [1.3, 2.1 ] , test2= {1.0}");
  test_string
      ("audio/raw, test= {(int)1 ,2,3    ,(int)   4   , 5   ,6 , (int  )7  ,8  , (int )   9, 10}, test2   = [1.0, 2.5 ]    ,   test3= (string)1  ,test4=(i)1");

  /* caps concatenations */
  test_string
      ("audio/raw, test= [(int ) 1, 2 ]      ,test2 =(fourcc)  \"RGB \";\"audio/raw\"");
  test_string
      ("audio/raw, test =(double ) [1,2]    ;  audio/raw, test=(fourcc )1 ;audio/raw, test= {\"hi\", \"i dig ya\", dude}");
  test_string
      ("audio/raw, test=(double) [1.3, (double)2.1 ];audio/raw, test =(i) [(int)1,2]");


  /* mimes */
  test_string_fail ("audio/raw\\");
  test_string_fail ("'audio/raw");
  test_string_fail ("'audio/raw\"");
  /* wrong type */
  test_string_fail ("audio/raw, test=(int) [1.0,2]");
  test_string_fail ("audio/raw, test=(int) [1 ,0.2]");
  test_string_fail ("audio/raw, test=(int) [1.0, 2.000]");
  /* unmatched */
  test_string_fail ("audio/raw, test=(int = [");
  test_string_fail ("audio/raw, test= {");
  test_string_fail ("audio/raw, test = \"dood'");
  test_string_fail ("audio/raw, test= '");

  if (failures) {
    g_print ("\n     FAILURES : %d\n", failures);
  } else {
    g_print ("\n     DONE\n");
  }
  return failures;
}
