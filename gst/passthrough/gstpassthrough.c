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
#include "gstpassthrough.h"

#define PASSTHRU_BUF_SIZE 4096
#define PASSTHRU_NUM_BUFS 4

static GstElementDetails passthrough_details = {
  "Passthrough",
  "Filter/Effect",
  "Transparent filter for audio/raw (boilerplate for effects)",
  VERSION,
  "Thomas <thomas@apestaart.org>, "\
  "Andy Wingo <apwingo@eos.ncsu.edu>",
  "(C) 2001",
};

enum {
  /* FILL ME */
  LAST_SIGNAL
};

/* static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

enum {
  ARG_0,
  ARG_SILENT
};

static GstPadTemplate*                    
passthrough_sink_factory (void)            
{                                         
  static GstPadTemplate *template = NULL; 
                                          
  if (! template) {                        
    template = gst_padtemplate_new 
      ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_append (gst_caps_new ("sink_int",  "audio/raw",
                                     GST_AUDIO_INT_PAD_TEMPLATE_PROPS),
                       gst_caps_new ("sink_float", "audio/raw",
                                     GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS)),
      NULL);
  }                                       
  return template;                        
}

static GstPadTemplate*
passthrough_src_factory (void)
{
  static GstPadTemplate *template = NULL;
  
  if (! template)
    template = gst_padtemplate_new 
      ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
       gst_caps_append (gst_caps_new ("src_float", "audio/raw",
                                      GST_AUDIO_FLOAT_MONO_PAD_TEMPLATE_PROPS),
                        gst_caps_new ("src_int", "audio/raw",
                                      GST_AUDIO_INT_PAD_TEMPLATE_PROPS)),
       NULL);
  
  return template;
}

static void		passthrough_class_init		(GstPassthroughClass *klass);
static void		passthrough_init		(GstPassthrough *filter);

static void		passthrough_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		passthrough_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstPadConnectReturn passthrough_connect_sink	(GstPad *pad, GstCaps *caps);

static void		passthrough_chain		(GstPad *pad, GstBuffer *buf);
static void inline 	passthrough_fast_float_chain 	(gfloat* data, guint numsamples);
static void inline 	passthrough_fast_16bit_chain 	(gint16* data, guint numsamples);
static void inline 	passthrough_fast_8bit_chain	(gint8* data, guint numsamples);

static GstElementClass *parent_class = NULL;

static GstBufferPool*
passthrough_get_bufferpool (GstPad *pad)
{
  GstPassthrough *filter;

  filter = GST_PASSTHROUGH (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (filter->srcpad);
}

static GstPadConnectReturn
passthrough_connect_sink (GstPad *pad, GstCaps *caps)
{
  const gchar *format;
  GstPassthrough *filter;
  
  g_return_val_if_fail (pad  != NULL, GST_PAD_CONNECT_DELAYED);
  g_return_val_if_fail (caps != NULL, GST_PAD_CONNECT_DELAYED);

  filter = GST_PASSTHROUGH (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_PASSTHROUGH (filter), GST_PAD_CONNECT_REFUSED);

  format = gst_caps_get_string(caps, "format");
  
  filter->rate       = gst_caps_get_int (caps, "rate");
  filter->channels   = gst_caps_get_int (caps, "channels");
  
  if (strcmp (format, "int") == 0) {
    filter->format        = GST_PASSTHROUGH_FORMAT_INT;
    filter->width         = gst_caps_get_int (caps, "width");
    filter->depth         = gst_caps_get_int (caps, "depth");
    filter->law           = gst_caps_get_int (caps, "law");
    filter->endianness    = gst_caps_get_int (caps, "endianness");
    filter->is_signed     = gst_caps_get_int (caps, "signed");
    if (! filter->silent) {
      g_print ("Passthrough : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Passthrough : format int, bit width %d, endianness %d, signed %s\n",
               filter->width, filter->endianness, filter->is_signed ? "yes" : "no");
    }
  } else if (strcmp (format, "float") == 0) {
    filter->format     = GST_PASSTHROUGH_FORMAT_FLOAT;
    filter->layout     = gst_caps_get_string (caps, "layout");
    filter->intercept  = gst_caps_get_float  (caps, "intercept");
    filter->slope      = gst_caps_get_float  (caps, "slope");
    if (! filter->silent) {
      g_print ("Passthrough : channels %d, rate %d\n",  
               filter->channels, filter->rate);
      g_print ("Passthrough : format float, layout %s, intercept %f, slope %f\n",
               filter->layout, filter->intercept, filter->slope);
    }
  }

  if (GST_CAPS_IS_FIXED (caps) && ! gst_pad_try_set_caps (filter->srcpad, caps))
    return GST_PAD_CONNECT_REFUSED;
  
  return GST_PAD_CONNECT_DELAYED;
}

GType
gst_passthrough_get_type (void)
{
  static GType passthrough_type = 0;

  if (!passthrough_type) {
    static const GTypeInfo passthrough_info = {
      sizeof (GstPassthroughClass),      NULL,
      NULL,
      (GClassInitFunc) passthrough_class_init,
      NULL,
      NULL,
      sizeof (GstPassthrough),
      0,
      (GInstanceInitFunc) passthrough_init,
    };
    passthrough_type = g_type_register_static (GST_TYPE_ELEMENT, "GstPassthrough", &passthrough_info, 0);
  }
  return passthrough_type;
}

static void
passthrough_class_init (GstPassthroughClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = passthrough_set_property;
  gobject_class->get_property = passthrough_get_property;
}

static void
passthrough_init (GstPassthrough *filter)
{
  filter->srcpad = gst_pad_new_from_template (passthrough_src_factory (),"src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->sinkpad = gst_pad_new_from_template (passthrough_sink_factory (),"sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  gst_pad_set_connect_function    (filter->sinkpad, passthrough_connect_sink);
  gst_pad_set_bufferpool_function (filter->sinkpad, passthrough_get_bufferpool);  
  gst_pad_set_chain_function      (filter->sinkpad, passthrough_chain);

  filter->silent = FALSE;
}

static void
passthrough_chain (GstPad *pad, GstBuffer *buf)
{
  GstPassthrough *filter;
  gint16 *int_data;
  gfloat *float_data;
  
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  
  filter = GST_PASSTHROUGH (gst_pad_get_parent (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_PASSTHROUGH (filter));

  filter->bufpool = gst_pad_get_bufferpool (filter->srcpad);
  if (filter->bufpool == NULL) {
    filter->bufpool = gst_buffer_pool_get_default (PASSTHRU_BUF_SIZE, PASSTHRU_NUM_BUFS);
  }
  
  switch (filter->format) {
  case GST_PASSTHROUGH_FORMAT_INT:
    int_data = (gint16 *) GST_BUFFER_DATA (buf);

    switch (filter->width) {
    case 16:
      passthrough_fast_16bit_chain (int_data, GST_BUFFER_SIZE (buf) / 2);
      break;
    case 8:
      passthrough_fast_8bit_chain ((gint8*) int_data, GST_BUFFER_SIZE (buf));
      break;
    }

    break;
  case GST_PASSTHROUGH_FORMAT_FLOAT:
    float_data = (gfloat *) GST_BUFFER_DATA (buf);
    
    passthrough_fast_float_chain (float_data, GST_BUFFER_SIZE (buf) / sizeof (gfloat));
    
    break;
  }
  
  gst_pad_push (filter->srcpad, buf);
}

static void inline
passthrough_fast_float_chain(gfloat* data, guint num_samples)
#include "filter.func"

static void inline
passthrough_fast_16bit_chain(gint16* data, guint num_samples)
#include "filter.func"

static void inline
passthrough_fast_8bit_chain(gint8* data, guint num_samples)
#include "filter.func"

static void
passthrough_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstPassthrough *filter;

  g_return_if_fail (GST_IS_PASSTHROUGH (object));
  filter = GST_PASSTHROUGH (object);

  switch (prop_id) 
  {
    case ARG_SILENT:
      filter->silent = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
passthrough_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstPassthrough *filter;

  g_return_if_fail (GST_IS_PASSTHROUGH (object));
  filter = GST_PASSTHROUGH (object);

  switch (prop_id) {
    case ARG_SILENT:
      g_value_set_boolean (value, filter->silent);
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

  factory = gst_elementfactory_new ("passthrough", GST_TYPE_PASSTHROUGH, &passthrough_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, passthrough_src_factory ());
  gst_elementfactory_add_padtemplate (factory, passthrough_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "passthrough",
  plugin_init
};
