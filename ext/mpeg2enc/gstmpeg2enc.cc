/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmpeg2enc.cc: gstreamer wrapping
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeg2enc.hh"

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2enc_sink",
     "video/x-raw-yuv",
       "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
       "width",        GST_PROPS_INT_RANGE (16, 4096),
       "height",       GST_PROPS_INT_RANGE (16, 4096),
       "framerate",    GST_PROPS_LIST (
			 GST_PROPS_FLOAT (24/1.001),
			 GST_PROPS_FLOAT (24.),
			 GST_PROPS_FLOAT (25.),
			 GST_PROPS_FLOAT (30/1.001),
			 GST_PROPS_FLOAT (30.),
			 GST_PROPS_FLOAT (50.),
			 GST_PROPS_FLOAT (60/1.001),
			 GST_PROPS_FLOAT (60.)
                       )
  )
)

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2enc_src",
     "video/mpeg",
       "systemstream", GST_PROPS_BOOLEAN (FALSE),
       "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
       "width",        GST_PROPS_INT_RANGE (16, 4096),
       "height",       GST_PROPS_INT_RANGE (16, 4096),
       "framerate",    GST_PROPS_LIST (
			 GST_PROPS_FLOAT (24/1.001),
			 GST_PROPS_FLOAT (24.),
			 GST_PROPS_FLOAT (25.),
			 GST_PROPS_FLOAT (30/1.001),
			 GST_PROPS_FLOAT (30.),
			 GST_PROPS_FLOAT (50.),
			 GST_PROPS_FLOAT (60/1.001),
			 GST_PROPS_FLOAT (60.)
                       )
  )
)

static void gst_mpeg2enc_base_init    (GstMpeg2encClass *klass);
static void gst_mpeg2enc_class_init   (GstMpeg2encClass *klass);
static void gst_mpeg2enc_init         (GstMpeg2enc  *enc);
static void gst_mpeg2enc_dispose      (GObject      *object);

static void gst_mpeg2enc_loop         (GstElement   *element);

static GstPadLinkReturn
            gst_mpeg2enc_sink_link    (GstPad       *pad,
				       GstCaps      *caps);
static GstCaps *
            gst_mpeg2enc_src_getcaps  (GstPad       *pad,
				       GstCaps      *caps);

static GstElementStateReturn
            gst_mpeg2enc_change_state (GstElement  *element);

static void gst_mpeg2enc_get_property (GObject      *object,
				       guint         prop_id, 	
				       GValue       *value,
				       GParamSpec   *pspec);
static void gst_mpeg2enc_set_property (GObject      *object,
				       guint         prop_id, 	
				       const GValue *value,
				       GParamSpec   *pspec);

static GstElementClass *parent_class = NULL;

GType
gst_mpeg2enc_get_type (void)
{
  static GType gst_mpeg2enc_type = 0;

  if (!gst_mpeg2enc_type) {
    static const GTypeInfo gst_mpeg2enc_info = {
      sizeof (GstMpeg2encClass),      
      (GBaseInitFunc) gst_mpeg2enc_base_init,
      NULL,
      (GClassInitFunc) gst_mpeg2enc_class_init,
      NULL,
      NULL,
      sizeof (GstMpeg2enc),
      0,
      (GInstanceInitFunc) gst_mpeg2enc_init,
    };

    gst_mpeg2enc_type =
	g_type_register_static (GST_TYPE_ELEMENT,
				"GstMpeg2enc",
				&gst_mpeg2enc_info,
				(GTypeFlags) 0);
  }

  return gst_mpeg2enc_type;
}

static void
gst_mpeg2enc_base_init (GstMpeg2encClass *klass)
{
  static GstElementDetails gst_mpeg2enc_details = {
    "mpeg2enc video encoder",
    "Codec/Video/Encoder",
    "High-quality MPEG-1/2 video encoder",
    "Andrew Stevens <andrew.stevens@nexgo.de>\n"
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (src_templ));
  gst_element_class_add_pad_template (element_class,
	GST_PAD_TEMPLATE_GET (sink_templ));
  gst_element_class_set_details (element_class,
				 &gst_mpeg2enc_details);
}

static void
gst_mpeg2enc_class_init (GstMpeg2encClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  /* register arguments */
  mjpeg_default_handler_verbosity (0);
  GstMpeg2EncOptions::initProperties (object_class);

  object_class->set_property = gst_mpeg2enc_set_property;
  object_class->get_property = gst_mpeg2enc_get_property;

  object_class->dispose = gst_mpeg2enc_dispose;

  element_class->change_state = gst_mpeg2enc_change_state;
}

static void
gst_mpeg2enc_dispose (GObject *object)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (object);

  if (enc->encoder) {
    delete enc->encoder;
    enc->encoder = NULL;
  }
  delete enc->options;
}

static void
gst_mpeg2enc_init (GstMpeg2enc *enc)
{
  GstElement *element = GST_ELEMENT (enc);

  enc->sinkpad = gst_pad_new_from_template (
	gst_element_get_pad_template (element, "sink"), "sink");
  gst_pad_set_link_function (enc->sinkpad, gst_mpeg2enc_sink_link);
  gst_element_add_pad (element, enc->sinkpad);

  enc->srcpad = gst_pad_new_from_template (
	gst_element_get_pad_template (element, "src"), "src");
  gst_pad_set_getcaps_function (enc->srcpad, gst_mpeg2enc_src_getcaps);
  gst_element_add_pad (element, enc->srcpad);

  enc->options = new GstMpeg2EncOptions ();

  gst_element_set_loop_function (element, gst_mpeg2enc_loop);

  enc->encoder = NULL;
}

static void
gst_mpeg2enc_loop (GstElement *element)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (element);

  if (!enc->encoder) {
    GstCaps *caps;

    if (!(caps = GST_PAD_CAPS (enc->sinkpad))) {
      gst_element_error (element,
			 "No format given by previous element");
      return;
    }

    /* create new encoder with these settings */
    enc->encoder = new GstMpeg2Encoder (enc->options, enc->sinkpad,
					caps, enc->srcpad);

    /* and set caps on other side */
    caps = enc->encoder->getFormat ();
    if (gst_pad_try_set_caps (enc->srcpad, caps) <= 0) {
      gst_element_error (element,
			 "Failed to set up encoder properly");
      delete enc->encoder;
      enc->encoder = NULL;
      return;
    }
  }

  enc->encoder->encodePicture ();
}

static GstPadLinkReturn
gst_mpeg2enc_sink_link (GstPad  *pad,
			GstCaps *caps)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  if (enc->encoder) {
    delete enc->encoder;
    enc->encoder = NULL;
  }

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_mpeg2enc_src_getcaps (GstPad  *pad,
			  GstCaps *caps)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (gst_pad_get_parent (pad));

  if (enc->encoder) {
    return enc->encoder->getFormat ();
  }

  return gst_caps_ref (gst_pad_template_get_caps (
	gst_element_get_pad_template (gst_pad_get_parent (pad), "src")));
}

static void
gst_mpeg2enc_get_property (GObject    *object,
			   guint       prop_id, 	
			   GValue     *value,
			   GParamSpec *pspec)
{
  GST_MPEG2ENC (object)->options->getProperty (prop_id, value);
}

static void
gst_mpeg2enc_set_property (GObject      *object,
			   guint         prop_id, 	
			   const GValue *value,
			   GParamSpec   *pspec)
{
  GST_MPEG2ENC (object)->options->setProperty (prop_id, value);
}

static GstElementStateReturn
gst_mpeg2enc_change_state (GstElement  *element)
{
  GstMpeg2enc *enc = GST_MPEG2ENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      delete enc->encoder;
      enc->encoder = NULL;
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "mpeg2enc",
			       GST_RANK_NONE,
			       GST_TYPE_MPEG2ENC);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpeg2enc",
  "High-quality MPEG-1/2 video encoder",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN
)
