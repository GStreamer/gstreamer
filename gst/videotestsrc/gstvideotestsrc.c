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
#include <gstvideotestsrc.h>

#include <stdlib.h>



/* elementfactory information */
static GstElementDetails videotestsrc_details = {
  "Video test source",
  "Source/Video",
  "Creates a test video stream",
  VERSION,
  "David A. Schleef <ds@schleef.org>",
  "(C) 2002",
};

/* GstVideotestsrc signals and args */
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

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "videotestsrc_caps",
    "video/raw",
      "format",		GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
      "width",		GST_PROPS_INT(640),
      "height",		GST_PROPS_INT(480)
  )
)

#define GST_TYPE_VIDEOTESTSRC_METHOD (gst_videotestsrc_method_get_type())
static GType
gst_videotestsrc_method_get_type (void)
{
  static GType videotestsrc_method_type = 0;
  static GEnumValue videotestsrc_methods[] = {
    { GST_VIDEOTESTSRC_POINT_SAMPLE, "0", "Point Sample" },
    { GST_VIDEOTESTSRC_NEAREST,      "1", "Nearest" },
    { GST_VIDEOTESTSRC_BILINEAR,     "2", "Bilinear" },
    { GST_VIDEOTESTSRC_BICUBIC,      "3", "Bicubic" },
    { 0, NULL, NULL },
  };
  if (!videotestsrc_method_type) {
    videotestsrc_method_type = g_enum_register_static ("GstVideotestsrcMethod", videotestsrc_methods);
  }
  return videotestsrc_method_type;
}

static void	gst_videotestsrc_class_init	(GstVideotestsrcClass *klass);
static void	gst_videotestsrc_init		(GstVideotestsrc *videotestsrc);

static void	gst_videotestsrc_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videotestsrc_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer * gst_videotestsrc_get (GstPad *pad);

static GstElementClass *parent_class = NULL;

void gst_videotestsrc_setup (GstVideotestsrc *v);
static void random_chars(unsigned char *dest, int nbytes);
static void gst_videotestsrc_random_yuv (GstVideotestsrc *v, unsigned char *dest, int w, int h);


GType
gst_videotestsrc_get_type (void)
{
  static GType videotestsrc_type = 0;

  if (!videotestsrc_type) {
    static const GTypeInfo videotestsrc_info = {
      sizeof(GstVideotestsrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_videotestsrc_class_init,
      NULL,
      NULL,
      sizeof(GstVideotestsrc),
      0,
      (GInstanceInitFunc)gst_videotestsrc_init,
    };
    videotestsrc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideotestsrc", &videotestsrc_info, 0);
  }
  return videotestsrc_type;
}

static void
gst_videotestsrc_class_init (GstVideotestsrcClass *klass)
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
                      GST_TYPE_VIDEOTESTSRC_METHOD,0,G_PARAM_READWRITE)); /* CHECKME! */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videotestsrc_set_property;
  gobject_class->get_property = gst_videotestsrc_get_property;

}

static GstPadConnectReturn
gst_videotestsrc_srcconnect (GstPad *pad, GstCaps *caps)
{
  GstVideotestsrc *videotestsrc;

  GST_DEBUG(0,"gst_videotestsrc_srcconnect");
  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

#if 0
  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_CONNECT_DELAYED;
  }
#endif

  gst_caps_get_fourcc_int (caps, "format", &videotestsrc->format);
  gst_caps_get_int (caps, "width", &videotestsrc->width);
  gst_caps_get_int (caps, "height", &videotestsrc->height);

  gst_videotestsrc_setup(videotestsrc);

  GST_DEBUG (0,"size %d x %d",videotestsrc->width, videotestsrc->height);

  return GST_PAD_CONNECT_OK;
}

static void
gst_videotestsrc_init (GstVideotestsrc *videotestsrc)
{
  GST_DEBUG(0,"gst_videotestsrc_init");

  /*gst_pad_set_negotiate_function(videotestsrc->sinkpad,videotestsrc_negotiate_sink); */
  //gst_element_add_pad(GST_ELEMENT(videotestsrc),videotestsrc->sinkpad);
  //gst_pad_set_chain_function(videotestsrc->sinkpad,gst_videotestsrc_chain);

  videotestsrc->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_templ), "src");
  /*gst_pad_set_negotiate_function(videotestsrc->srcpad,videotestsrc_negotiate_src); */
  gst_element_add_pad(GST_ELEMENT(videotestsrc),videotestsrc->srcpad);
  gst_pad_set_get_function(videotestsrc->srcpad,gst_videotestsrc_get);
  gst_pad_set_connect_function(videotestsrc->srcpad,gst_videotestsrc_srcconnect);

  videotestsrc->width = 640;
  videotestsrc->height = 480;
}


static GstBuffer *
gst_videotestsrc_get (GstPad *pad)
{
  GstVideotestsrc *videotestsrc;
  gulong newsize;
  GstBuffer *buf;

  GST_DEBUG (0,"gst_videotestsrc_get");

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  videotestsrc = GST_VIDEOTESTSRC (gst_pad_get_parent (pad));

  newsize = videotestsrc->width*videotestsrc->height + 
	  videotestsrc->width*videotestsrc->height/2;

  GST_DEBUG(0,"size=%ld %dx%d",newsize,
	videotestsrc->width, videotestsrc->height);

  buf = gst_buffer_new();
  /* XXX this is wrong for anything but I420 */
  GST_BUFFER_SIZE(buf) = newsize;
  GST_BUFFER_DATA(buf) = g_malloc (newsize);
  g_return_val_if_fail(GST_BUFFER_DATA(buf) != NULL, NULL);
  //GST_BUFFER_TIMESTAMP(buf) = GST_BUFFER_TIMESTAMP(buf);

  gst_videotestsrc_random_yuv(videotestsrc, (void *)GST_BUFFER_DATA(buf),
		  videotestsrc->width, videotestsrc->height);

  return buf;
}

static void
gst_videotestsrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideotestsrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOTESTSRC(object));
  src = GST_VIDEOTESTSRC(object);

  GST_DEBUG(0,"gst_videotestsrc_set_property");
  switch (prop_id) {
    case ARG_WIDTH:
      src->width = g_value_get_int (value);
      break;
    case ARG_HEIGHT:
      src->height = g_value_get_int (value);
      break;
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      break;
  }
}

static void
gst_videotestsrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideotestsrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOTESTSRC(object));
  src = GST_VIDEOTESTSRC(object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, src->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, src->height);
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

  /* create an elementfactory for the videotestsrc element */
  factory = gst_element_factory_new("videotestsrc",GST_TYPE_VIDEOTESTSRC,
                                   &videotestsrc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_templ));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videotestsrc",
  plugin_init
};



/* Non-GST specific stuff */

void
gst_videotestsrc_setup (GstVideotestsrc *v)
{

}

static void
random_chars(unsigned char *dest, int nbytes)
{
	int i;
	static unsigned int state;

	for(i=0;i<nbytes;i++){
		state *= 1103515245;
		state += 12345;
		dest[i] = (state>>16);
	}
}

static void
paint_rect_random (unsigned char *dest, int stride, int x, int y, int w, int h)
{
	unsigned char *d = dest + stride*y + x;
	int i;

	for(i=0;i<h;i++){
		random_chars(d,w);
		d += stride;
	}
}

static void
paint_rect (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char color)
{
	unsigned char *d = dest + stride*y + x;
	int i;

	for(i=0;i<h;i++){
		memset(d,color,w);
		d += stride;
	}
}

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q */
static int y_colors[] = { 255, 226, 179, 150, 105,  76,  29,   0,   0,   0 };
static int u_colors[] = { 128,   0, 170,  46, 212,  85, 255, 128,   0, 128 };
static int v_colors[] = { 128, 155,   0,  21, 235, 255, 107, 128, 128, 255 };

static void
gst_videotestsrc_random_yuv (GstVideotestsrc *v, unsigned char *dest, int w, int h)
{
	unsigned char *yp = dest;
	unsigned char *up = dest + w*h;
	unsigned char *vp = up + w*h/4;
	//int h24 = h/24;
	//int j,k;
	int i;
	int y1,y2;

#if 0
	//memset(dest,255,w*h/2);
	random_chars(dest + w*h/2,w*h/2);

	//random_chars(dest + w*h, w*h/4);
	memset(dest + w*h, 128, w*h/4);
	
	//random_chars(dest + w*h + w*h/4, w*h/4);
	memset(dest + w*h + w*h/4, 128, w*h/4);
#endif

	y1 = h/3;
	y2 = h*0.375;

	/* color bars */
	for(i=0;i<7;i++){
		int x1 = i*(w/2)/7;
		int x2 = (i+1)*(w/2)/7;
		paint_rect(yp, w, x1*2, 0, (x2-x1)*2, y1*2, y_colors[i]);
		paint_rect(up, w/2, x1, 0, x2-x1, y1, u_colors[i]);
		paint_rect(vp, w/2, x1, 0, x2-x1, y1, v_colors[i]);
	}

	/* inverse blue bars */
	for(i=0;i<7;i++){
		int x1 = i*(w/2)/7;
		int x2 = (i+1)*(w/2)/7;
		int k;
		if(i&1){
			k = 7;
		}else{
			k = 6-i;
		}
		paint_rect(yp, w, x1*2, y1*2, (x2-x1)*2, (y2-y1)*2, y_colors[k]);
		paint_rect(up, w/2, x1, y1, x2-x1, y2-y1, u_colors[k]);
		paint_rect(vp, w/2, x1, y1, x2-x1, y2-y1, v_colors[k]);
	}

	/* -I, white, Q regions */
	for(i=0;i<3;i++){
		int x1 = i*(w/2)/6;
		int x2 = (i+1)*(w/2)/6;
		int k;

		if(i==0){ k = 8; }
		else if(i==1){ k = 0; }
		else k = 9;

		paint_rect(yp, w, x1*2, y2*2, (x2-x1)*2, h-y2*2, y_colors[k]);
		paint_rect(up, w/2, x1, y2, x2-x1, h/2-y2, u_colors[k]);
		paint_rect(vp, w/2, x1, y2, x2-x1, h/2-y2, v_colors[k]);
	}

	{
		int x1 = 3*(w/2)/6;
		int x2 = w/2;
		paint_rect_random(yp, w, x1*2, y2*2, (x2-x1)*2, h-y2*2);
		paint_rect(up, w/2, x1, y2, x2-x1, h/2-y2, u_colors[0]);
		paint_rect(vp, w/2, x1, y2, x2-x1, h/2-y2, v_colors[0]);
	}
}

