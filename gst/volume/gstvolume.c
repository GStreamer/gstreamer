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
#include <gst/control/control.h>
#include "gstvolume.h"



static GstElementDetails volume_details = {
  "Volume",
  "Filter/Audio/Effect",
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
  ARG_MUTE,
  ARG_VOLUME
};

GST_PAD_TEMPLATE_FACTORY (volume_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "volume_float_sink",
    "audio/raw",
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "format",     GST_PROPS_STRING ("float"),
    "layout",     GST_PROPS_STRING ("gfloat"),
    "intercept",  GST_PROPS_FLOAT(0.0),
    "slope",      GST_PROPS_FLOAT(1.0),
    "channels",   GST_PROPS_INT (1)
  ),
  GST_CAPS_NEW (
    "volume_int_sink",
    "audio/raw",
    "format",     GST_PROPS_STRING ("int"),
    "channels",   GST_PROPS_INT_RANGE (1, G_MAXINT),
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "law",        GST_PROPS_INT (0),
    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
    "width",      GST_PROPS_INT (16),
    "depth",      GST_PROPS_INT (16),
    "signed",     GST_PROPS_BOOLEAN (TRUE)
  )
);

GST_PAD_TEMPLATE_FACTORY (volume_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "volume_float_src",
    "audio/raw",
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "format",     GST_PROPS_STRING ("float"),
    "layout",     GST_PROPS_STRING ("gfloat"),
    "intercept",  GST_PROPS_FLOAT(0.0),
    "slope",      GST_PROPS_FLOAT(1.0),
    "channels",   GST_PROPS_INT (1)
  ), 
  GST_CAPS_NEW (
    "volume_int_src",
    "audio/raw",
    "format",     GST_PROPS_STRING ("int"),
    "channels",   GST_PROPS_INT_RANGE (1, G_MAXINT),
    "rate",       GST_PROPS_INT_RANGE (1, G_MAXINT),
    "law",        GST_PROPS_INT (0),
    "endianness", GST_PROPS_INT (G_BYTE_ORDER),
    "width",      GST_PROPS_INT (16),
    "depth",      GST_PROPS_INT (16),
    "signed",     GST_PROPS_BOOLEAN (TRUE)
  )
);

static void		volume_class_init		(GstVolumeClass *klass);
static void		volume_init		(GstVolume *filter);

static void		volume_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		volume_get_property     (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void		volume_update_volume    (const GValue *value, gpointer data);
static void		volume_update_mute      (const GValue *value, gpointer data);

static gboolean		volume_parse_caps          (GstVolume *filter, GstCaps *caps);

static void		volume_chain_float         (GstPad *pad, GstBuffer *buf);
static void		volume_chain_int16         (GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

static GstBufferPool*
volume_get_bufferpool (GstPad *pad)
{
  GstVolume *filter;

  filter = GST_VOLUME (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (filter->srcpad);
}

static GstPadConnectReturn
volume_connect (GstPad *pad, GstCaps *caps)
{
  GstVolume *filter;
  GstPad *otherpad;
  
  filter = GST_VOLUME (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (GST_IS_VOLUME (filter), GST_PAD_CONNECT_REFUSED);
  otherpad = (pad == filter->srcpad ? filter->sinkpad : filter->srcpad);
  
  if (GST_CAPS_IS_FIXED (caps)) {
    if (!volume_parse_caps (filter, caps) || !gst_pad_try_set_caps (otherpad, caps))
      return GST_PAD_CONNECT_REFUSED;
    
    return GST_PAD_CONNECT_OK;
  }
  
  return GST_PAD_CONNECT_DELAYED;
}

static gboolean
volume_parse_caps (GstVolume *filter, GstCaps *caps)
{
  const gchar *format;
  
  g_return_val_if_fail(filter!=NULL,FALSE);
  g_return_val_if_fail(caps!=NULL,FALSE);
  
  gst_caps_get_string (caps, "format", &format);
  
  if (strcmp(format, "int")==0) {
    gst_pad_set_chain_function(filter->sinkpad,volume_chain_int16);
    return TRUE;
  }
  
  if (strcmp(format, "float")==0) {
    gst_pad_set_chain_function(filter->sinkpad,volume_chain_float);
    return TRUE;
  }

  return FALSE;
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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         FALSE,G_PARAM_READWRITE));
  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
    g_param_spec_float("volume","volume","volume",
                       0.0,4.0,1.0,G_PARAM_READWRITE));
  
  gobject_class->set_property = volume_set_property;
  gobject_class->get_property = volume_get_property;
}

static void
volume_init (GstVolume *filter)
{
  filter->sinkpad = gst_pad_new_from_template(volume_sink_factory (),"sink");
  gst_pad_set_connect_function(filter->sinkpad,volume_connect);
  gst_pad_set_bufferpool_function(filter->sinkpad,volume_get_bufferpool);
  filter->srcpad = gst_pad_new_from_template(volume_src_factory (),"src");
  gst_pad_set_connect_function(filter->srcpad,volume_connect);
  
  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);
  gst_pad_set_chain_function(filter->sinkpad,volume_chain_int16);
  filter->mute = FALSE;
  filter->volume_i = 8192;
  filter->volume_f = 1.0;
  filter->real_vol_i = 8192;
  filter->real_vol_f = 1.0;

  filter->dpman = gst_dpman_new ("volume_dpman", GST_ELEMENT(filter));
  gst_dpman_add_required_dparam_callback (
    filter->dpman, 
    g_param_spec_int("mute","Mute","Mute the audio",
                     0, 1, 0, G_PARAM_READWRITE),
    "int",
    volume_update_mute, 
    filter
  );
  gst_dpman_add_required_dparam_callback (
    filter->dpman, 
    g_param_spec_float("volume","Volume","Volume of the audio",
                       0.0, 4.0, 1.0, G_PARAM_READWRITE),
    "scalar",
    volume_update_volume, 
    filter
  );

}

static void
volume_chain_float (GstPad *pad, GstBuffer *buf)
{
  GstVolume *filter;
  GstBuffer *out_buf;
  gfloat *data;
  gint i, sample_countdown, num_samples;

  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  
  filter = GST_VOLUME(GST_OBJECT_PARENT (pad));
  g_return_if_fail(GST_IS_VOLUME(filter));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_READONLY)){
    out_buf = gst_buffer_copy (buf);
    gst_buffer_unref(buf);
  }
  else {
    out_buf = buf;
  }

  data = (gfloat *)GST_BUFFER_DATA(out_buf);
  num_samples = GST_BUFFER_SIZE(out_buf)/sizeof(gfloat);
  sample_countdown = GST_DPMAN_PREPROCESS(filter->dpman, num_samples, GST_BUFFER_TIMESTAMP(out_buf));
  i = 0;
    
  while(GST_DPMAN_PROCESS_COUNTDOWN(filter->dpman, sample_countdown, i)) {
    data[i++] *= filter->real_vol_f;
  }
  
  gst_pad_push(filter->srcpad,out_buf);
  
}

static void
volume_chain_int16 (GstPad *pad, GstBuffer *buf)
{
  GstVolume *filter;
  GstBuffer *out_buf;
  gint16 *data;
  gint i, sample_countdown, num_samples;

  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  
  filter = GST_VOLUME(GST_OBJECT_PARENT (pad));
  g_return_if_fail(GST_IS_VOLUME(filter));

  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_READONLY)){
    out_buf = gst_buffer_copy (buf);
    gst_buffer_unref(buf);
  }
  else {
    out_buf = buf;
  }

  data = (gint16 *)GST_BUFFER_DATA(out_buf);
  num_samples = GST_BUFFER_SIZE(out_buf)/sizeof(gint16);
  sample_countdown = GST_DPMAN_PREPROCESS(filter->dpman, num_samples, GST_BUFFER_TIMESTAMP(out_buf));
  i = 0;
    
  while(GST_DPMAN_PROCESS_COUNTDOWN(filter->dpman, sample_countdown, i)) {
    data[i] = (gint16)(filter->real_vol_i * (gint)data[i] / 8192);
    i++;
  }
  
  gst_pad_push(filter->srcpad,out_buf);   
  
}

static void
volume_update_mute(const GValue *value, gpointer data)
{
  GstVolume *filter = (GstVolume*)data;
  g_return_if_fail(GST_IS_VOLUME(filter));

  if (G_VALUE_HOLDS_BOOLEAN(value)){
    filter->mute = g_value_get_boolean(value);
  }
  else if (G_VALUE_HOLDS_INT(value)){
    filter->mute = (g_value_get_int(value) == 1);
  }
  
  if (filter->mute){
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  }
  else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_update_volume(const GValue *value, gpointer data)
{
  GstVolume *filter = (GstVolume*)data;
  g_return_if_fail(GST_IS_VOLUME(filter));

  filter->volume_f       = g_value_get_float (value);
  filter->volume_i       = filter->volume_f*8192;
  if (filter->mute){
    filter->real_vol_f = 0.0;
    filter->real_vol_i = 0;
  }
  else {
    filter->real_vol_f = filter->volume_f;
    filter->real_vol_i = filter->volume_i;
  }
}

static void
volume_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVolume *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VOLUME(object));
  filter = GST_VOLUME(object);

  switch (prop_id) 
  {
  case ARG_MUTE:
    gst_dpman_bypass_dparam(filter->dpman, "mute");
    volume_update_mute(value, filter);
    break;
  case ARG_VOLUME:
    gst_dpman_bypass_dparam(filter->dpman, "volume");
    volume_update_volume(value, filter);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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
  case ARG_MUTE:
    g_value_set_boolean (value, filter->mute);
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

  factory = gst_element_factory_new("volume",GST_TYPE_VOLUME,
                                   &volume_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_element_factory_add_pad_template (factory, volume_src_factory ());
  gst_element_factory_add_pad_template (factory, volume_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  /* load dparam support library */
  if (!gst_library_load ("gstcontrol"))
  {
    gst_info ("volume: could not load support library: 'gstcontrol'\n");
    return FALSE;
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "volume",
  plugin_init
};
