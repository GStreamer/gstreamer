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
#include <gst/audio/audio.h>
#include "gstvolume.h"



static GstElementDetails volume_details = {
  "Volume",
  "Filter/Effect",
  "Set volume on audio/raw streams",
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
  ARG_MUTED,
  ARG_VOLUME
};

static GstPadTemplate*                    
volume_sink_factory (void)            
{                                         
  static GstPadTemplate *template = NULL; 
                                          
  if (!template) {                        
    template = gst_padtemplate_new 
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append(gst_caps_new ("sink_int",  "audio/raw",
                                    GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                      gst_caps_new ("sink_float", "audio/raw",
                                    GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);
  }                                       
  return template;                        
}

static GstPadTemplate*
volume_src_factory (void)
{
  static GstPadTemplate *template = NULL;
  
  if (!template)
    template = gst_padtemplate_new 
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/raw",
                                      GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/raw",
                                      GST_AUDIO_INT_PAD_TEMPLATE_PROPS)),
       NULL);
  
  return template;
}

static void		volume_class_init		(GstVolumeClass *klass);
static void		volume_init		(GstVolume *filter);

static void		volume_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		volume_get_property        (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean		volume_parse_caps          (GstVolume *filter, GstCaps *caps);

static void		volume_chain               (GstPad *pad, GstBuffer *buf);
static void inline 	volume_fast_float_chain    (gfloat* data, guint numsamples, GstVolume *filter);
static void inline 	volume_fast_8bit_chain 	   (gint8* data, guint numsamples, GstVolume *filter);
static void inline 	volume_fast_16bit_chain    (gint16* data, guint numsamples, GstVolume *filter);

static GstElementClass *parent_class = NULL;
//static guint gst_filter_signals[LAST_SIGNAL] = { 0 };

static GstBufferPool*
volume_get_bufferpool (GstPad *pad)
{
  GstVolume *filter;

  filter = GST_VOLUME (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (filter->srcpad);
}

static GstPadConnectReturn
volume_connect_sink (GstPad *pad, GstCaps *caps)
{
  GstVolume *filter;
  
  filter = GST_VOLUME (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_VOLUME (filter), GST_PAD_CONNECT_REFUSED);

  if (GST_CAPS_IS_FIXED (caps)) {
    if (!volume_parse_caps (filter, caps) || !gst_pad_try_set_caps (filter->srcpad, caps))
      return GST_PAD_CONNECT_REFUSED;
    
    return GST_PAD_CONNECT_OK;
  }
  
  return GST_PAD_CONNECT_DELAYED;
}

static gboolean
volume_parse_caps (GstVolume *filter, GstCaps *caps)
{
  const gchar *format;
  
  g_return_val_if_fail(filter!=NULL,-1);
  g_return_val_if_fail(caps!=NULL,-1);
  
  format = gst_caps_get_string(caps, "format");
  
  filter->rate       = gst_caps_get_int (caps, "rate");
  filter->channels   = gst_caps_get_int (caps, "channels");
  
  if (strcmp(format, "int")==0) {
    filter->format        = GST_VOLUME_FORMAT_INT;
    filter->width         = gst_caps_get_int (caps, "width");
    filter->depth         = gst_caps_get_int (caps, "depth");
    filter->law           = gst_caps_get_int (caps, "law");
    filter->endianness    = gst_caps_get_int (caps, "endianness");
    filter->is_signed     = gst_caps_get_int (caps, "signed");
    if (!filter->silent) {
      g_print ("Volume : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Volume : format int, bit width %d, endianness %d, signed %s\n",
               filter->width, filter->endianness, filter->is_signed ? "yes" : "no");
    }
  } else if (strcmp(format, "float")==0) {
    filter->format     = GST_VOLUME_FORMAT_FLOAT;
    filter->layout     = gst_caps_get_string(caps, "layout");
    filter->intercept  = gst_caps_get_float(caps, "intercept");
    filter->slope      = gst_caps_get_float(caps, "slope");
    if (!filter->silent) {
      g_print ("Volume : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Volume : format float, layout %s, intercept %f, slope %f\n",
               filter->layout, filter->intercept, filter->slope);
    }
  } else  {
    return FALSE;
  }
  return TRUE;
}


GType
gst_volume_get_type(void) {
  static GType volume_type = 0;

  if (!volume_type) {
    static const GTypeInfo volume_info = {
      sizeof(GstVolumeClass),      NULL,
      NULL,
      (GClassInitFunc)volume_class_init,
      NULL,
      NULL,
      sizeof(GstVolume),
      0,
      (GInstanceInitFunc)volume_init,
    };
    volume_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVolume", &volume_info, 0);
  }
  return volume_type;
}

static void
volume_class_init (GstVolumeClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE,G_PARAM_READWRITE)); // CHECKME
  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTED,
    g_param_spec_boolean("muted","muted","muted",
                         FALSE,G_PARAM_READWRITE));
  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
    g_param_spec_float("volume","volume","volume",
                       -4.0,4.0,1.0,G_PARAM_READWRITE));
  
  gobject_class->set_property = volume_set_property;
  gobject_class->get_property = volume_get_property;
}

static void
volume_init (GstVolume *filter)
{
  filter->sinkpad = gst_pad_new_from_template(volume_sink_factory (),"sink");
  gst_pad_set_connect_function(filter->sinkpad,volume_connect_sink);
  gst_pad_set_bufferpool_function(filter->sinkpad,volume_get_bufferpool);
  filter->srcpad = gst_pad_new_from_template(volume_src_factory (),"src");
  
  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);
  gst_pad_set_chain_function(filter->sinkpad,volume_chain);
  filter->silent = FALSE;
  filter->muted = FALSE;
  filter->volume_i = 8192;
  filter->volume_f = 1.0;
}

static void
volume_chain (GstPad *pad, GstBuffer *buf)
{
  GstVolume *filter;
  gint16 *int_data;
  gfloat *float_data;
  
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  
  filter = GST_VOLUME(GST_OBJECT_PARENT (pad));
  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_VOLUME(filter));
  
  switch (filter->format) {
  case GST_VOLUME_FORMAT_INT:
    int_data = (gint16 *)GST_BUFFER_DATA(buf);

    switch (filter->width) {
    case 16:
      volume_fast_16bit_chain(int_data,GST_BUFFER_SIZE(buf)/2, filter);
      break;
    case 8:
      volume_fast_8bit_chain((gint8*)int_data,GST_BUFFER_SIZE(buf), filter);
      break;
    }

    break;
  case GST_VOLUME_FORMAT_FLOAT:
    float_data = (gfloat *)GST_BUFFER_DATA(buf);
    
    volume_fast_float_chain(float_data,GST_BUFFER_SIZE(buf)/sizeof(float), filter);
    
    break;
  }
  
  gst_pad_push(filter->srcpad,buf);
}

static void inline
volume_fast_float_chain(gfloat* data, guint num_samples, GstVolume *filter)
#include "filter.func"

static void inline
volume_fast_16bit_chain(gint16* data, guint num_samples, GstVolume *filter)
#include "filter.func"

static void inline
volume_fast_8bit_chain(gint8* data, guint num_samples, GstVolume *filter)
#include "filter.func"

static void
volume_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVolume *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VOLUME(object));
  filter = GST_VOLUME(object);

  switch (prop_id) 
  {
  case ARG_SILENT:
    filter->silent = g_value_get_boolean (value);
    break;
  case ARG_MUTED:
    filter->muted = g_value_get_boolean (value);
    break;
  case ARG_VOLUME:
    filter->volume_f       = g_value_get_float (value);
    filter->volume_i       = filter->volume_f*8192;
    break;
  default:
    break;
  }
}

static void
volume_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVolume *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VOLUME(object));
  filter = GST_VOLUME(object);
  
  switch (prop_id) {
  case ARG_SILENT:
    g_value_set_boolean (value, filter->silent);
    break;
  case ARG_MUTED:
    g_value_set_boolean (value, filter->muted);
    break;
  case ARG_VOLUME:
    g_value_set_float (value, filter->volume_f);
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

  factory = gst_elementfactory_new("volume",GST_TYPE_VOLUME,
                                   &volume_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, volume_src_factory ());
  gst_elementfactory_add_padtemplate (factory, volume_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "volume",
  plugin_init
};
