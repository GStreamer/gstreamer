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

/*
 * Portions derived from maswavplay.c (distributed under the X11
 * license):
 *
 * Copyright (c) 2001-2003 Shiman Associates Inc. All Rights Reserved.
 * Copyright (c) 2000, 2001 by Shiman Associates Inc. and Sun
 * Microsystems, Inc. All Rights Reserved.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "massink.h"

#define  BUFFER_SIZE           640


/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_DEPTH,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_HOST,
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "massink_sink8",				/* the name of the caps */
    "audio/x-raw-int",
    NULL
  )
);

static void                     gst_massink_base_init           (gpointer g_class);
static void			gst_massink_class_init		(GstMassinkClass *klass);
static void			gst_massink_init		(GstMassink *massink);
static void 			gst_massink_set_clock 		(GstElement *element, GstClock *clock);
static gboolean			gst_massink_open_audio		(GstMassink *sink);
//static void			gst_massink_close_audio		(GstMassink *sink);
static GstElementStateReturn	gst_massink_change_state	(GstElement *element);
static gboolean			gst_massink_sync_parms		(GstMassink *massink);
static GstPadLinkReturn	gst_massink_sinkconnect		(GstPad *pad, GstCaps *caps);

static void			gst_massink_chain		(GstPad *pad, GstData *_data);

static void			gst_massink_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_massink_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

#define GST_TYPE_MASSINK_DEPTHS (gst_massink_depths_get_type())
static GType
gst_massink_depths_get_type (void)
{
  static GType massink_depths_type = 0;
  static GEnumValue massink_depths[] = {
    {8, "8", "8 Bits"},
    {16, "16", "16 Bits"},
    {0, NULL, NULL},
  };
  if (!massink_depths_type) {
    massink_depths_type = g_enum_register_static("GstMassinkDepths", massink_depths);
  }
  return massink_depths_type;
}

static GstElementClass *parent_class = NULL;
/*static guint gst_massink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_massink_get_type (void)
{
  static GType massink_type = 0;

  if (!massink_type) {
    static const GTypeInfo massink_info = {
      sizeof(GstMassinkClass),
      gst_massink_base_init,
      NULL,
      (GClassInitFunc)gst_massink_class_init,
      NULL,
      NULL,
      sizeof(GstMassink),
      0,
      (GInstanceInitFunc)gst_massink_init,
    };
    massink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMassink", &massink_info, 0);
  }
  return massink_type;
}

static void
gst_massink_base_init (gpointer g_class)
{
  static GstElementDetails massink_details = GST_ELEMENT_DETAILS (
    "Esound audio sink",
    "Sink/Audio",
    "Plays audio to a MAS server",
    "Zeeshan Ali <zak147@yahoo.com>"
  );
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_class_set_details (element_class, &massink_details);
}

static void
gst_massink_class_init (GstMassinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEPTH,
    g_param_spec_enum("depth","depth","depth",
                      GST_TYPE_MASSINK_DEPTHS,16,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RATE,
    g_param_spec_int("frequency","frequency","frequency",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_massink_set_property;
  gobject_class->get_property = gst_massink_get_property;

  gstelement_class->change_state = gst_massink_change_state;
  gstelement_class->set_clock = gst_massink_set_clock;
}

static void
gst_massink_set_clock (GstElement *element, GstClock *clock)
{
  GstMassink *massink;
	      
  massink = GST_MASSINK (element);

  massink->clock = clock;
}

static void
gst_massink_init(GstMassink *massink)
{
  massink->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(massink), massink->sinkpad);
  gst_pad_set_chain_function(massink->sinkpad, GST_DEBUG_FUNCPTR(gst_massink_chain));
  gst_pad_set_link_function(massink->sinkpad, gst_massink_sinkconnect);

  massink->mute = FALSE;
  massink->format = 16;
  massink->depth = 16;
  massink->channels = 2;
  massink->frequency = 44100;
  massink->host = NULL;
}

static gboolean
gst_massink_sync_parms (GstMassink *massink)
{
  g_return_val_if_fail (massink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_MASSINK (massink), FALSE);

  //gst_massink_close_audio (massink);
  //return gst_massink_open_audio (massink);
  return 1;
}

static GstPadLinkReturn
gst_massink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstMassink *massink;

  massink = GST_MASSINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;
  
  if (gst_massink_sync_parms (massink))
    return GST_PAD_LINK_OK;

  return GST_PAD_LINK_REFUSED;
}

static void
gst_massink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  gint32 err;
    
  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  GstMassink *massink = GST_MASSINK (gst_pad_get_parent (pad));

  if (massink->clock) {
    GstClockID id = gst_clock_new_single_shot_id (massink->clock, GST_BUFFER_TIMESTAMP (buf));

    GST_DEBUG ("massink: clock wait: %llu\n", GST_BUFFER_TIMESTAMP (buf));
    gst_element_clock_wait (GST_ELEMENT (massink), id, NULL);
    gst_clock_id_free (id);
  }

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!massink->mute) {
      GST_DEBUG ("massink: data=%p size=%d", GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      if (GST_BUFFER_SIZE (buf) > BUFFER_SIZE) {
  	gst_buffer_unref (buf);
	return;
      }

      massink->data->length = GST_BUFFER_SIZE (buf);
	
      memcpy (massink->data->segment, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf)); 
	  
      err = mas_send (massink->audio_channel, massink->data);
	
      if (err < 0) {
    	 g_print ("error sending data to MAS server\n");
  	 gst_buffer_unref (buf);
	 return;
      }
      
      /* FIXME: Please correct the Timestamping if its wrong */
      massink->data->header.media_timestamp += massink->data->length / 4;
      massink->data->header.sequence++;
    }
  }
  
  gst_buffer_unref (buf);
}

static void
gst_massink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMassink *massink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MASSINK(object));
  massink = GST_MASSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      massink->mute = g_value_get_boolean (value);
      break;
    case ARG_DEPTH:
      massink->depth = g_value_get_enum (value);
      gst_massink_sync_parms (massink);
      break;
    case ARG_CHANNELS:
      massink->channels = g_value_get_enum (value);
      gst_massink_sync_parms (massink);
      break;
    case ARG_RATE:
      massink->frequency = g_value_get_int (value);
      gst_massink_sync_parms (massink);
      break;
    case ARG_HOST:
      if (massink->host != NULL) g_free(massink->host);
      if (g_value_get_string (value) == NULL)
	  massink->host = NULL;
      else
	  massink->host = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_massink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMassink *massink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MASSINK(object));
  massink = GST_MASSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, massink->mute);
      break;
    case ARG_DEPTH:
      g_value_set_enum (value, massink->depth);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, massink->channels);
      break;
    case ARG_RATE:
      g_value_set_int (value, massink->frequency);
      break;
    case ARG_HOST:
      g_value_set_string (value, massink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "massink", GST_RANK_NONE,
        GST_TYPE_MASSINK)){
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "massink",
  "uses MAS for audio output",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
);

static gboolean
gst_massink_open_audio (GstMassink *massink)
{
  gint32     err;
  char       *ratestring = g_malloc (16);
  char       *bps = g_malloc (8);
    
  struct mas_data_characteristic* dc;
  
  g_print ("Connecting to MAS.\n");
  masc_log_verbosity (MAS_VERBLVL_DEBUG);
  err = mas_init();
    
  if (err < 0) {
     GST_DEBUG ("connection with local MAS server failed.");
     exit (1);
  }    
    
  GST_DEBUG ("Establishing audio output channel.");
  mas_make_data_channel ("Gstreamer", &massink->audio_channel, &massink->audio_source, &massink->audio_sink);
  mas_asm_get_port_by_name (0, "default_mix_sink", &massink->mix_sink);

  GST_DEBUG ("Instantiating endian device.");
  err = mas_asm_instantiate_device ("endian", 0, 0, &massink->endian);
    
  if (err < 0) {
     GST_DEBUG ("Failed to instantiate endian converter device");
     exit(1);
  }
  
  mas_asm_get_port_by_name (massink->endian, "endian_sink", &massink->endian_sink);
  mas_asm_get_port_by_name (massink->endian, "endian_source", &massink->endian_source);

  sprintf (bps, "%u", massink->depth);
  sprintf (ratestring, "%u", massink->frequency);

  GST_DEBUG ("Connecting net -> endian.");
  masc_make_dc (&dc, 6);
    
  /* wav weirdness: 8 bit data is unsigned, >8 data is signed. */
  masc_append_dc_key_value (dc, "format", (massink->depth==8) ? "ulinear":"linear");
  masc_append_dc_key_value (dc, "resolution", bps);
  masc_append_dc_key_value (dc, "sampling rate", ratestring);
  masc_append_dc_key_value (dc, "channels", "2");
  masc_append_dc_key_value (dc, "endian", "little");
  err = mas_asm_connect_source_sink (massink->audio_source, massink->endian_sink, dc);
  
  if ( err < 0 ) {
     GST_DEBUG ("Failed to connect net audio output to endian");
     return -1;
  }
    
  /* The next device is 'if needed' only. After the following if()
     statement, open_source will contain the current unconnected
     source in the path (will be either endian_source or
     squant_source in this case)
  */
    
  massink->open_source = massink->endian_source;
    
  if (massink->depth != 16) {
     GST_DEBUG ("Sample resolution is not 16 bit/sample, instantiating squant device.");
     err = mas_asm_instantiate_device ("squant", 0, 0, &massink->squant);
     if (err < 0) {
	 GST_DEBUG ("Failed to instantiate squant device");
         return -1;
     }
	
     mas_asm_get_port_by_name (massink->squant, "squant_sink", &massink->squant_sink);
     mas_asm_get_port_by_name (massink->squant, "squant_source", &massink->squant_source);

     GST_DEBUG ("Connecting endian -> squant.");

     masc_make_dc (&dc, 6);
     masc_append_dc_key_value (dc,"format",(massink->depth==8) ? "ulinear":"linear");
     masc_append_dc_key_value (dc, "resolution", bps);
     masc_append_dc_key_value (dc, "sampling rate", ratestring);
     masc_append_dc_key_value (dc, "channels", "2");
     masc_append_dc_key_value (dc, "endian", "host");
     err = mas_asm_connect_source_sink (massink->endian_source, massink->squant_sink, dc);
        
     if (err < 0) {
        GST_DEBUG ("Failed to connect endian output to squant");
        return -1;
    }

    /* sneaky: the squant device is optional -> pretend it isn't there */
    massink->open_source = massink->squant_source;
 }

    
   /* Another 'if necessary' device, as above */
   if (massink->frequency != 44100) {
     GST_DEBUG ("Sample rate is not 44100, instantiating srate device.");
     err = mas_asm_instantiate_device ("srate", 0, 0, &massink->srate);
	
     if (err < 0) {
	GST_DEBUG ("Failed to instantiate srate device");
        return -1;
     }
	
     mas_asm_get_port_by_name (massink->srate, "sink", &massink->srate_sink);
     mas_asm_get_port_by_name (massink->srate, "source", &massink->srate_source);
	
     GST_DEBUG ("Connecting to srate.");
     masc_make_dc (&dc, 6);
     masc_append_dc_key_value (dc, "format", "linear");
     masc_append_dc_key_value (dc, "resolution", "16");
     masc_append_dc_key_value (dc, "sampling rate", ratestring);
     masc_append_dc_key_value (dc, "channels", "2");
     masc_append_dc_key_value (dc, "endian", "host");
	
     err = mas_asm_connect_source_sink (massink->open_source, massink->srate_sink, dc);
	
     if ( err < 0 ) {
	GST_DEBUG ("Failed to connect to srate");
        return -1;
     }

        
     massink->open_source = massink->srate_source;        
  }

  GST_DEBUG ("Connecting to mix.");
  masc_make_dc(&dc, 6);
  masc_append_dc_key_value (dc, "format", "linear");
  masc_append_dc_key_value (dc, "resolution", "16");
  masc_append_dc_key_value (dc, "sampling rate", "44100");
  masc_append_dc_key_value (dc, "channels", "2");
  masc_append_dc_key_value (dc, "endian", "host");
    
  err = mas_asm_connect_source_sink (massink->open_source, massink->mix_sink, dc);
    
  if ( err < 0 ) {
    GST_DEBUG ("Failed to connect to mixer");
    return -1;
  }
  
  GST_FLAG_SET (massink, GST_MASSINK_OPEN);

  masc_make_mas_data (&massink->data, BUFFER_SIZE);
      
  massink->data->header.type = 10;
  
  massink->data->header.media_timestamp = 0;
  massink->data->header.sequence = 0;
      
  return TRUE;
}

/*static void
gst_massink_close_audio (GstMassink *massink)
{
  mas_free_device(massink->endian);
  mas_free_device(massink->srate);
  mas_free_device(massink->squant);
  
  mas_free_port(massink->mix_sink);
  mas_free_port(massink->srate_source);
  mas_free_port(massink->srate_sink);
  mas_free_port(massink->audio_source);
  mas_free_port(massink->audio_sink);
  mas_free_port(massink->endian_source);
  mas_free_port(massink->endian_sink);
  mas_free_port(massink->squant_source);
  mas_free_port(massink->squant_sink);
  mas_free_port(massink->open_source);

  mas_free_channel (massink->audio_channel);
  masc_destroy_mas_data (massink->data);

  g_free (ratestring);
  g_free (bps);
  
  GST_FLAG_UNSET (massink, GST_MASSINK_OPEN);

  GST_DEBUG ("massink: closed sound channel");
}*/

static GstElementStateReturn
gst_massink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_MASSINK (element), FALSE);

  /* if going down into NULL state, close the fd if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    //if (GST_FLAG_IS_SET (element, GST_MASSINK_OPEN))
      //gst_massink_close_audio (GST_MASSINK (element));
    /* otherwise (READY or higher) we need to open the fd */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_MASSINK_OPEN)) {
      if (!gst_massink_open_audio (GST_MASSINK (element)))
	return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}

