
#include <gst/gst.h>

GstPad *srcconvpad, *sinkconvpad;
GstPadTemplate *srcconvtempl, *sinkconvtempl;

static GstPadTemplate*
src_conv_factory (void)
{
  return 
    gst_padtemplate_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "rate",    GST_PROPS_INT_RANGE (16, 20000),
	  NULL)),
      NULL);
}

static GstPadTemplate*
sink_conv_factory (void)
{
  return 
    gst_padtemplate_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "rate",    GST_PROPS_INT_RANGE (16, 20000),
	  NULL)),
      NULL);
}

static GstCaps*
src_caps (void)
{
  return
    gst_caps_new (
      "src_caps",
      "audio/raw",
      gst_props_new (
        "rate",     GST_PROPS_INT (3000),
	NULL));
}

static GstCaps *srccaps, *sinkcaps;

static gint src_rate = 140;
static gint sink_rate = 100;

static GstPadNegotiateReturn
negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  g_print (">(%d:%d)", src_rate, (*caps)->refcount);
  src_rate++;

  if (*data == NULL || caps == NULL) {
    g_print ("*");
    *caps = gst_caps_new (
		    "src_caps",
		    "audio/raw",
		    gst_props_new (
			    "rate", GST_PROPS_INT (src_rate),
			    NULL)
		    );
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    gint in_rate = gst_caps_get_int (*caps, "rate");
    g_print ("(%d)", in_rate);

    if (in_rate > 140 && in_rate < 300) {
      g_print ("A");
      return GST_PAD_NEGOTIATE_AGREE;
    }

    *caps = gst_caps_copy_on_write (*caps);
    gst_caps_set (*caps, "rate", GST_PROPS_INT (src_rate));
    g_print ("T");
    return GST_PAD_NEGOTIATE_TRY;
  }

  g_print ("F");
  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{

  g_print ("<(%d:%d:%p)", sink_rate, (*caps)->refcount, *caps);
  sink_rate++;

  if (*data == NULL || *caps == NULL) {
    g_print ("*");
    *caps = gst_caps_new (
		    "sink_caps",
		    "audio/raw",
		    gst_props_new (
			    "rate", GST_PROPS_INT (sink_rate),
			    NULL)
		    );
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    gint in_rate = gst_caps_get_int (*caps, "rate");
    g_print ("(%d)", in_rate);

    if (in_rate >= 100 && in_rate < 140) {
      g_print ("A");
      return GST_PAD_NEGOTIATE_AGREE;
    }

    *caps = gst_caps_copy_on_write (*caps);
    g_print ("%p", *caps);
    gst_caps_set (*caps, "rate", GST_PROPS_INT (sink_rate));

    g_print ("T");
    return GST_PAD_NEGOTIATE_TRY;
  }

  g_print ("F");
  return GST_PAD_NEGOTIATE_FAIL;
}

int 
main (int argc, char *argv[])
{
  gboolean overall = TRUE;
  gboolean result;
  GstElement *queue;
  
  gst_init (&argc, &argv);

  g_mem_chunk_info();

  srcconvtempl = src_conv_factory ();
  sinkconvtempl = sink_conv_factory ();
  srcconvpad = gst_pad_new_from_template (srcconvtempl, "src");
  sinkconvpad = gst_pad_new_from_template (sinkconvtempl, "sink");

  gst_pad_set_negotiate_function (srcconvpad, negotiate_src);
  gst_pad_set_negotiate_function (sinkconvpad, negotiate_sink);

  srccaps  = src_caps ();
  sinkcaps  = gst_caps_copy (srccaps);

  g_print ("The wild goose chase...\n");

  result = gst_pad_connect (srcconvpad, sinkconvpad);
  g_print ("pad connect 1: %d\n", result);
  overall &= (result == TRUE);

  result = gst_pad_set_caps (srcconvpad, srccaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));

  g_print ("with the src negotiate function disabled...\n");
  
  GST_PAD_CAPS (srcconvpad) = NULL;
  GST_PAD_CAPS (sinkconvpad) = NULL;
  src_rate = 140;
  sink_rate = 100;

  gst_pad_set_negotiate_function (srcconvpad, NULL);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (120));
  result = gst_pad_set_caps (srcconvpad, srccaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));


  g_print ("with the sink negotiate function disabled...\n");
  
  GST_PAD_CAPS (srcconvpad) = NULL;
  GST_PAD_CAPS (sinkconvpad) = NULL;
  src_rate = 140;
  sink_rate = 100;

  gst_pad_set_negotiate_function (srcconvpad, negotiate_src);
  gst_pad_set_negotiate_function (sinkconvpad, NULL);

  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (170));
  result = gst_pad_set_caps (sinkconvpad, sinkcaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));

  g_print ("without negotiate functions...\n");
  
  GST_PAD_CAPS (srcconvpad) = NULL;
  GST_PAD_CAPS (sinkconvpad) = NULL;
  src_rate = 140;
  sink_rate = 100;

  gst_pad_set_negotiate_function (srcconvpad, NULL);
  gst_pad_set_negotiate_function (sinkconvpad, NULL);

  sinkcaps = gst_caps_copy (sinkcaps);
  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (150));
  result = gst_pad_set_caps (sinkconvpad, sinkcaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));


  sinkcaps = gst_caps_copy (sinkcaps);
  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (160));
  result = gst_pad_set_caps (sinkconvpad, sinkcaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));

  g_print ("with a proxy element in between...\n");

  gst_pad_disconnect (srcconvpad, sinkconvpad);

  queue = gst_elementfactory_make ("queue", "queue");

  GST_PAD_CAPS (srcconvpad) = NULL;
  GST_PAD_CAPS (sinkconvpad) = NULL;
  src_rate = 140;
  sink_rate = 100;

  gst_pad_set_negotiate_function (srcconvpad, negotiate_src);
  gst_pad_set_negotiate_function (sinkconvpad, negotiate_sink);

  gst_pad_connect (srcconvpad, gst_element_get_pad (queue, "sink"));
  gst_pad_connect (gst_element_get_pad (queue, "src"), sinkconvpad);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (50));
  result = gst_pad_set_caps (srcconvpad, srccaps);
  g_print ("\nset caps on src: %d, final rate: %d\n", result, 
		  gst_caps_get_int (gst_pad_get_caps (srcconvpad), "rate"));


  exit (!overall);
}
