
#include <gst/gst.h>

GstPad *srcpad, *sinkpad;
GstPad *srcconvpad, *sinkconvpad;
GstPad *srcpadtempl, *sinkpadtempl;
GstPad *srcconvtempl, *sinkconvtempl;

gint converter_in = -1, converter_out = -1;
gint target_rate = 2000;

static GstPadFactory src_factory = {
  "src",
  GST_PAD_FACTORY_SRC,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS(
  "test_src",
    "audio/raw",
    "rate",    GST_PROPS_INT_RANGE (16, 20000)
  ),
  NULL,
};

static GstPadFactory src_conv_factory = {
  "src",
  GST_PAD_FACTORY_SRC,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS(
  "test_src",
    "audio/raw",
    "rate",    GST_PROPS_INT_RANGE (16, 20000)
  ),
  NULL,
};

static GstPadFactory sink_conv_factory = {
  "src",
  GST_PAD_FACTORY_SINK,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS(
  "test_src",
    "audio/raw",
    "rate",    GST_PROPS_INT_RANGE (16, 20000)
  ),
  NULL,
};

static GstPadFactory sink_factory = {
  "sink",
  GST_PAD_FACTORY_SINK,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS(
  "test_sink",
    "audio/raw",
    "rate",    GST_PROPS_INT_RANGE (16, 20000)
  ),
  NULL,
};

static GstCapsFactory sink_caps = {
  "sink_caps",
  "audio/raw",
  "rate",     GST_PROPS_INT (6000),
  NULL
};

static GstCapsFactory src_caps = {
  "src_caps",
  "audio/raw",
  "rate",     GST_PROPS_INT (3000),
  NULL
};

static GstPadTemplate *srctempl, *sinktempl;
static GstCaps *srccaps, *sinkcaps;

static GstPadNegotiateReturn
converter_negotiate_src (GstPad *pad, GstCaps **caps, gint counter)
{
  g_print (">");

  if (counter == 0) {
    *caps = NULL;
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    converter_out = gst_caps_get_int (*caps, "rate");
    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
converter_negotiate_sink (GstPad *pad, GstCaps **caps, gint counter)
{
  g_print ("<");
  if (counter == 0) {
    *caps = GST_PAD_CAPS (srcconvpad);
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    converter_in = gst_caps_get_int (*caps, "rate");

    if (counter == 1) {
      converter_out = gst_caps_get_int (*caps, "rate");
      return gst_pad_negotiate_proxy (pad, srcconvpad, caps, counter);
    }
    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
target_negotiate_sink (GstPad *pad, GstCaps **caps, gint counter)
{
  g_print ("{");
  if (counter == 0) {
    *caps = gst_caps_new_with_props (
		    "target_caps",
		    "audio/raw",
		    gst_props_new (
			    "rate", GST_PROPS_INT (target_rate),
			    NULL)
		    );
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    target_rate = gst_caps_get_int (*caps, "rate");
    g_print ("target set %d\n", target_rate);
    return GST_PAD_NEGOTIATE_AGREE;
  }

  return GST_PAD_NEGOTIATE_FAIL;
}

int 
main (int argc, char *argv[])
{
  gboolean overall = TRUE;
  gboolean result;
  
  gst_init (&argc, &argv);

  srctempl = gst_padtemplate_new (&src_factory);
  sinktempl = gst_padtemplate_new (&sink_factory);
  srcpad = gst_pad_new_from_template (srctempl, "src");
  sinkpad = gst_pad_new_from_template (sinktempl, "sink");

  srcconvtempl = gst_padtemplate_new (&src_conv_factory);
  sinkconvtempl = gst_padtemplate_new (&sink_conv_factory);
  srcconvpad = gst_pad_new_from_template (srcconvtempl, "csrc");
  sinkconvpad = gst_pad_new_from_template (sinkconvtempl, "csink");

  gst_pad_set_negotiate_function (srcconvpad, converter_negotiate_src);
  gst_pad_set_negotiate_function (sinkconvpad, converter_negotiate_sink);
  gst_pad_set_negotiate_function (sinkpad, target_negotiate_sink);

  sinkcaps  = gst_caps_register (&sink_caps);
  srccaps  = gst_caps_register (&src_caps);

  g_print ("-------)      (-----------)       (-----   \n");
  g_print ("       !      ! converter !       !        \n");
  g_print ("      src -- csink       csrc -- sink      \n");
  g_print ("-------)      (-----------)       (-----   \n\n");
  g_print ("The convertor first tries to proxy the caps received\n");
  g_print ("on its csink pad to its csrc pad, when that fails, it\n");
  g_print ("sets up the conversion.\n\n");
  

  g_print ("sink pad set caps (rate=%d), converter status: %d %d\n", target_rate, 
		  converter_in, converter_out);
  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (target_rate));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("result: %d, converter status: %d %d, target: %d\n\n", result, 
		  converter_in, converter_out, target_rate);

  result = gst_pad_connect (srcpad, sinkconvpad);
  g_print ("pad connect 1: %d\n", result);
  overall &= (result == TRUE);
  result = gst_pad_connect (srcconvpad, sinkpad);
  g_print ("pad connect 2: %d\n", result);
  overall &= (result == TRUE);

  g_print ("after connect, converter status: %d %d, target %d\n\n", converter_in, converter_out, target_rate);

  g_print ("src pad set caps (rate=%d), converter status: %d %d, target %d \n", gst_caps_get_int (srccaps, "rate"),
		  converter_in, converter_out, target_rate);
  result = gst_pad_set_caps (srcpad, srccaps);
  g_print ("result %d, converter status: %d %d, target %d\n\n", result, 
		  converter_in, converter_out, target_rate);

  g_print ("sink pad set caps (rate=2000), converter status: %d %d, target %d \n",
		  converter_in, converter_out, target_rate);
  target_rate = 2000;
  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (target_rate));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("result %d, converter status: %d %d, target: %d\n\n", result, 
		  converter_in, converter_out, target_rate);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (4000));
  result = gst_pad_renegotiate (srcpad);
  g_print ("sink pad renegotiate caps %d, converter status: %d %d, target: %d\n", result, 
		  converter_in, converter_out, target_rate);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (40000));
  result = gst_pad_set_caps (srcpad, srccaps);
  g_print ("sink pad set caps %d, converter status: %d %d, target: %d\n", result, 
		  converter_in, converter_out, target_rate);

  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (40000));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("sink pad set caps %d, converter status: %d %d, target: %d\n", result, 
		  converter_in, converter_out, target_rate);

  target_rate = 9000;
  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (target_rate));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("sink pad set caps %d, converter status: %d %d, target: %d\n", result, 
		  converter_in, converter_out, target_rate);

  exit (!overall);
}
