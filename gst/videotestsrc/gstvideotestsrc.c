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

#include <string.h>
#include <stdlib.h>



/* elementfactory information */
static GstElementDetails videotestsrc_details = {
  "Video test source",
  "Source/Video",
  "LGPL",
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
      "format",		GST_PROPS_LIST(
			  GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
			  GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
			  GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R','G','B',' '))
			),
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
static GstElementStateReturn gst_videotestsrc_change_state (GstElement *element);

static void	gst_videotestsrc_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videotestsrc_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer * gst_videotestsrc_get (GstPad *pad);

static GstElementClass *parent_class = NULL;

void gst_videotestsrc_setup (GstVideotestsrc *v);
static void random_chars(unsigned char *dest, int nbytes);
void gst_videotestsrc_smpte_I420 (GstVideotestsrc *v, unsigned char *dest, int w, int h);
void gst_videotestsrc_smpte_YV12 (GstVideotestsrc *v, unsigned char *dest, int w, int h);
void gst_videotestsrc_smpte_RGB (GstVideotestsrc *v, unsigned char *dest, int w, int h);


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

  gstelement_class->change_state = gst_videotestsrc_change_state;
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

  GST_DEBUG (0,"format is 0x%08x\n",videotestsrc->format);

  switch(videotestsrc->format){
  case GST_MAKE_FOURCC('R','G','B',' '):
	  videotestsrc->make_image = gst_videotestsrc_smpte_RGB;
	  videotestsrc->bpp = 16;
	  break;
  case GST_MAKE_FOURCC('I','4','2','0'):
	  videotestsrc->make_image = gst_videotestsrc_smpte_I420;
	  videotestsrc->bpp = 12;
	  break;
  case GST_MAKE_FOURCC('Y','U','Y','V'):
	  //videotestsrc->make_image = gst_videotestsrc_smpte_YUYV;
	  return GST_PAD_CONNECT_REFUSED;
	  break;
  case GST_MAKE_FOURCC('Y','V','1','2'):
	  videotestsrc->make_image = gst_videotestsrc_smpte_YV12;
	  videotestsrc->bpp = 12;
	  break;
  default:
	  return GST_PAD_CONNECT_REFUSED;
  }

  GST_DEBUG (0,"size %d x %d",videotestsrc->width, videotestsrc->height);

  return GST_PAD_CONNECT_OK;
}

static GstElementStateReturn
gst_videotestsrc_change_state (GstElement *element)
{
	GstVideotestsrc *v;

	v = GST_VIDEOTESTSRC(element);

	switch(GST_STATE_TRANSITION(element)){
	case GST_STATE_PAUSED_TO_PLAYING:
		v->pool = gst_pad_get_bufferpool(v->srcpad);
		break;
	case GST_STATE_PLAYING_TO_PAUSED:
		v->pool = NULL;
		break;
	}

	parent_class->change_state(element);

	return GST_STATE_SUCCESS;
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

#if 0
      "bpp",		GST_PROPS_INT(16),
      "depth",		GST_PROPS_INT(16),
      "endianness",	GST_PROPS_INT(1234),
      "red_mask",	GST_PROPS_INT(63488),
      "green_mask",	GST_PROPS_INT(2016),
      "blue_mask",	GST_PROPS_INT(31),
#endif
  videotestsrc->width = 640;
  videotestsrc->height = 480;

  videotestsrc->timestamp = 0;
  videotestsrc->interval = GST_SECOND/30;

  videotestsrc->pool = NULL;
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

#if 0
  /* XXX this is wrong for anything but I420 */
  newsize = videotestsrc->width*videotestsrc->height + 
	  videotestsrc->width*videotestsrc->height/2;
#endif
  /* XXX this is wrong for anything but RGB16 */
  newsize = (videotestsrc->width*videotestsrc->height*videotestsrc->bpp)>>3;

  GST_DEBUG(0,"size=%ld %dx%d",newsize,
	videotestsrc->width, videotestsrc->height);

  buf = NULL;
  if(videotestsrc->pool){
	  buf = gst_buffer_new_from_pool(videotestsrc->pool, 0, 0);
  }
  if(!buf){
  	buf = gst_buffer_new();
  	GST_BUFFER_SIZE(buf) = newsize;
  	GST_BUFFER_DATA(buf) = g_malloc (newsize);
  }
  g_return_val_if_fail(GST_BUFFER_DATA(buf) != NULL, NULL);

  videotestsrc->timestamp += videotestsrc->interval;
  GST_BUFFER_TIMESTAMP(buf) = videotestsrc->timestamp;

  videotestsrc->make_image(videotestsrc, (void *)GST_BUFFER_DATA(buf),
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

static void
paint_rect2 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
{
	unsigned char *d = dest + stride*y + x*2;
	unsigned char *dp;
	int i,j;

	for(i=0;i<h;i++){
		dp = d;
		for(j=0;j<w;j++){
			*dp++ = col[0];
			*dp++ = col[1];
		}
		d += stride;
	}
}
static void
paint_rect3 (unsigned char *dest, int stride, int x, int y, int w, int h, unsigned char *col)
{
	unsigned char *d = dest + stride*y + x*3;
	unsigned char *dp;
	int i,j;

	for(i=0;i<h;i++){
		dp = d;
		for(j=0;j<w;j++){
			*dp++ = col[0];
			*dp++ = col[1];
			*dp++ = col[2];
		}
		d += stride;
	}
}

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q */
static int y_colors[] = { 255, 226, 179, 150, 105,  76,  29,  16,  16,   0 };
static int u_colors[] = { 128,   0, 170,  46, 212,  85, 255, 128,   0, 128 };
static int v_colors[] = { 128, 155,   0,  21, 235, 255, 107, 128, 128, 255 };

void
gst_videotestsrc_smpte_I420 (GstVideotestsrc *v, unsigned char *dest, int w, int h)
{
	unsigned char *yp = dest;
	unsigned char *up = dest + w*h;
	unsigned char *vp = dest + w*h + w*h/4;
	//int h24 = h/24;
	//int j,k;
	int i;
	int y1,y2;

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

/* same as I420, but with U and V swapped */
void
gst_videotestsrc_smpte_YV12 (GstVideotestsrc *v, unsigned char *dest, int w, int h)
{
	unsigned char *yp = dest;
	unsigned char *up = dest + w*h + w*h/4;
	unsigned char *vp = dest + w*h;
	//int h24 = h/24;
	//int j,k;
	int i;
	int y1,y2;

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

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q */
static int r_colors[] = { 255, 255,   0,   0, 255, 255,   0,   0,   0,   0 };
static int g_colors[] = { 255, 255, 255, 255,   0,   0,   0,   0,   0, 128 };
static int b_colors[] = { 255,   0, 255,   0, 255,   0, 255,   0, 128, 255 };

void
gst_videotestsrc_smpte_RGB (GstVideotestsrc *v, unsigned char *dest, int w, int h)
{
	int i;
	int y1,y2;

	y1 = h*2/3;
	y2 = h*0.75;

	/* color bars */
	for(i=0;i<7;i++){
		int x1 = i*w/7;
		int x2 = (i+1)*w/7;
		unsigned char col[2];

		col[0] = (g_colors[i]&0xe0) | (b_colors[i]>>3);
		col[1] = (r_colors[i]&0xf8) | (g_colors[i]>>5);
		paint_rect2(dest, w*2, x1, 0, x2-x1, y1, col);
	}

	/* inverse blue bars */
	for(i=0;i<7;i++){
		int x1 = i*w/7;
		int x2 = (i+1)*w/7;
		unsigned char col[2];
		int k;

		if(i&1){
			k = 7;
		}else{
			k = 6-i;
		}
		col[0] = (g_colors[k]&0xe0) | (b_colors[k]>>3);
		col[1] = (r_colors[k]&0xf8) | (g_colors[k]>>5);
		paint_rect2(dest, w*2, x1, y1, x2-x1, y2-y1, col);
	}

	/* -I, white, Q regions */
	for(i=0;i<3;i++){
		int x1 = i*w/6;
		int x2 = (i+1)*w/6;
		unsigned char col[2];
		int k;

		if(i==0){ k = 8; }
		else if(i==1){ k = 0; }
		else k = 9;

		col[0] = (g_colors[k]&0xe0) | (b_colors[k]>>3);
		col[1] = (r_colors[k]&0xf8) | (g_colors[k]>>5);
		paint_rect2(dest, w*2, x1, y2, x2-x1, h-y2, col);
	}

	{
		int x1 = w/2;
		int x2 = w-1;
		paint_rect_random(dest, w*2, x1*2, y2, (x2-x1)*2, h-y2);
	}
}

