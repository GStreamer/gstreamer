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
#include <videoscale.h>



/* elementfactory information */
static GstElementDetails videoscale_details = {
  "Video scaler",
  "Filter/Video",
  "LGPL",
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
static GstCaps * gst_videoscale_get_capslist(void);

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

static GstPadTemplate *
gst_videoscale_src_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("src","video/raw",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT));

    caps = gst_caps_intersect(caps, gst_videoscale_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstPadTemplate *
gst_videoscale_sink_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("sink","video/raw",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT));

    caps = gst_caps_intersect(caps, gst_videoscale_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstCaps *
gst_videoscale_get_capslist(void)
{
  static GstCaps *capslist = NULL;
  GstCaps *caps;
  int i;

  if (capslist){
    gst_caps_ref(capslist);
    return capslist;
  }

  for(i=0;i<videoscale_n_formats;i++){
    caps = videoscale_get_caps(videoscale_formats + i);
    capslist = gst_caps_append(capslist, caps);
  }

  gst_caps_ref(capslist);
  return capslist;
}

static GstCaps *
gst_videoscale_getcaps (GstPad *pad, GstCaps *caps)
{
  GstVideoscale *videoscale;
  GstCaps *capslist = NULL;
  GstCaps *peercaps;
  GstCaps *sizecaps;
  int i;

  GST_DEBUG(0,"gst_videoscale_src_link");
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));
  
  /* get list of peer's caps */
  if(pad == videoscale->srcpad){
    peercaps = gst_pad_get_allowed_caps (videoscale->sinkpad);
  }else{
    peercaps = gst_pad_get_allowed_caps (videoscale->srcpad);
  }

  /* FIXME videoscale doesn't allow passthru of video formats it
   * doesn't understand. */
  /* Look through our list of caps and find those that match with
   * the peer's formats.  Create a list of them. */
  for(i=0;i<videoscale_n_formats;i++){
    GstCaps *fromcaps = videoscale_get_caps(videoscale_formats + i);
    if(gst_caps_is_always_compatible(fromcaps, peercaps)){
      capslist = gst_caps_append(capslist, fromcaps);
    }
    gst_caps_unref (fromcaps);
  }
  gst_caps_unref (peercaps);

  sizecaps = GST_CAPS_NEW("videoscale_size","video/raw",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT));

  caps = gst_caps_intersect(caps, gst_videoscale_get_capslist ());
  gst_caps_unref (sizecaps);

  return caps;
}


static GstPadLinkReturn
gst_videoscale_src_link (GstPad *pad, GstCaps *caps)
{
  GstVideoscale *videoscale;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG(0,"gst_videoscale_src_link");
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  videoscale->format = videoscale_find_by_caps (caps);
  gst_caps_get_int (caps, "width", &videoscale->to_width);
  gst_caps_get_int (caps, "height", &videoscale->to_height);

  gst_videoscale_setup(videoscale);

  GST_DEBUG(0,"width %d height %d",videoscale->to_width,videoscale->to_height);

  peercaps = gst_caps_copy(caps);

  gst_caps_set(peercaps, "width", GST_PROPS_INT_RANGE (0, G_MAXINT));
  gst_caps_set(peercaps, "height", GST_PROPS_INT_RANGE (0, G_MAXINT));

  ret = gst_pad_try_set_caps (videoscale->srcpad, peercaps);

  gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK){
    caps = gst_pad_get_caps (videoscale->srcpad);

    gst_caps_get_int (caps, "width", &videoscale->from_width);
    gst_caps_get_int (caps, "height", &videoscale->from_height);
    gst_videoscale_setup(videoscale);
  }

  return ret;
}

static GstPadLinkReturn
gst_videoscale_sink_link (GstPad *pad, GstCaps *caps)
{
  GstVideoscale *videoscale;
  GstPadLinkReturn ret;
  GstCaps *peercaps;

  GST_DEBUG(0,"gst_videoscale_src_link");
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  videoscale->format = videoscale_find_by_caps (caps);
  gst_caps_get_int (caps, "width", &videoscale->from_width);
  gst_caps_get_int (caps, "height", &videoscale->from_height);

  gst_videoscale_setup(videoscale);

  peercaps = gst_caps_copy(caps);

  if(videoscale->force_size){
    gst_caps_set(peercaps, "width", GST_PROPS_INT (videoscale->forced_width));
    gst_caps_set(peercaps, "height", GST_PROPS_INT (videoscale->forced_height));
  }else{
    gst_caps_set(peercaps, "width", GST_PROPS_INT_RANGE (0, G_MAXINT));
    gst_caps_set(peercaps, "height", GST_PROPS_INT_RANGE (0, G_MAXINT));
  }

  ret = gst_pad_try_set_caps (videoscale->srcpad, peercaps);

  gst_caps_unref(peercaps);

  if(ret==GST_PAD_LINK_OK){
    caps = gst_pad_get_caps (videoscale->srcpad);

    gst_caps_get_int (caps, "width", &videoscale->to_width);
    gst_caps_get_int (caps, "height", &videoscale->to_height);
    gst_videoscale_setup(videoscale);
  }

  return ret;
}

static void
gst_videoscale_init (GstVideoscale *videoscale)
{
  GST_DEBUG(0,"gst_videoscale_init");
  videoscale->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoscale_sink_template_factory),
		  "sink");
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->sinkpad);
  gst_pad_set_chain_function(videoscale->sinkpad,gst_videoscale_chain);
  gst_pad_set_link_function(videoscale->sinkpad,gst_videoscale_sink_link);
  gst_pad_set_getcaps_function(videoscale->sinkpad,gst_videoscale_getcaps);

  videoscale->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoscale_src_template_factory),
		  "src");
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->srcpad);
  gst_pad_set_link_function(videoscale->srcpad,gst_videoscale_src_link);
  gst_pad_set_getcaps_function(videoscale->srcpad,gst_videoscale_getcaps);

  videoscale->inited = FALSE;
  videoscale->force_size = FALSE;

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
  g_return_if_fail (videoscale->inited);

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(videoscale->passthru){
    gst_pad_push(videoscale->srcpad, buf);
    return;
  }

  GST_DEBUG (0,"gst_videoscale_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videoscale));
 
  GST_DEBUG(0,"size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
	size,
	videoscale->from_width, videoscale->from_height,
	videoscale->to_width, videoscale->to_height,
  	size, videoscale->from_buf_size,
  	videoscale->to_buf_size);

  g_return_if_fail (size == videoscale->from_buf_size);

  outbuf = gst_buffer_new();
  /* FIXME: handle bufferpools */
  GST_BUFFER_SIZE(outbuf) = videoscale->to_buf_size;
  GST_BUFFER_DATA(outbuf) = g_malloc (videoscale->to_buf_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  g_return_if_fail(videoscale->format);
  GST_DEBUG (0,"format %s",videoscale->format->fourcc);
  g_return_if_fail(videoscale->format->scale);

  videoscale->format->scale(videoscale, GST_BUFFER_DATA(outbuf), data);

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
      src->forced_width = g_value_get_int (value);
      src->force_size = TRUE;
      break;
    case ARG_HEIGHT:
      src->forced_height = g_value_get_int (value);
      src->force_size = TRUE;
      break;
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
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
      g_value_set_int (value, src->forced_width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->forced_height);
      break;
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
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
  factory = gst_element_factory_new("videoscale",GST_TYPE_VIDEOSCALE,
                                   &videoscale_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoscale_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoscale_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoscale",
  plugin_init
};
