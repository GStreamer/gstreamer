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

#include "gstmikmod.h"
#include "mikmod_types.h"

#include <gst/audio/audio.h>
#include <stdlib.h>

GstElementDetails mikmod_details = {
  "MikMod",
  "Codec/Audio/Decoder",
  "Module decoder based on libmikmod",
  VERSION,
  "Jeremy SIMON <jsimon13@yahoo.fr>",
  "(C) 2001",
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
  ARG_MIXFREQ,
  ARG_INTERP,
  ARG_REVERSE,
  ARG_SURROUND,
  ARG_16BIT,
  ARG_HQMIXER,
  ARG_SOFT_MUSIC,
  ARG_SOFT_SNDFX,
  ARG_STEREO
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
        "audio/raw",
	gst_props_new (
  	  "format",   		GST_PROPS_STRING ("int"),
    	    "law",   		GST_PROPS_INT (0),
    	    "endianness", 	GST_PROPS_INT (G_BYTE_ORDER),
    	    "signed", 		GST_PROPS_BOOLEAN (TRUE),
    	    "width", 		GST_PROPS_INT (16),
    	    "depth",    	GST_PROPS_INT (16),
    	    "rate",     	GST_PROPS_INT_RANGE (8000, 48000),
    	    "channels", 	GST_PROPS_INT_RANGE (1, 2),
	    NULL)),NULL);
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
        "audio/mod",
        NULL),NULL        
      );
  }
  return template;
}

static GstCaps* 
mikmod_type_find (GstBuffer *buf, gpointer private) 
{  
  if ( MOD_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Mod_669_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Amf_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Dsm_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Fam_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Gdm_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Imf_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( It_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( M15_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  /* FIXME
  if ( Med_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
    */
  
  if ( Mtm_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Okt_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( S3m_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  if ( Xm_CheckType( buf ) )
    return gst_caps_new ("mikmod_type_find", "audio/mod", NULL);
  
  return NULL;
}

static GstTypeDefinition mikmoddefinition = {
  "mikmod_audio/mod", "audio/mod", ".mod .sam .med .s3m .it .xm .stm .mtm .669 .ult .far .amf  .dsm .imf .gdm .stx .okt", mikmod_type_find 
};

static void		gst_mikmod_class_init		(GstMikModClass *klass);
static void		gst_mikmod_init			(GstMikMod *filter);
static void		gst_mikmod_set_property 	(GObject *object, guint id, const GValue *value, GParamSpec *pspec );
static void		gst_mikmod_get_property		(GObject *object, guint id, GValue *value, GParamSpec *pspec );
static void             gst_mikmod_loop                 (GstElement *element);
static gboolean		gst_mikmod_setup 		(GstMikMod *mikmod);
static GstElementStateReturn  gst_mikmod_change_state 	(GstElement *element);



static GstElementClass *parent_class = NULL;

#define GST_TYPE_MIKMOD_MIXFREQ (gst_mikmod_mixfreq_get_type())

static GType 
gst_mikmod_mixfreq_get_type (void)
{
  static GType mikmod_mixfreq_type = 0;
  static GEnumValue mikmod_mixfreq[] = {
    { 0, "8000",  "8000 Hz" },
    { 1, "11025", "11025 Hz" },
    { 2, "22100", "22100 Hz" },
    { 3, "44100", "44100 Hz" },
    { 0, NULL, NULL },
  };
  if (! mikmod_mixfreq_type ) {
    mikmod_mixfreq_type = g_enum_register_static ("GstMikmodmixfreq", mikmod_mixfreq);
  }
  return mikmod_mixfreq_type;
}

GType
gst_mikmod_get_type(void) {
  static GType mikmod_type = 0;

  if (!mikmod_type) {
    static const GTypeInfo mikmod_info = {
      sizeof(GstMikModClass),
      NULL,
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
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MIXFREQ,
    g_param_spec_enum("mixfreq", "mixfreq", "mixfreq",
    		       GST_TYPE_MIKMOD_MIXFREQ, 3,G_PARAM_READWRITE )); 			    			  
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_INTERP,
    g_param_spec_boolean("interp", "interp", "interp",
    		       FALSE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERSE,
    g_param_spec_boolean("reverse", "reverse", "reverse",
    		       FALSE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND,
    g_param_spec_boolean("surround", "surround", "surround",
    		       TRUE, G_PARAM_READWRITE ));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_16BIT,
    g_param_spec_boolean("use16bit", "use16bit", "use16bit",
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
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_STEREO,
    g_param_spec_boolean("stereo", "stereo", "stereo",
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


static void
gst_mikmod_loop (GstElement *element)
{
  GstMikMod *mikmod;
  GstBuffer *buffer_in;
  gint mode16bits;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MIKMOD (element));
	
  mikmod = GST_MIKMOD (element);
  srcpad = mikmod->srcpad;
  mikmod->Buffer = NULL;
  	
  while ((buffer_in = gst_pad_pull( mikmod->sinkpad ))) {
    if ( GST_IS_EVENT (buffer_in) ) {
      GstEvent *event = GST_EVENT (buffer_in);
		
      if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) 
         break;
    }
		
    if ( mikmod->Buffer ) {	 
      mikmod->Buffer = gst_buffer_append( mikmod->Buffer, buffer_in );
      gst_buffer_unref( buffer_in );	 	  
    }
    else
      mikmod->Buffer = buffer_in;
  }  
  
  if ( mikmod->_16bit )
    mode16bits = 16;
  else
    mode16bits = 8;

  MikMod_RegisterDriver(&drv_gst);
  MikMod_RegisterAllLoaders();

  MikMod_Init("");
  reader = GST_READER_new( mikmod );
  module = Player_LoadGeneric ( reader, 64, 0 );
  
  gst_buffer_unref (mikmod->Buffer);
  
  if ( ! Player_Active() )
    Player_Start(module);

  gst_pad_try_set_caps (mikmod->srcpad, 
		          GST_CAPS_NEW (
			    "mikmod_src",
			    "audio/raw",
			      "format",      GST_PROPS_STRING ("int"),
			      "law",         GST_PROPS_INT (0),
			      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
			      "signed",      GST_PROPS_BOOLEAN (TRUE),
			      "width",       GST_PROPS_INT (mode16bits),
			      "depth",       GST_PROPS_INT (mode16bits),
			      "rate",        GST_PROPS_INT (mikmod->mixfreq),
			      "channels",    GST_PROPS_INT (2)));
				    
  do {
    if ( Player_Active() ) {
      drv_gst.Update();

      gst_element_yield (element);
    }
    else {
      gst_element_set_eos (GST_ELEMENT (mikmod));
      gst_pad_push (mikmod->srcpad, GST_BUFFER (gst_event_new (GST_EVENT_EOS)));
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

  GST_DEBUG (0,"state pending %d", GST_STATE_PENDING (element));

  /* if going down into NULL state, close the file if it's open */
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
  

  /* if we haven't failed already, give the parent class a chance to ;-) */
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
    case ARG_MIXFREQ:
      filter->mixfreq = g_value_get_enum (value);
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
    case ARG_16BIT:
      filter->_16bit = g_value_get_boolean (value);
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
    case ARG_STEREO:
      filter->stereo = g_value_get_boolean (value);
      break;
    default:
/*      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); */
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
    case ARG_MIXFREQ:
      g_value_set_enum (value, filter->mixfreq);
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
    case ARG_16BIT:
      g_value_set_boolean (value, filter->_16bit);
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
    case ARG_STEREO:
      g_value_set_boolean (value, filter->stereo);
      break;
    default:
/*      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); */
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory    *type;  
	
  factory = gst_element_factory_new("mikmod",GST_TYPE_MIKMOD,
                                   &mikmod_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);
 
  gst_element_factory_add_pad_template (factory, mikmod_src_factory ());
  gst_element_factory_add_pad_template (factory, mikmod_sink_factory ());

  type = gst_type_factory_new (&mikmoddefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));	
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));	

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mikmod",
  plugin_init
};
