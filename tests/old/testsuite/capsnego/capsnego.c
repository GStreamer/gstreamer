
#include <gst/gst.h>

GstPad *srcpad, *sinkpad;
GstPad *srcpadtempl, *sinkpadtempl;

static GstPadTemplate*
src_template_factory (void)
{
  static GstPadTemplate *templ = NULL;

  if (!templ) {
    templ = gst_padtemplate_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "video/raw",
	gst_props_new (
          "height",    GST_PROPS_INT_RANGE (16, 4096),
	  NULL)),
      NULL);
  }
  return templ;
}

static GstPadTemplate*
sink_template_factory (void)
{
  static GstPadTemplate *templ = NULL;

  if (!templ) {
    templ = gst_padtemplate_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_sink",
        "video/raw",
	gst_props_new (
           "height",    GST_PROPS_INT_RANGE (16, 8192),
	   NULL)),
      NULL);
  }
  return templ;
}

static GstCaps*
sink_caps_factory (void)
{
  static GstCaps *caps = NULL;

  if (!caps) {
    caps = gst_caps_new (
      "sink_caps",
      "video/raw",
      gst_props_new (
        "height",     GST_PROPS_INT (3000),
        NULL));
  }
  return caps;
}

static GstCaps*
src_caps_factory (void)
{
  static GstCaps *caps = NULL;

  if (!caps) {
    caps = gst_caps_new (
      "src_caps",
      "video/raw",
      gst_props_new (
        "height",     GST_PROPS_INT (3000),
	NULL));
  }
  return caps;
}

static GstPadNegotiateReturn
negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  g_print (">");

  if (*data == NULL) {
    *data = GINT_TO_POINTER (TRUE);
    *caps = NULL;
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps)
    return GST_PAD_NEGOTIATE_AGREE;

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  g_print ("<");
  if (*data == NULL) {
    *data = GINT_TO_POINTER (TRUE);
    *caps = NULL;
    return GST_PAD_NEGOTIATE_TRY;
  }
  if (*caps)
    return GST_PAD_NEGOTIATE_AGREE;

  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadTemplate *srctempl, *sinktempl;
static GstCaps *srccaps, *sinkcaps;

static gboolean
perform_check  (void)
{
  gboolean result, overall = TRUE;

  gint i, j;

  g_print ("ABC: A=pad caps, B=pad template, C=negotiate function\n");

  for (j=0; j<8; j++) {
    GstPad *srctest, *sinktest;

    for (i=0; i<8; i++) {

      (j & 0x2 ? (sinktest = sinkpadtempl) : (sinktest = sinkpad));
      (j & 0x4 ? (gst_pad_set_caps (sinktest, sinkcaps)) : (gst_pad_set_caps (sinktest, NULL)));
      (j & 0x1 ? (gst_pad_set_negotiate_function (sinktest, negotiate_sink)) :
                gst_pad_set_negotiate_function (sinktest, NULL));
 
      (i & 0x2 ? (srctest = srcpadtempl) : (srctest = srcpad));
      (i & 0x4 ? (gst_pad_set_caps (srctest, srccaps)) : (gst_pad_set_caps (srctest, NULL)));
      (i & 0x1 ? (gst_pad_set_negotiate_function (srctest, negotiate_src)) :
                gst_pad_set_negotiate_function (srctest, NULL));


      g_print ("%d%d%d -> %d%d%d ..", (i&4)>>2, (i&2)>>1, i&1, (j&4)>>2, (j&2)>>1, j&1);
      result = gst_pad_connect (srctest, sinktest);

      g_print (".. %s\n", (result? "ok":"fail"));
      if (result) gst_pad_disconnect (srctest, sinktest);

      overall &= result;
    }
  }
  return overall;
}

int 
main (int argc, char *argv[])
{
  gboolean overall = TRUE;
  
  gst_init (&argc, &argv);

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);

  srctempl = src_template_factory ();
  sinktempl = sink_template_factory ();

  srcpadtempl = gst_pad_new_from_template (src_template_factory (), "src");
  sinkpadtempl = gst_pad_new_from_template (sink_template_factory (), "sink");

  sinkcaps  = sink_caps_factory ();
  srccaps  = src_caps_factory ();

  g_print ("*** compatible caps/templates ***\n");

  overall &= perform_check ();

  gst_caps_set (srccaps, "height", GST_PROPS_INT (9000));

  g_print ("*** incompatible caps ***\n");
  overall &= perform_check ();

  exit (!overall);
}
