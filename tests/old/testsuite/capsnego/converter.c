
#include <gst/gst.h>

GstPad *srcpad, *sinkpad;
GstPad *srcconvpad, *sinkconvpad;
GstPadTemplate *srcpadtempl, *sinkpadtempl;
GstPadTemplate *srcconvtempl, *sinkconvtempl;

gint converter_in = -1, converter_out = -1;

static GstPadTemplate*
src_factory (void)
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
      "src",
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

static GstPadTemplate*
sink_factory (void)
{
  return 
    gst_padtemplate_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_sink",
        "audio/raw",
	gst_props_new (
          "rate",    GST_PROPS_INT_RANGE (16, 20000),
	  NULL)),
      NULL);
}

static GstCaps*
sink_caps (void)
{
  return 
    gst_caps_new (
      "sink_caps",
      "audio/raw",
      gst_props_new (
        "rate",     GST_PROPS_INT (6000),
	NULL));
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

static GstPadTemplate *srctempl, *sinktempl;
static GstCaps *srccaps, *sinkcaps;

static GstPadNegotiateReturn
negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  g_print (">");

  if (data == NULL) {
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
negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  g_print ("<");
  if (data == NULL) {
    *caps = NULL;
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps) {
    converter_in = gst_caps_get_int (*caps, "rate");
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

  srctempl = src_factory ();
  sinktempl = sink_factory ();
  srcpad = gst_pad_new_from_template (srctempl, "src");
  sinkpad = gst_pad_new_from_template (sinktempl, "sink");

  srcconvtempl = src_conv_factory ();
  sinkconvtempl = sink_conv_factory ();
  srcconvpad = gst_pad_new_from_template (srcconvtempl, "src");
  sinkconvpad = gst_pad_new_from_template (sinkconvtempl, "sink");

  gst_pad_set_negotiate_function (srcconvpad, negotiate_src);
  gst_pad_set_negotiate_function (sinkconvpad, negotiate_sink);

  sinkcaps  = sink_caps ();
  srccaps  = src_caps ();

  result = gst_pad_set_caps (srcpad, srccaps);
  g_print ("set caps on src: %d\n", result);
  g_print ("initial converter status: %d %d\n", converter_in, converter_out);

  /* result = FIXME */ gst_pad_connect (srcpad, sinkconvpad);
  g_print ("pad connect 1: %d\n", result);
  overall &= (result == TRUE);
  /* result = FIXME */ gst_pad_connect (srcconvpad, sinkpad);
  g_print ("pad connect 2: %d\n", result);
  overall &= (result == TRUE);

  g_print ("after connect, converter status: %d %d\n", converter_in, converter_out);

  result = gst_pad_set_caps (srcpad, srccaps);
  g_print ("src pad set caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("sink pad set caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (4000));
  result = gst_pad_renegotiate (srcpad);
  g_print ("sink pad renegotiate caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  gst_caps_set (srccaps, "rate", GST_PROPS_INT (40000));
  result = gst_pad_set_caps (srcpad, srccaps);
  g_print ("sink pad set caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (40000));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("sink pad set caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  gst_caps_set (sinkcaps, "rate", GST_PROPS_INT (9000));
  result = gst_pad_set_caps (sinkpad, sinkcaps);
  g_print ("sink pad set caps %d, converter status: %d %d\n", result, converter_in, converter_out);

  exit (!overall);
}
