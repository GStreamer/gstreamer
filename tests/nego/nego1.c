#include <gst/gst.h>

/* this is an example of the src pad dictating the caps
 * the sink pad only accepts audio/raw */

static GstPadNegotiateReturn
negotiate (GstPad *pad, GstCaps **caps, gpointer *count) 
{
  g_print ("negotiation entered\n");

  if (!strcmp (gst_caps_get_mime (*caps), "audio/raw"))
    return GST_PAD_NEGOTIATE_AGREE;

  return GST_PAD_NEGOTIATE_FAIL;
}

int 
main(int argc,char *argv[]) 
{
  GstPad *srcpad, *sinkpad;
  GstCaps *new;

  gst_init(&argc,&argv);

  srcpad = gst_pad_new ("src", GST_PAD_SRC);
  sinkpad = gst_pad_new ("sink", GST_PAD_SINK);

  gst_pad_connect (srcpad, sinkpad);

  gst_pad_set_negotiate_function (sinkpad, negotiate);

 /* fill in our desired caps */
  new = gst_caps_new (
          "src_caps",                       /* name */
          "audio/raw",                      /* mime */
          gst_props_new (
            "format",   GST_PROPS_INT (16),
            "depth",    GST_PROPS_INT (16),
            "rate",     GST_PROPS_INT (48000),
            "channels", GST_PROPS_INT (2),
            NULL
          )
        );

  gst_pad_set_caps (srcpad, new);

  new = gst_caps_new (
          "src_caps",                       /* name */
          "video/raw",                      /* mime */
          gst_props_new (
            "format",   GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','V')),
            NULL
          )
        );

  gst_pad_set_caps (srcpad, new);

  exit (0);
}
