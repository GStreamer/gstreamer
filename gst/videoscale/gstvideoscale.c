/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


/*#define DEBUG_ENABLED */
#include <gstvideoscale.h>



/* elementfactory information */
static GstElementDetails videoscale_details = {
  "Video scaler",
  "Filter/Video/Scaler",
  "Resizes video",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* GstVideoscale signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_WIDTH,
  ARG_HEIGHT,
  ARG_METHOD,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "videoscale_caps",
    "video/raw",
      "format",		GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0'))
  )
)
  
GST_PADTEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "videoscale_caps",
    "video/raw",
      "format",		GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0'))
  )
)

#define GST_TYPE_VIDEOSCALE_METHOD (gst_videoscale_method_get_type())
static GType
gst_videoscale_method_get_type (void)
{
  static GType videoscale_method_type = 0;
  static GEnumValue videoscale_methods[] = {
    { GST_VIDEOSCALE_POINT_SAMPLE, "0", "Point Sample" },
    { GST_VIDEOSCALE_NEAREST,      "1", "Nearest" },
    { GST_VIDEOSCALE_BILINEAR,     "2", "Bilinear" },
    { GST_VIDEOSCALE_BICUBIC,      "3", "Bicubic" },
    { 0, NULL, NULL },
  };
  if (!videoscale_method_type) {
    videoscale_method_type = g_enum_register_static ("GstVideoscaleMethod", videoscale_methods);
  }
  return videoscale_method_type;
}

static void	gst_videoscale_class_init	(GstVideoscaleClass *klass);
static void	gst_videoscale_init		(GstVideoscale *videoscale);

static void	gst_videoscale_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoscale_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_videoscale_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_videoscale_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_videoscale_get_type (void)
{
  static GType videoscale_type = 0;

  if (!videoscale_type) {
    static const GTypeInfo videoscale_info = {
      sizeof(GstVideoscaleClass),      NULL,
      NULL,
      (GClassInitFunc)gst_videoscale_class_init,
      NULL,
      NULL,
      sizeof(GstVideoscale),
      0,
      (GInstanceInitFunc)gst_videoscale_init,
    };
    videoscale_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideoscale", &videoscale_info, 0);
  }
  return videoscale_type;
}

static void
gst_videoscale_class_init (GstVideoscaleClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_WIDTH,
    g_param_spec_int("width","width","width",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HEIGHT,
    g_param_spec_int("height","height","height",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
    g_param_spec_enum("method","method","method",
                      GST_TYPE_VIDEOSCALE_METHOD,0,G_PARAM_READWRITE)); /* CHECKME! */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoscale_set_property;
  gobject_class->get_property = gst_videoscale_get_property;

}

/*
static GstPadNegotiateReturn
videoscale_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstVideoscale *videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  GST_DEBUG(0,"videoscale_negotiate_src");

  if(*caps==NULL){
    return GST_PAD_NEGOTIATE_FAIL;
  }

  *caps = GST_CAPS_NEW ( "videoscale_caps",
		      "video/raw",
		        "format",	GST_PROPS_FOURCC (videoscale->format),
			"width",	GST_PROPS_INT (videoscale->targetwidth),
			"height",	GST_PROPS_INT (videoscale->targetheight)
		    );

  return GST_PAD_NEGOTIATE_AGREE;
}

static GstPadNegotiateReturn
videoscale_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GST_DEBUG(0,"videoscale_negotiate_sink");

  if (*caps==NULL)
    return GST_PAD_NEGOTIATE_FAIL;
  
  return GST_PAD_NEGOTIATE_AGREE;
}
*/

static GstPadConnectReturn
gst_videoscale_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstVideoscale *videoscale;

  GST_DEBUG(0,"gst_videoscale_sinkconnect");
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_CONNECT_DELAYED;
  }

  gst_caps_get_int (caps, "width", &videoscale->width);
  gst_caps_get_int (caps, "height", &videoscale->height);
  gst_caps_get_int (caps, "format", &videoscale->format);

  gst_videoscale_setup(videoscale);

  GST_DEBUG (0,"target size %d x %d",videoscale->targetwidth,
		videoscale->targetheight);

  GST_DEBUG(0,"width %d",videoscale->targetwidth);
  gst_pad_try_set_caps (videoscale->srcpad, 
		    GST_CAPS_NEW (
		      "videoscale_src",
		      "video/raw",
		        "format",	GST_PROPS_FOURCC (videoscale->format),
			"width",	GST_PROPS_INT (videoscale->targetwidth),
			"height",	GST_PROPS_INT (videoscale->targetheight)
		    ));

  return GST_PAD_CONNECT_OK;
}

static void
gst_videoscale_init (GstVideoscale *videoscale)
{
  GST_DEBUG(0,"gst_videoscale_init");
  videoscale->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_templ), "sink");
  /*gst_pad_set_negotiate_function(videoscale->sinkpad,videoscale_negotiate_sink); */
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->sinkpad);
  gst_pad_set_chain_function(videoscale->sinkpad,gst_videoscale_chain);
  gst_pad_set_connect_function(videoscale->sinkpad,gst_videoscale_sinkconnect);

  videoscale->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (src_templ), "src");
  /*gst_pad_set_negotiate_function(videoscale->srcpad,videoscale_negotiate_src); */
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->srcpad);

  videoscale->targetwidth = -1;
  videoscale->targetheight = -1;
  videoscale->method = GST_VIDEOSCALE_NEAREST;
  /*videoscale->method = GST_VIDEOSCALE_BILINEAR; */
  /*videoscale->method = GST_VIDEOSCALE_POINT_SAMPLE; */
}


static void
gst_videoscale_chain (GstPad *pad, GstBuffer *buf)
{
  GstVideoscale *videoscale;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  GST_DEBUG (0,"gst_videoscale_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));
  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(!videoscale->scale_cc){
    gst_caps_get_int (gst_pad_get_caps(pad), "format", &videoscale->format);
    gst_videoscale_setup(videoscale);
  }
  GST_DEBUG (0,"gst_videoscale_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videoscale));
 
GST_DEBUG(0,"size=%ld from=%dx%d to=%dx%d newsize=%d",
	size,
	videoscale->width, videoscale->height,
	videoscale->targetwidth, videoscale->targetheight,
  	videoscale->targetwidth*videoscale->targetheight + videoscale->targetwidth*videoscale->targetheight/2);

  outbuf = gst_buffer_new();
  /* XXX this is wrong for anything but I420 */
  GST_BUFFER_SIZE(outbuf) = videoscale->targetwidth*videoscale->targetheight +
  			    videoscale->targetwidth*videoscale->targetheight/2;
  GST_BUFFER_DATA(outbuf) = g_malloc (videoscale->targetwidth*videoscale->targetheight*2);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  /*g_return_if_fail(videoscale->scale_cc != NULL); */
  videoscale->scale_cc(videoscale, data, GST_BUFFER_DATA(outbuf));

  GST_DEBUG (0,"gst_videoscale_chain: pushing buffer of %d bytes in '%s'",GST_BUFFER_SIZE(outbuf),
		              GST_OBJECT_NAME (videoscale));

  gst_pad_push(videoscale->srcpad, outbuf);

  gst_buffer_unref(buf);
}

static void
gst_videoscale_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOSCALE(object));
  src = GST_VIDEOSCALE(object);

  GST_DEBUG(0,"gst_videoscale_set_property");
  switch (prop_id) {
    case ARG_WIDTH:
      src->targetwidth = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->targetheight = g_value_get_int (value);
      break;
    case ARG_METHOD:
      src->method = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_videoscale_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOSCALE(object));
  src = GST_VIDEOSCALE(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, src->targetwidth);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->targetheight);
      break;
    case ARG_METHOD:
      g_value_set_int (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the videoscale element */
  factory = gst_elementfactory_new("videoscale",GST_TYPE_VIDEOSCALE,
                                   &videoscale_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_templ));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_templ));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoscale",
  plugin_init
};
