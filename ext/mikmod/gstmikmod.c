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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmikmod.h"

#include <gst/audio/audio.h>
#include <stdlib.h>

/* elementfactory information */
GstElementDetails mikmod_details = {
  "MikMod",
  "Codec/Decoder/Audio",
  "Module decoder based on libmikmod",
  "Jeremy SIMON <jsimon13@yahoo.fr>",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SONGNAME,
  ARG_MODTYPE,
  ARG_MUSICVOLUME,
  ARG_PANSEP,
  ARG_REVERB,
  ARG_SNDFXVOLUME,
  ARG_VOLUME,
  ARG_INTERP,
  ARG_REVERSE,
  ARG_SURROUND,
  ARG_HQMIXER,
  ARG_SOFT_MUSIC,
  ARG_SOFT_SNDFX
};


static GstPadTemplate*
mikmod_src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "mikmod_src",
        "audio/x-raw-int",
          GST_AUDIO_INT_PAD_TEMPLATE_PROPS
      ), NULL);
  }
  return template;
}


static GstPadTemplate*
mikmod_sink_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "mikmod_sink",
        "audio/x-mod",
        NULL),NULL        
      );
  }
  return template;
}

static void             gst_mikmod_base_init            (gpointer g_class);
static void		gst_mikmod_class_init		(GstMikModClass *klass);
static void		gst_mikmod_init			(GstMikMod *filter);
static void		gst_mikmod_set_property 	(GObject *object, guint id, const GValue *value, GParamSpec *pspec );
static void		gst_mikmod_get_property		(GObject *object, guint id, GValue *value, GParamSpec *pspec );
static GstPadLinkReturn	gst_mikmod_srclink		(GstPad *pad, GstCaps *caps);
static void             gst_mikmod_loop                 (GstElement *element);
static gboolean		gst_mikmod_setup 		(GstMikMod *mikmod);
static GstElementStateReturn  gst_mikmod_change_state 	(GstElement *element);



static GstElementClass *parent_class = NULL;

GType
gst_mikmod_get_type(void) {
  static GType mikmod_type = 0;

  if (!mikmod_type) {
    static const GTypeInfo mikmod_info = {
      sizeof(GstMikModClass),
      gst_mikmod_base_init,
      NULL,
      (GClassInitFunc)gst_mikmod_class_init,
      NULL,
      NULL,
      sizeof(GstMikMod),
      0,
      (GInstanceInitFunc)gst_mikmod_init,
    };
    mikmod_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMikmod", &mikmod_info, 0);
  }
  return mikmod_type;
}

static void
gst_mikmod_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, mikmod_src_factory ());
  gst_element_class_add_pad_template (element_class, mikmod_sink_factory ());
  gst_element_class_set_details (element_class, &mikmod_details);
}

static void
gst_mikmod_class_init (GstMikModClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SONGNAME,
    g_param_spec_string("songname","songname","songname",
                        NULL, G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MODTYPE,
    g_param_spec_string("modtype", "modtype", "modtype",
    			NULL, G_PARAM_READABLE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUSICVOLUME,
    g_param_spec_int("musicvolume", "musivolume", "musicvolume",
    			0, 128, 128, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_PANSEP,
    g_param_spec_int("pansep", "pansep", "pansep",
    			0, 128, 128, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB,
    g_param_spec_int("reverb", "reverb", "reverb",
    			0, 15, 0, G_PARAM_READWRITE ));    			
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SNDFXVOLUME,
    g_param_spec_int("sndfxvolume", "sndfxvolume", "sndfxvolume",
    			0, 128, 128, G_PARAM_READWRITE ));    			
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_VOLUME,
    g_param_spec_int("volume", "volume", "volume",
    			0, 128, 96, G_PARAM_READWRITE ));
			    			  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_INTERP,
    g_param_spec_boolean("interp", "interp", "interp",
    		       FALSE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERSE,
    g_param_spec_boolean("reverse", "reverse", "reverse",
    		       FALSE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND,
    g_param_spec_boolean("surround", "surround", "surround",
    		       TRUE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HQMIXER,
    g_param_spec_boolean("hqmixer", "hqmixer", "hqmixer",
    		       FALSE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SOFT_MUSIC,
    g_param_spec_boolean("soft_music", "soft_music", "soft_music",
    		       TRUE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SOFT_SNDFX,
    g_param_spec_boolean("soft_sndfx", "soft_sndfx", "soft_sndfx",
    		       TRUE, G_PARAM_READWRITE ));

  
  gobject_class->set_property = gst_mikmod_set_property;
  gobject_class->get_property = gst_mikmod_get_property;

  gstelement_class->change_state = gst_mikmod_change_state;
}


static void
gst_mikmod_init (GstMikMod *filter)
{  
  filter->sinkpad = gst_pad_new_from_template(mikmod_sink_factory (),"sink");
  filter->srcpad = gst_pad_new_from_template(mikmod_src_factory (),"src");

  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);
  gst_pad_set_link_function (filter->srcpad, gst_mikmod_srclink);
  
  gst_element_set_loop_function (GST_ELEMENT (filter), gst_mikmod_loop);
  
  filter->Buffer = NULL;

  filter->stereo      = TRUE;
  filter->surround    = TRUE;
  filter->_16bit      = TRUE;
  filter->soft_music  = TRUE;
  filter->soft_sndfx  = TRUE;
  filter->mixfreq     = 44100;
  filter->reverb      = 0;
  filter->pansep      = 128;
  filter->musicvolume = 128;
  filter->volume      = 96;
  filter->sndfxvolume = 128;
  filter->songname    = NULL;
  filter->modtype     = NULL;
}


static GstPadLinkReturn
gst_mikmod_negotiate (GstMikMod *mikmod)
{
  gint width, sign;

  if ( mikmod->_16bit ) {
    width = 16;
    sign = TRUE;
  } else {
    width = 8;
    sign = FALSE;
  }

  return gst_pad_try_set_caps (mikmod->srcpad, 
		          GST_CAPS_NEW (
			    "mikmod_src",
			    "audio/x-raw-int",
			      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
			      "signed",      GST_PROPS_BOOLEAN (sign),
			      "width",       GST_PROPS_INT (width),
			      "depth",       GST_PROPS_INT (width),
			      "rate",        GST_PROPS_INT (mikmod->mixfreq),
			      "channels",    GST_PROPS_INT (mikmod->stereo ? 2 : 1)));
}


static GstPadLinkReturn
gst_mikmod_srclink (GstPad *pad, GstCaps *caps)
{
  GstMikMod *filter; 

  filter = GST_MIKMOD (gst_pad_get_parent (pad));

  if (gst_caps_has_property_typed (caps, "depth", GST_PROPS_INT_TYPE)) {
    gint depth;
    gst_caps_get_int (caps, "depth", &depth);
    filter->_16bit = (depth == 16);
  }
  if (gst_caps_has_property_typed (caps, "channels", GST_PROPS_INT_TYPE)) {
    gint channels;
    gst_caps_get_int (caps, "channels", &channels);
    filter->stereo = (channels == 2);
  }
  if (gst_caps_has_property_typed (caps, "rate", GST_PROPS_INT_TYPE)) {
    gst_caps_get_int (caps, "rate", &filter->mixfreq);
  }

  return gst_mikmod_negotiate(filter);
}


static void
gst_mikmod_loop (GstElement *element)
{
  GstMikMod *mikmod;
  GstBuffer *buffer_in;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MIKMOD (element));
	
  mikmod = GST_MIKMOD (element);
  srcpad = mikmod->srcpad;
  mikmod->Buffer = NULL;
  	
  while ((buffer_in = GST_BUFFER (gst_pad_pull( mikmod->sinkpad )))) {
    if ( GST_IS_EVENT (buffer_in) ) {
      GstEvent *event = GST_EVENT (buffer_in);
		
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) 
         break;
    }
    else		
    {
      if ( mikmod->Buffer ) {	 
        mikmod->Buffer = gst_buffer_merge( mikmod->Buffer, buffer_in );
        gst_buffer_unref( buffer_in );	 	  
      }
      else
        mikmod->Buffer = buffer_in;
    }
  }  
  
  if (!GST_PAD_CAPS (mikmod->srcpad)) {
    if (gst_mikmod_negotiate (mikmod) <= 0) {
      gst_element_error (GST_ELEMENT (mikmod),
			 "Failed to negotiate with next element in mikmod");
      return;
    }
  }
  gst_mikmod_setup( mikmod );
  
  MikMod_RegisterDriver(&drv_gst);
  MikMod_RegisterAllLoaders();

  MikMod_Init("");
  reader = GST_READER_new( mikmod );
  module = Player_LoadGeneric ( reader, 64, 0 );
  
  gst_buffer_unref (mikmod->Buffer);
  
  if ( ! Player_Active() )
    Player_Start(module);

  do {
    if ( Player_Active() ) {

      timestamp = ( module->sngtime / 1024.0 ) * GST_SECOND;
      drv_gst.Update();
      gst_element_yield (element);
    }
    else {
      gst_element_set_eos (GST_ELEMENT (mikmod));
      gst_pad_push (mikmod->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    }

  } 
  while ( 1 );
}


static gboolean
gst_mikmod_setup (GstMikMod *mikmod)
{
  md_musicvolume = mikmod->musicvolume;
  md_pansep = mikmod->pansep;
  md_reverb = mikmod->reverb;
  md_sndfxvolume = mikmod->sndfxvolume;
  md_volume = mikmod->volume;
  md_mixfreq = mikmod->mixfreq;

  md_mode = 0;

  if ( mikmod->interp )
    md_mode = md_mode | DMODE_INTERP;

  if ( mikmod->reverse )
    md_mode = md_mode | DMODE_REVERSE;

  if ( mikmod->surround )
    md_mode = md_mode | DMODE_SURROUND;

  if ( mikmod->_16bit )
    md_mode = md_mode | DMODE_16BITS;

  if ( mikmod->hqmixer )
    md_mode = md_mode | DMODE_HQMIXER;

  if ( mikmod->soft_music )
    md_mode = md_mode | DMODE_SOFT_MUSIC;

  if ( mikmod->soft_sndfx )
    md_mode = md_mode | DMODE_SOFT_SNDFX;

  if ( mikmod->stereo )
    md_mode = md_mode | DMODE_STEREO;

  return TRUE;
}


static GstElementStateReturn
gst_mikmod_change_state (GstElement *element)
{
GstMikMod *mikmod;

  g_return_val_if_fail (GST_IS_MIKMOD (element), GST_STATE_FAILURE);

  mikmod = GST_MIKMOD (element);

  GST_DEBUG ("state pending %d", GST_STATE_PENDING (element));

  if (GST_STATE_PENDING (element) == GST_STATE_READY) 
  {
     gst_mikmod_setup(mikmod);
	  
	 if ( Player_Active() )
	 {
		Player_TogglePause();
		Player_SetPosition( 0 );
	 }
	
  }
  
  if (GST_STATE_PENDING (element) == GST_STATE_PLAYING) 
  {	 
	 if ( Player_Active() && Player_Paused() )	 
		Player_TogglePause();
	 else
	   if ( ! Player_Active() )
	     Player_Start(module);
	 
  }
    	
  if (GST_STATE_PENDING (element) == GST_STATE_PAUSED) 
    if ( Player_Active() && ! Player_Paused() )
       Player_TogglePause();

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) 
	  MikMod_Exit();    
  

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}



static void
gst_mikmod_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec )
{
  GstMikMod *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MIKMOD(object));
  filter = GST_MIKMOD(object);

  switch (id) {
    case ARG_SONGNAME:
      g_free (filter->songname);
      filter->songname = g_strdup (g_value_get_string (value));
      break;
    case ARG_MODTYPE:
      g_free (filter->modtype);
      filter->modtype = g_strdup (g_value_get_string (value));
      break;
    case ARG_MUSICVOLUME:
      filter->musicvolume = g_value_get_int (value);
      break;
    case ARG_PANSEP:
      filter->pansep = g_value_get_int (value);
      break;
    case ARG_REVERB:
      filter->reverb = g_value_get_int (value);
      break;
    case ARG_SNDFXVOLUME:
      filter->sndfxvolume = g_value_get_int (value);
      break;
    case ARG_VOLUME:
      filter->volume = g_value_get_int (value);
      break;
    case ARG_INTERP:
      filter->interp = g_value_get_boolean (value);
      break;
    case ARG_REVERSE:
      filter->reverse = g_value_get_boolean (value);
      break;
    case ARG_SURROUND:
      filter->surround = g_value_get_boolean (value);
      break;
    case ARG_HQMIXER:
      filter->hqmixer = g_value_get_boolean (value);
      break;
    case ARG_SOFT_MUSIC:
      filter->soft_music = g_value_get_boolean (value);
      break;
    case ARG_SOFT_SNDFX:
      filter->soft_sndfx = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_mikmod_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec )
{
  GstMikMod *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MIKMOD(object));
  filter = GST_MIKMOD(object);

  switch (id) {
    case ARG_MUSICVOLUME:
      g_value_set_int (value, filter->musicvolume);
      break;
    case ARG_PANSEP:
      g_value_set_int (value, filter->pansep);
      break;
    case ARG_REVERB:
      g_value_set_int (value, filter->reverb);
      break;
    case ARG_SNDFXVOLUME:
      g_value_set_int (value, filter->sndfxvolume);
      break;
    case ARG_VOLUME:
      g_value_set_int (value, filter->volume);
      break;
    case ARG_INTERP:
      g_value_set_boolean (value, filter->interp);
      break;
    case ARG_REVERSE:
      g_value_set_boolean (value, filter->reverse);
      break;
    case ARG_SURROUND:
      g_value_set_boolean (value, filter->surround);
      break;
    case ARG_HQMIXER:
      g_value_set_boolean (value, filter->hqmixer);
      break;
    case ARG_SOFT_MUSIC:
      g_value_set_boolean (value, filter->soft_music);
      break;
    case ARG_SOFT_SNDFX:
      g_value_set_boolean (value, filter->soft_sndfx);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "mikmod", GST_RANK_SECONDARY, GST_TYPE_MIKMOD))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mikmod",
  "Mikmod plugin library",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN)
