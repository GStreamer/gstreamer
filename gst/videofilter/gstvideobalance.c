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

/*
 * This file was (probably) generated from gstvideobalance.c,
 * gstvideobalance.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*#define DEBUG_ENABLED */
#include <gstvideobalance.h>
#include <string.h>
#include <math.h>

/* GstVideobalance signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_CONTRAST,
  ARG_BRIGHTNESS,
  ARG_HUE,
  ARG_SATURATION,
  /* FILL ME */
};

static void	gst_videobalance_base_init	(gpointer g_class);
static void	gst_videobalance_class_init	(gpointer g_class, gpointer class_data);
static void	gst_videobalance_init		(GTypeInstance *instance, gpointer g_class);

static void	gst_videobalance_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videobalance_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_videobalance_planar411(GstVideofilter *videofilter, void *dest, void *src);
static void gst_videobalance_setup(GstVideofilter *videofilter);

GType
gst_videobalance_get_type (void)
{
  static GType videobalance_type = 0;

  if (!videobalance_type) {
    static const GTypeInfo videobalance_info = {
      sizeof(GstVideobalanceClass),
      gst_videobalance_base_init,
      NULL,
      gst_videobalance_class_init,
      NULL,
      NULL,
      sizeof(GstVideobalance),
      0,
      gst_videobalance_init,
    };
    videobalance_type = g_type_register_static(GST_TYPE_VIDEOFILTER,
        "GstVideobalance", &videobalance_info, 0);
  }
  return videobalance_type;
}

static GstVideofilterFormat gst_videobalance_formats[] = {
  { "I420", 12, gst_videobalance_planar411, },
};

  
static void
gst_videobalance_base_init (gpointer g_class)
{
  static GstElementDetails videobalance_details = GST_ELEMENT_DETAILS (
    "Video Balance Control",
    "Filter/Effect/Video",
    "Adjusts brightness, contrast, hue, saturation on a video stream",
    "David Schleef <ds@schleef.org>"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  
  gst_element_class_set_details (element_class, &videobalance_details);

  for(i=0;i<G_N_ELEMENTS(gst_videobalance_formats);i++){
    gst_videofilter_class_add_format(videofilter_class,
	gst_videobalance_formats + i);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_videobalance_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  g_object_class_install_property(gobject_class, ARG_CONTRAST,
      g_param_spec_double("contrast","Contrast","contrast",
      0, 2, 1, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_BRIGHTNESS,
      g_param_spec_double("brightness","Brightness","brightness",
      -1, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_HUE,
      g_param_spec_double("hue","Hue","hue",
      -1, 1, 0, G_PARAM_READWRITE));
  g_object_class_install_property(gobject_class, ARG_SATURATION,
      g_param_spec_double("saturation","Saturation","saturation",
      0, 2, 1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_videobalance_set_property;
  gobject_class->get_property = gst_videobalance_get_property;

  videofilter_class->setup = gst_videobalance_setup;
}

static void
gst_videobalance_init (GTypeInstance *instance, gpointer g_class)
{
  GstVideobalance *videobalance = GST_VIDEOBALANCE (instance);
  GstVideofilter *videofilter;

  GST_DEBUG("gst_videobalance_init");

  videofilter = GST_VIDEOFILTER(videobalance);

  /* do stuff */
  videobalance->contrast = 1;
  videobalance->brightness = 0;
  videobalance->saturation = 1;
  videobalance->hue = 0;

}

static void
gst_videobalance_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideobalance *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOBALANCE(object));
  src = GST_VIDEOBALANCE(object);

  GST_DEBUG("gst_videobalance_set_property");
  switch (prop_id) {
    case ARG_CONTRAST:
      src->contrast = g_value_get_double (value);
      break;
    case ARG_BRIGHTNESS:
      src->brightness = g_value_get_double (value);
      break;
    case ARG_HUE:
      src->hue = g_value_get_double (value);
      break;
    case ARG_SATURATION:
      src->saturation = g_value_get_double (value);
      break;
    default:
      break;
  }
}

static void
gst_videobalance_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideobalance *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOBALANCE(object));
  src = GST_VIDEOBALANCE(object);

  switch (prop_id) {
    case ARG_CONTRAST:
      g_value_set_double (value, src->contrast);
      break;
    case ARG_BRIGHTNESS:
      g_value_set_double (value, src->brightness);
      break;
    case ARG_HUE:
      g_value_set_double (value, src->hue);
      break;
    case ARG_SATURATION:
      g_value_set_double (value, src->saturation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean plugin_init (GstPlugin *plugin)
{
  if(!gst_library_load("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "videobalance", GST_RANK_NONE,
      GST_TYPE_VIDEOBALANCE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videobalance",
  "Changes hue, saturation, brightness etc. on video images",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)

static void gst_videobalance_setup(GstVideofilter *videofilter)
{
  GstVideobalance *videobalance;

  g_return_if_fail(GST_IS_VIDEOBALANCE(videofilter));
  videobalance = GST_VIDEOBALANCE(videofilter);

  /* if any setup needs to be done, do it here */

}

static void gst_videobalance_planar411(GstVideofilter *videofilter,
    void *dest, void *src)
{
  GstVideobalance *videobalance;
  int width;
  int height;
  int x,y;

  g_return_if_fail(GST_IS_VIDEOBALANCE(videofilter));
  videobalance = GST_VIDEOBALANCE(videofilter);

  width = videofilter->from_width;
  height = videofilter->from_height;

  {
    double Y;
    double contrast;
    double brightness;
    guint8 *cdest = dest;
    guint8 *csrc = src;

    contrast = videobalance->contrast;
    brightness = videobalance->brightness;

    for(y=0;y<height;y++){
      for(x=0;x<width;x++){
        Y = csrc[y*width + x];
        Y = 16 + ((Y-16) * contrast + brightness*255);
        if(Y<0)Y=0;
        if(Y>255)Y=255;
        cdest[y*width + x] = rint(Y);
      }
    }
  }

  {
    double u, v;
    double u1, v1;
    double saturation;
    double hue_cos, hue_sin;
    guint8 *usrc, *vsrc;
    guint8 *udest, *vdest;

    saturation = videobalance->saturation;

    usrc = src + width*height;
    udest = dest + width*height;
    vsrc = src + width*height + (width/2)*(height/2);
    vdest = dest + width*height + (width/2)*(height/2);

    /* FIXME this is a bogus transformation for hue, but you get
     * the idea */
    hue_cos = cos(M_PI*videobalance->hue);
    hue_sin = sin(M_PI*videobalance->hue);
  
    for(y=0;y<height/2;y++){
      for(x=0;x<width/2;x++){
        u1 = usrc[y*(width/2) + x] - 128;
        v1 = vsrc[y*(width/2) + x] - 128;
        u = 128 + (( u1 * hue_cos + v1 * hue_sin) * saturation);
        v = 128 + ((-u1 * hue_sin + v1 * hue_cos) * saturation);
        if(u<0)u=0;
        if(u>255)u=255;
        if(v<0)v=0;
        if(v>255)v=255;
        udest[y*(width/2) + x] = rint(u);
        vdest[y*(width/2) + x] = rint(v);
      }
    }
  }

}

