/* -*- c-basic-offset: 2 -*-
 * GStreamer
 * Copyright (C) 1999-2001 Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include <gst/gst.h>
#include <math.h>
#include <gst/audio/audio.h>
#include "gstspeed.h"

/* buffer size to make if no bufferpool is available, must be divisible by 
 * sizeof(gfloat) */
#define SPEED_BUFSIZE 4096
/* number of buffers to allocate per chunk in sink buffer pool */
#define SPEED_NUMBUF 6

static GstElementDetails speed_details = {
  "Speed",
  "Filter/Effect",
  "Set speed/pitch on audio/raw streams (resampler)",
  VERSION,
  "Andy Wingo <apwingo@eos.ncsu.edu>",
  "(C) 2001"
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SILENT,
  ARG_SPEED
};

static GstPadTemplate*                    
speed_sink_factory (void)            
{                                         
  static GstPadTemplate *template = NULL; 
                                          
  if (!template) {                        
    template = gst_padtemplate_new 
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/raw",
                                    GST_AUDIO_INT_MONO_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/raw",
                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);
  }                                       
  return template;                        
}

static GstPadTemplate*
speed_src_factory (void)
{
  static GstPadTemplate *template = NULL;
  
  if (!template)
    template = gst_padtemplate_new 
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/raw",
                                      GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/raw",
                                      GST_AUDIO_INT_MONO_PAD_TEMPLATE_PROPS)),
       NULL);
  
  return template;
}

static GstBufferPool*
speed_sink_get_bufferpool (GstPad *pad)
{
  GstSpeed *filter;
  
  filter = GST_SPEED (gst_pad_get_parent(pad));

  return filter->sinkpool;
}

static void		speed_class_init		(GstSpeedClass *klass);
static void		speed_init		(GstSpeed *filter);

static void		speed_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		speed_get_property        (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean		speed_parse_caps          (GstSpeed *filter, GstCaps *caps);

static void		speed_loop              (GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

static GstPadConnectReturn
speed_connect (GstPad *pad, GstCaps *caps)
{
  GstSpeed *filter;
  GstPad *otherpad;
  
  filter = GST_SPEED (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_SPEED (filter), GST_PAD_CONNECT_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);
  
  if (GST_CAPS_IS_FIXED (caps)) {
    if (!speed_parse_caps (filter, caps) || !gst_pad_try_set_caps (otherpad, caps))
      return GST_PAD_CONNECT_REFUSED;
    
    return GST_PAD_CONNECT_OK;
  }
  
  return GST_PAD_CONNECT_DELAYED;
}

static gboolean
speed_parse_caps (GstSpeed *filter, GstCaps *caps)
{
  const gchar *format;
  
  g_return_val_if_fail(filter!=NULL,-1);
  g_return_val_if_fail(caps!=NULL,-1);
  
  gst_caps_get_string(caps, "format", &format);
  
  gst_caps_get_int (caps, "rate", &filter->rate);
  gst_caps_get_int (caps, "channels", &filter->channels);
  
  if (strcmp(format, "int")==0) {
    filter->format        = GST_SPEED_FORMAT_INT;
    gst_caps_get_int	 (caps, "width", 	  &filter->width);
    gst_caps_get_int	 (caps, "depth",      &filter->depth);
    gst_caps_get_int	 (caps, "law",        &filter->law);
    gst_caps_get_int	 (caps, "endianness", &filter->endianness);
    gst_caps_get_boolean (caps, "signed",     &filter->is_signed);

    if (!filter->silent) {
      g_print ("Speed : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Speed : format int, bit width %d, endianness %d, signed %s\n",
               filter->width, filter->endianness, filter->is_signed ? "yes" : "no");
    }
  } else if (strcmp(format, "float")==0) {
    filter->format     = GST_SPEED_FORMAT_FLOAT;
    gst_caps_get_string (caps, "layout",    &filter->layout);
    gst_caps_get_float  (caps, "intercept", &filter->intercept);
    gst_caps_get_float  (caps, "slope",     &filter->slope);

    if (!filter->silent) {
      g_print ("Speed : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Speed : format float, layout %s, intercept %f, slope %f\n",
               filter->layout, filter->intercept, filter->slope);
    }
  } else  {
    return FALSE;
  }
  return TRUE;
}


GType
gst_speed_get_type(void) {
  static GType speed_type = 0;

  if (!speed_type) {
    static const GTypeInfo speed_info = {
      sizeof(GstSpeedClass),      NULL,
      NULL,
      (GClassInitFunc)speed_class_init,
      NULL,
      NULL,
      sizeof(GstSpeed),
      0,
      (GInstanceInitFunc)speed_init,
    };
    speed_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSpeed", &speed_info, 0);
  }
  return speed_type;
}

static void
speed_class_init (GstSpeedClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SPEED,
    g_param_spec_float("speed","speed","speed",
                       0.1,40.0,1.0,G_PARAM_READWRITE));

  gobject_class->set_property = speed_set_property;
  gobject_class->get_property = speed_get_property;
}

static void
speed_init (GstSpeed *filter)
{
  filter->sinkpad = gst_pad_new_from_template(speed_sink_factory (),"sink");
  gst_pad_set_connect_function(filter->sinkpad,speed_connect);
  gst_pad_set_bufferpool_function (filter->sinkpad, speed_sink_get_bufferpool);
  filter->srcpad = gst_pad_new_from_template(speed_src_factory (),"src");
  gst_pad_set_connect_function(filter->srcpad,speed_connect);
  
  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);
  gst_element_set_loop_function(GST_ELEMENT(filter),speed_loop);
  filter->silent = FALSE;
  filter->speed = 1.0;
  
  filter->sinkpool = gst_buffer_pool_get_default(SPEED_BUFSIZE,
                                                 SPEED_NUMBUF);
}

static void
speed_loop (GstElement *element)
{
  GstSpeed *filter = GST_SPEED(element);
  GstBuffer *in, *out;
  guint i, j, nin, nout;
  gfloat interp, speed, lower, i_float;
  
  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_SPEED(filter));

  filter->srcpool = gst_pad_get_bufferpool(filter->srcpad);
  
  i = j = 0;
  speed = filter->speed;
  
  in = gst_pad_pull(filter->sinkpad);
  
  /* this is a bit nasty, but hey, it's what you've got to do to keep the same
   * algorithm and multiple data types in c. */
  if (filter->format==GST_SPEED_FORMAT_FLOAT) {
#define _FORMAT gfloat
#include "filter.func"
#undef _FORMAT
  } else if (filter->format==GST_SPEED_FORMAT_INT && filter->width==16) {
#define _FORMAT gint16
#include "filter.func"
#undef _FORMAT
  } else if (filter->format==GST_SPEED_FORMAT_INT && filter->width==8) {
#define _FORMAT gint8
#include "filter.func"
#undef _FORMAT
  } else {
    gst_element_error (element, "capsnego was never performed, bailing...");
    gst_element_yield (element); /* this is necessary for some reason with loop
                                    elements */
  }
}

static void
speed_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSpeed *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SPEED(object));
  filter = GST_SPEED(object);

  switch (prop_id) 
  {
    case ARG_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    case ARG_SPEED:
      filter->speed = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void
speed_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSpeed *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SPEED(object));
  filter = GST_SPEED(object);

  switch (prop_id) {
    case ARG_SILENT:
      g_value_set_boolean (value, filter->silent);
      break;
    case ARG_SPEED:
      g_value_set_float (value, filter->speed);
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

  factory = gst_elementfactory_new("speed",GST_TYPE_SPEED,
                                   &speed_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, speed_src_factory ());
  gst_elementfactory_add_padtemplate (factory, speed_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "speed",
  plugin_init
};
