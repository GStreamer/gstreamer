/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstvideoflip.h>

#include <stdlib.h>
#include <math.h>
#include <string.h>



/* elementfactory information */
static GstElementDetails videoflip_details = {
  "Video flipper",
  "Filter/Video",
  "LGPL",
  "Flips and rotates video",
  VERSION,
  "David Schleef <ds@schleef.org>",
  "(C) 2003",
};

/* GstVideoflip signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static void	gst_videoflip_class_init	(GstVideoflipClass *klass);
static void	gst_videoflip_init		(GstVideoflip *videoflip);

static void	gst_videoflip_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoflip_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_videoflip_planar411(GstVideofilter *videofilter, void *dest, void *src);
static void gst_videoflip_setup(GstVideofilter *videofilter);

static GstVideoflipClass *this_class = NULL;
static GstVideofilterClass *parent_class = NULL;
static GstElementClass *element_class = NULL;

#define GST_TYPE_VIDEOFLIP_METHOD (gst_videoflip_method_get_type())

static GType
gst_videoflip_method_get_type(void)
{
  static GType videoflip_method_type = 0;
  static GEnumValue videoflip_methods[] = {
    { GST_VIDEOFLIP_METHOD_IDENTITY,	"0", "Identity (no rotation)" },
    { GST_VIDEOFLIP_METHOD_90R,		"1", "Rotate clockwise 90 degrees" },
    { GST_VIDEOFLIP_METHOD_180,		"2", "Rotate 180 degrees" },
    { GST_VIDEOFLIP_METHOD_90L,		"3", "Rotate counter-clockwise 90 degrees" },
    { GST_VIDEOFLIP_METHOD_HORIZ,	"4", "Flip horizontally" },
    { GST_VIDEOFLIP_METHOD_VERT,	"5", "Flip vertically" },
    { GST_VIDEOFLIP_METHOD_TRANS,	"6", "Flip across upper left/lower right diagonal" },
    { GST_VIDEOFLIP_METHOD_OTHER,	"7", "Flip across upper right/lower left diagonal" },
    { 0, NULL, NULL },
  };
  if(!videoflip_method_type){
    videoflip_method_type = g_enum_register_static("GstVideoflipMethod",
	videoflip_methods);
  }
  return videoflip_method_type;
}

GType
gst_videoflip_get_type (void)
{
  static GType videoflip_type = 0;

  if (!videoflip_type) {
    static const GTypeInfo videoflip_info = {
      sizeof(GstVideoflipClass),      NULL,
      NULL,
      (GClassInitFunc)gst_videoflip_class_init,
      NULL,
      NULL,
      sizeof(GstVideoflip),
      0,
      (GInstanceInitFunc)gst_videoflip_init,
    };
    videoflip_type = g_type_register_static(GST_TYPE_VIDEOFILTER, "GstVideoflip", &videoflip_info, 0);
  }
  return videoflip_type;
}

static GstVideofilterFormat gst_videoflip_formats[] = {
  /* planar */
  { "YV12", 12, gst_videoflip_planar411, },
  { "I420", 12, gst_videoflip_planar411, },
  { "IYUV", 12, gst_videoflip_planar411, },
};

static void
gst_videoflip_class_init (GstVideoflipClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideofilterClass *gstvideofilter_class;
  int i;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstvideofilter_class = (GstVideofilterClass *)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_VIDEOFLIP_METHOD, GST_VIDEOFLIP_METHOD_90R,
      G_PARAM_READWRITE));

  this_class = klass;
  parent_class = g_type_class_ref(GST_TYPE_VIDEOFILTER);
  element_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoflip_set_property;
  gobject_class->get_property = gst_videoflip_get_property;

  gstvideofilter_class->setup = gst_videoflip_setup;

  for(i=0;i<G_N_ELEMENTS(gst_videoflip_formats);i++){
    gst_videofilter_class_add_format(gstvideofilter_class,
	gst_videoflip_formats + i);
  }
}

static GstCaps *gst_videoflip_get_capslist(void)
{
  GstVideofilterClass *klass;

  klass = g_type_class_ref(GST_TYPE_VIDEOFILTER);

  return gst_videofilter_class_get_capslist(klass);
}

static GstPadTemplate *
gst_videoflip_src_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("src","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstPadTemplate *
gst_videoflip_sink_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("sink","video/x-raw-yuv",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static void
gst_videoflip_init (GstVideoflip *videoflip)
{
  GstVideofilter *videofilter;

  GST_DEBUG ("gst_videoflip_init");

  videofilter = GST_VIDEOFILTER(videoflip);

  videofilter->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory),
		  "sink");

  videofilter->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory),
		  "src");

  gst_videofilter_postinit(GST_VIDEOFILTER(videoflip));
}

static void
gst_videoflip_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoflip *videoflip;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  videoflip = GST_VIDEOFLIP(object);

  GST_DEBUG ("gst_videoflip_set_property");
  switch (prop_id) {
    case ARG_METHOD:
      videoflip->method = g_value_get_enum (value);
      /* FIXME is this ok? (threading issues) */
      gst_videoflip_setup(GST_VIDEOFILTER(videoflip));
      break;
    default:
      break;
  }
}

static void
gst_videoflip_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoflip *videoflip;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  videoflip = GST_VIDEOFLIP(object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, videoflip->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  if(!gst_library_load("gstvideofilter"))
    return FALSE;

  /* create an elementfactory for the videoflip element */
  factory = gst_element_factory_new("videoflip",GST_TYPE_VIDEOFLIP,
                                   &videoflip_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoflip",
  plugin_init
};


static void gst_videoflip_flip(GstVideoflip *videoflip, unsigned char *dest,
    unsigned char *src, int sw, int sh, int dw, int dh);


static void gst_videoflip_setup (GstVideofilter *videofilter)
{
  int from_width, from_height;
  GstVideoflip *videoflip;

  GST_DEBUG("gst_videoflip_setup");

  videoflip = GST_VIDEOFLIP(videofilter);

  from_width = gst_videofilter_get_input_width(videofilter);
  from_height = gst_videofilter_get_input_height(videofilter);

  if(from_width==0 || from_height==0){
    return;
  }

  switch(videoflip->method){
    case GST_VIDEOFLIP_METHOD_90R:
    case GST_VIDEOFLIP_METHOD_90L:
    case GST_VIDEOFLIP_METHOD_TRANS:
    case GST_VIDEOFLIP_METHOD_OTHER:
      gst_videofilter_set_output_size(videofilter, from_height, from_width);
      break;
    case GST_VIDEOFLIP_METHOD_IDENTITY:
    case GST_VIDEOFLIP_METHOD_180:
    case GST_VIDEOFLIP_METHOD_HORIZ:
    case GST_VIDEOFLIP_METHOD_VERT:
      gst_videofilter_set_output_size(videofilter, from_width, from_height);
      break;
    default:
      g_assert_not_reached();
      break;
  }

  GST_DEBUG ("format=%p \"%s\" from %dx%d to %dx%d",
		videofilter->format, videofilter->format->fourcc,
		from_width, from_height,
		videofilter->to_width, videofilter->to_height);

  if(videoflip->method == GST_VIDEOFLIP_METHOD_IDENTITY){
    GST_DEBUG ("videoflip: using passthru");
    videofilter->passthru = TRUE;
  }else{
    videofilter->passthru = FALSE;
  }

  videofilter->from_buf_size = (videofilter->from_width * videofilter->from_height
		  * videofilter->format->depth) / 8;
  videofilter->to_buf_size = (videofilter->to_width * videofilter->to_height
		  * videofilter->format->depth) / 8;

  videofilter->inited = TRUE;
}

static void gst_videoflip_planar411(GstVideofilter *videofilter,
    void *dest, void *src)
{
  GstVideoflip *videoflip;
  int sw;
  int sh;
  int dw;
  int dh;

  g_return_if_fail(GST_IS_VIDEOFLIP(videofilter));
  videoflip = GST_VIDEOFLIP(videofilter);

  sw = videofilter->from_width;
  sh = videofilter->from_height;
  dw = videofilter->to_width;
  dh = videofilter->to_height;

  GST_DEBUG ("videoflip: scaling planar 4:1:1 %dx%d to %dx%d", sw, sh, dw, dh);

  gst_videoflip_flip(videoflip, dest, src, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  dh = dh>>1;
  dw = dw>>1;
  sh = sh>>1;
  sw = sw>>1;

  gst_videoflip_flip(videoflip, dest, src, sw, sh, dw, dh);

  src += sw*sh;
  dest += dw*dh;

  gst_videoflip_flip(videoflip, dest, src, sw, sh, dw, dh);
}

static void
gst_videoflip_flip(GstVideoflip *videoflip, unsigned char *dest,
    unsigned char *src, int sw, int sh, int dw, int dh)
{
  int x,y;

  switch(videoflip->method){
    case GST_VIDEOFLIP_METHOD_90R:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[(sh - 1 - x)*sw + y];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_90L:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[x*sw + (sw - 1 - y)];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_180:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[(sh - 1 - y)*sw + (sw - 1 - x)];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_HORIZ:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[y*sw + (sw - 1 - x)];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_VERT:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[(sh - 1 - y)*sw + x];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_TRANS:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[x*sw + y];
	}
      }
      break;
    case GST_VIDEOFLIP_METHOD_OTHER:
      for(y=0;y<dh;y++){
        for(x=0;x<dw;x++){
	  dest[y*dw + x] = src[(sh - 1 - x)*sw + (sw - 1 - y)];
	}
      }
      break;
    default:
      /* FIXME */
      break;
  }
}


