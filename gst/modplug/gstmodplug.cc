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
   Code based on modplugxmms
   XMMS plugin:
     Kenton Varda <temporal@gauge3d.org>
   Sound Engine:
     Olivier Lapicque <olivierl@jps.net>  
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "libmodplug/stdafx.h"
#include "libmodplug/sndfile.h"

#include "gstmodplug.h"

#include <gst/gst.h>
#include <stdlib.h>
#include <gst/audio/audio.h>

/* elementfactory information */
GstElementDetails modplug_details = {
  "ModPlug",
  "Codec/Decoder/Audio",
  "Module decoder based on modplug engine",
  "Jeremy SIMON <jsimon13@yahoo.fr>"
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SONGNAME,
  ARG_REVERB,
  ARG_REVERB_DEPTH,
  ARG_REVERB_DELAY,
  ARG_MEGABASS,
  ARG_MEGABASS_AMOUNT,
  ARG_MEGABASS_RANGE,
  ARG_NOISE_REDUCTION,
  ARG_SURROUND,
  ARG_SURROUND_DEPTH,
  ARG_SURROUND_DELAY,
  ARG_OVERSAMP
};

static GstStaticPadTemplate modplug_src_template_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (GST_AUDIO_INT_PAD_TEMPLATE_CAPS)
);

static GstStaticPadTemplate modplug_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-mod")
);

enum {
  MODPLUG_STATE_NEED_TUNE = 1,
  MODPLUG_STATE_LOAD_TUNE = 2,
  MODPLUG_STATE_PLAY_TUNE = 3,
};

static void	gst_modplug_base_init		(GstModPlugClass *klass);
static void	gst_modplug_class_init		(GstModPlugClass *klass);
static void	gst_modplug_init		(GstModPlug *filter);
static void	gst_modplug_set_property 	(GObject *object,
						 guint id,
						 const GValue *value,
						 GParamSpec *pspec );
static void	gst_modplug_get_property	(GObject *object,
						 guint id,
						 GValue *value,
						 GParamSpec *pspec );
static GstPadLinkReturn
		gst_modplug_srclink		(GstPad *pad, const GstCaps *caps);
static void  	gst_modplug_loop          	(GstElement *element);
static void	gst_modplug_setup 		(GstModPlug *modplug);
static const GstFormat *
              gst_modplug_get_formats 		(GstPad *pad);
static const GstQueryType *
              gst_modplug_get_query_types	(GstPad *pad);
static gboolean	gst_modplug_src_event		(GstPad *pad, GstEvent *event);
static gboolean	gst_modplug_src_query		(GstPad *pad,
						 GstQueryType type,
						 GstFormat *format,
						 gint64 *value);
static GstElementStateReturn  
		gst_modplug_change_state	(GstElement *element);

static GstElementClass *parent_class = NULL;

GType
gst_modplug_get_type(void) {
  static GType modplug_type = 0;

  if (!modplug_type) {
    static const GTypeInfo modplug_info = {
      sizeof(GstModPlugClass),
      (GBaseInitFunc)gst_modplug_base_init,
      NULL,
      (GClassInitFunc)gst_modplug_class_init,
      NULL,
      NULL,
      sizeof(GstModPlug),
      0,
      (GInstanceInitFunc)gst_modplug_init,
      NULL
    };
    modplug_type = g_type_register_static(GST_TYPE_ELEMENT, "GstModPlug", &modplug_info, (GTypeFlags)0);
  }
  return modplug_type;
}

static void
gst_modplug_base_init (GstModPlugClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&modplug_sink_template_factory));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&modplug_src_template_factory));
  gst_element_class_set_details (element_class, &modplug_details);
}

static void
gst_modplug_class_init (GstModPlugClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = GST_ELEMENT_CLASS( g_type_class_ref(GST_TYPE_ELEMENT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SONGNAME,
    g_param_spec_string("songname","Songname","The song name",
                        "", G_PARAM_READABLE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB,
    g_param_spec_boolean("reverb", "reverb", "reverb",
                         FALSE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB_DEPTH,
    g_param_spec_int("reverb_depth", "reverb_depth", "reverb_depth",
   		     0, 100, 30, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_REVERB_DELAY,
    g_param_spec_int("reverb_delay", "reverb_delay", "reverb_delay",
	 	     0, 200, 100, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS,
    g_param_spec_boolean("megabass", "megabass", "megabass",
                         FALSE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS_AMOUNT,
    g_param_spec_int("megabass_amount", "megabass_amount", "megabass_amount",
                     0, 100, 40, (GParamFlags)G_PARAM_READWRITE ));
					
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEGABASS_RANGE,
    g_param_spec_int("megabass_range", "megabass_range", "megabass_range",
                     0, 100, 30, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND,
    g_param_spec_boolean("surround", "surround", "surround",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND_DEPTH,
    g_param_spec_int("surround_depth", "surround_depth", "surround_depth",
                     0, 100, 20, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SURROUND_DELAY,
    g_param_spec_int("surround_delay", "surround_delay", "surround_delay",
                     0, 40, 20, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OVERSAMP,
    g_param_spec_boolean("oversamp", "oversamp", "oversamp",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NOISE_REDUCTION,
    g_param_spec_boolean("noise_reduction", "noise_reduction", "noise_reduction",
                         TRUE, (GParamFlags)G_PARAM_READWRITE ));

  gobject_class->set_property = gst_modplug_set_property;
  gobject_class->get_property = gst_modplug_get_property;

  gstelement_class->change_state = gst_modplug_change_state;
}

static void
gst_modplug_init (GstModPlug *modplug)
{  
  modplug->sinkpad = gst_pad_new_from_template (gst_static_pad_template_get (&modplug_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT(modplug), modplug->sinkpad);

  modplug->srcpad = gst_pad_new_from_template (gst_static_pad_template_get (&modplug_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT(modplug), modplug->srcpad);
  gst_pad_set_link_function (modplug->srcpad, gst_modplug_srclink);
  
  gst_pad_set_event_function (modplug->srcpad, (GstPadEventFunction)GST_DEBUG_FUNCPTR(gst_modplug_src_event));
  gst_pad_set_query_function (modplug->srcpad, gst_modplug_src_query);
  gst_pad_set_query_type_function (modplug->srcpad,  (GstPadQueryTypeFunction) GST_DEBUG_FUNCPTR (gst_modplug_get_query_types));
  gst_pad_set_formats_function (modplug->srcpad, (GstPadFormatsFunction)GST_DEBUG_FUNCPTR (gst_modplug_get_formats));
  
  gst_element_set_loop_function (GST_ELEMENT (modplug), gst_modplug_loop);      
  
  modplug->reverb          = FALSE;
  modplug->reverb_depth    = 30;
  modplug->reverb_delay    = 100;
  modplug->megabass        = FALSE;
  modplug->megabass_amount = 40;
  modplug->megabass_range  = 30;	  
  modplug->surround        = TRUE;
  modplug->surround_depth  = 20;
  modplug->surround_delay  = 20;
  modplug->oversamp        = TRUE;
  modplug->noise_reduction = TRUE;

  modplug->_16bit          = TRUE;
  modplug->channel         = 2;
  modplug->frequency       = 44100;
  modplug->audiobuffer = NULL;
  modplug->buffer_in = NULL;
  
  modplug->state = MODPLUG_STATE_NEED_TUNE;
}

static void
gst_modplug_setup (GstModPlug *modplug)
{
  if (modplug->_16bit) 
    modplug->mSoundFile->SetWaveConfig (modplug->frequency, 16, modplug->channel);
  else
    modplug->mSoundFile->SetWaveConfig (modplug->frequency, 8,  modplug->channel);
  
  modplug->mSoundFile->SetWaveConfigEx (modplug->surround, !modplug->oversamp, modplug->reverb, true, modplug->megabass, modplug->noise_reduction, true);
  modplug->mSoundFile->SetResamplingMode (SRCMODE_POLYPHASE);

  if (modplug->surround)
    modplug->mSoundFile->SetSurroundParameters (modplug->surround_depth, modplug->surround_delay);

  if (modplug->megabass)
    modplug->mSoundFile->SetXBassParameters (modplug->megabass_amount, modplug->megabass_range);

  if (modplug->reverb)
    modplug->mSoundFile->SetReverbParameters (modplug->reverb_depth, modplug->reverb_delay);

}

static const GstFormat*
gst_modplug_get_formats (GstPad *pad)
{
  static const GstFormat src_formats[] = {
/*    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  static const GstFormat sink_formats[] = {
    /*GST_FORMAT_BYTES,*/
    GST_FORMAT_TIME,
    (GstFormat)0
  };
  
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

static const GstQueryType*
gst_modplug_get_query_types (GstPad *pad)
{
  static const GstQueryType gst_modplug_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    (GstQueryType)0
  };
  
  return gst_modplug_src_query_types;
}


static gboolean
gst_modplug_src_query (GstPad *pad, GstQueryType type,
		                   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstModPlug *modplug;
  gfloat tmp;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_TIME:
            *value=(gint64)modplug->mSoundFile->GetSongTime() * GST_SECOND;
            break;
        default:
            res = FALSE;
            break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
         default:
           tmp = ((float)( modplug->mSoundFile->GetSongTime() * modplug->mSoundFile->GetCurrentPos() ) / (float)modplug->mSoundFile->GetMaxPosition() );
           *value=(gint64)(tmp * GST_SECOND);
           break;
      }
    default:
      break;
  }

  return res;
}    
		

static gboolean
gst_modplug_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstModPlug *modplug; 

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gboolean flush;
      GstFormat format;

      format = GST_FORMAT_TIME;

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

      modplug->seek_at = GST_EVENT_SEEK_OFFSET (event);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  
  gst_event_unref (event);

  return res;
}

#if 0
static GstCaps*
gst_modplug_get_streaminfo (GstModPlug *modplug)
{
  GstCaps *caps;
 
  props = gst_props_empty_new ();

  entry = gst_props_entry_new ("Patterns", G_TYPE_INT ((gint)modplug->mSoundFile->GetNumPatterns()));
  gst_props_add_entry (props, (GstPropsEntry *) entry);
  
  caps = gst_caps_new_simple ("application/x-gst-streaminfo", NULL);
  return caps;
}


static void
gst_modplug_update_info (GstModPlug *modplug)
{  
    if (modplug->streaminfo) {
      gst_caps_unref (modplug->streaminfo);
    }

    modplug->streaminfo = gst_modplug_get_streaminfo (modplug);
    g_object_notify (G_OBJECT (modplug), "streaminfo"); 
}

static void 
gst_modplug_update_metadata (GstModPlug *modplug)
{  
  GstProps *props;
  GstPropsEntry *entry;
  const gchar *title;

  props = gst_props_empty_new ();

  title = modplug->mSoundFile->GetTitle();
  entry = gst_props_entry_new ("Title", G_TYPE_STRING (title));
  gst_props_add_entry (props, entry);

  modplug->metadata = gst_caps_new_simple ("application/x-gst-metadata",
		  NULL);

  g_object_notify (G_OBJECT (modplug), "metadata");
}
#endif


static GstPadLinkReturn
modplug_negotiate (GstModPlug *modplug)
{
  GstPadLinkReturn ret = GST_PAD_LINK_OK;
  gboolean sign;
  modplug->length = 1152 * modplug->channel;
  
  if (modplug->_16bit)
  {
    modplug->length *= 2;
    modplug->bitsPerSample = 16;
    sign = TRUE;
  }
  else {
    modplug->bitsPerSample = 8;
    sign = FALSE;
  }
    
  if ((ret = gst_pad_try_set_caps (modplug->srcpad, 
	  gst_caps_new_simple ("audio/x-raw-int",
	    "endianness",   G_TYPE_INT, G_BYTE_ORDER,
	    "signed",     	G_TYPE_BOOLEAN, sign,
	    "width",      	G_TYPE_INT, modplug->bitsPerSample,
	    "depth",      	G_TYPE_INT, modplug->bitsPerSample,
	    "rate",       	G_TYPE_INT, modplug->frequency,
	    "channels",     G_TYPE_INT, modplug->channel,
	    NULL))) <= 0) {
    return ret;
  }
  
  gst_modplug_setup (modplug);

  return ret;
}


static GstPadLinkReturn
gst_modplug_srclink (GstPad *pad, const GstCaps *caps)
{
  GstModPlug *modplug; 
  GstStructure *structure;
  gint depth;

  modplug = GST_MODPLUG (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "depth", &depth);
  modplug->_16bit = (depth == 16);
  gst_structure_get_int (structure, "channels", &modplug->channel);
  gst_structure_get_int (structure, "rate", &modplug->frequency);

  return modplug_negotiate(modplug);
}

static void
gst_modplug_handle_event (GstModPlug *modplug)
{
  guint32 remaining;
  GstEvent *event;

  gst_bytestream_get_status (modplug->bs, &remaining, &event);

  if (!event) {
    g_warning ("modplug: no bytestream event");
    return;
  }

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      gst_bytestream_flush_fast (modplug->bs, remaining);
    default:
      gst_pad_event_default (modplug->sinkpad, event);
      break;
  }
}

static void
gst_modplug_loop (GstElement *element)
{
  GstModPlug *modplug;  
  GstEvent *event;    

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_MODPLUG (element));
	
  modplug = GST_MODPLUG (element);

  if (modplug->state == MODPLUG_STATE_NEED_TUNE) 
  {            
/*    GstBuffer *buf;*/
       
    modplug->seek_at = -1;
    modplug->need_discont = FALSE;
    modplug->eos = FALSE;
/*            
    buf = gst_pad_pull (modplug->sinkpad);
    g_assert (buf != NULL);
      
    if (GST_IS_EVENT (buf)) {
      GstEvent *event = GST_EVENT (buf);

      switch (GST_EVENT_TYPE (buf)) {
        case GST_EVENT_EOS:             
          modplug->state = MODPLUG_STATE_LOAD_TUNE;
          break;
        case GST_EVENT_DISCONTINUOUS:
          break;
        default:
           bail out, we're not going to do anything 
          gst_event_unref (event);
          gst_pad_send_event (modplug->srcpad, gst_event_new (GST_EVENT_EOS));
          gst_element_set_eos (element);
          return;
      }
      gst_event_unref (event);
    }
    else {      
      memcpy (modplug->buffer_in + modplug->song_size, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      modplug->song_size += GST_BUFFER_SIZE (buf);

      gst_buffer_unref (buf);
    }
*/

    if (modplug->bs)
    {
      guint64 got;
      
      modplug->song_size = gst_bytestream_length (modplug->bs);

      got = gst_bytestream_peek_bytes (modplug->bs, &modplug->buffer_in,  modplug->song_size);

      if ( got < modplug->song_size )
      {
        gst_modplug_handle_event (modplug);
        return;
      }
      modplug->state = MODPLUG_STATE_LOAD_TUNE; 
    }  
  }  
  
  if (modplug->state == MODPLUG_STATE_LOAD_TUNE) 
  {            
    modplug->mSoundFile = new CSoundFile;
    
    if (!GST_PAD_CAPS (modplug->srcpad) &&
        modplug_negotiate (modplug) <= 0) {
      gst_element_error (modplug, CORE, NEGOTIATION, ("test"), ("test"));
      return;
    }
        
    modplug->mSoundFile->Create (modplug->buffer_in, modplug->song_size);    
    modplug->opened = TRUE;
      
    gst_bytestream_flush (modplug->bs, modplug->song_size);
    modplug->buffer_in = NULL;

    modplug->audiobuffer = (guchar *) g_malloc (modplug->length);
    
    //gst_modplug_update_metadata (modplug);
    //gst_modplug_update_info (modplug);

    modplug->state = MODPLUG_STATE_PLAY_TUNE;
  }
      
  if (modplug->state == MODPLUG_STATE_PLAY_TUNE && !modplug->eos) 
  {
    if (modplug->seek_at != -1)
    {
      gint seek_to_pos;
      gint64 total;
      gfloat temp;
       
      total = modplug->mSoundFile->GetSongTime () * GST_SECOND;

      temp = (gfloat) total / modplug->seek_at;     
      seek_to_pos = (int) (modplug->mSoundFile->GetMaxPosition () / temp);

      modplug->mSoundFile->SetCurrentPos (seek_to_pos);    
      modplug->need_discont = TRUE;
      modplug->seek_at = -1;
    }
        
    if (modplug->mSoundFile->Read (modplug->audiobuffer, modplug->length) != 0)
    {         
      GstBuffer *buffer_out;
      GstFormat format;
      gint64 value;
 
      format = GST_FORMAT_TIME;
      gst_modplug_src_query (modplug->srcpad, GST_QUERY_POSITION, &format, &value);
      
      if (modplug->need_discont && GST_PAD_IS_USABLE (modplug->srcpad))
      {
        GstEvent *discont;
    
        discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, value, NULL);
        gst_pad_push (modplug->srcpad, GST_DATA (discont));        
      
        modplug->need_discont= FALSE;
      }
 	  
      buffer_out = gst_buffer_new ();
      GST_BUFFER_DATA (buffer_out) = (guchar *) g_memdup (modplug->audiobuffer, modplug->length);
      GST_BUFFER_SIZE (buffer_out) = modplug->length;
      GST_BUFFER_TIMESTAMP (buffer_out) = value;
      
      if (GST_PAD_IS_USABLE (modplug->srcpad))
        gst_pad_push (modplug->srcpad, GST_DATA (buffer_out));   
    }
    else
      if (GST_PAD_IS_LINKED (modplug->srcpad))
      {        
        /* FIXME, hack, pull final EOS from peer */
        gst_bytestream_flush (modplug->bs, 1);
	
        event = gst_event_new (GST_EVENT_EOS);
        gst_pad_push (modplug->srcpad, GST_DATA (event));
        gst_element_set_eos (element);
        modplug->eos = TRUE;
      }
  }
}


static GstElementStateReturn
gst_modplug_change_state (GstElement *element)
{
  GstModPlug *modplug;

  modplug = GST_MODPLUG (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:  
      modplug->bs = gst_bytestream_new (modplug->sinkpad);
      modplug->song_size = 0;
      modplug->state = MODPLUG_STATE_NEED_TUNE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:     
      gst_bytestream_destroy (modplug->bs);          
      modplug->bs = NULL;
      if (modplug->opened)
      {
        modplug->mSoundFile->Destroy ();      
        modplug->opened = FALSE;
      }
      if (modplug->audiobuffer) g_free (modplug->audiobuffer);      
      modplug->buffer_in = NULL;
      modplug->audiobuffer = NULL;
      modplug->state = MODPLUG_STATE_NEED_TUNE;
      break;
    case GST_STATE_READY_TO_NULL:         
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
    
  return GST_STATE_SUCCESS;
}


static void
gst_modplug_set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG (object);

  switch (id) {
    case ARG_REVERB:
      modplug->reverb = g_value_get_boolean (value);
      break;
    case ARG_REVERB_DEPTH:
      modplug->reverb_depth = g_value_get_int (value);
      break;
    case ARG_REVERB_DELAY:
      modplug->reverb_delay = g_value_get_int (value);
      break;
    case ARG_MEGABASS:
      modplug->megabass = g_value_get_boolean (value);
      break;
    case ARG_MEGABASS_AMOUNT:
      modplug->megabass_amount = g_value_get_int (value);
      break;
    case ARG_MEGABASS_RANGE:
      modplug->megabass_range = g_value_get_int (value);
      break;
    case ARG_NOISE_REDUCTION:
      modplug->noise_reduction = g_value_get_boolean (value);
      break;
    case ARG_SURROUND:
      modplug->surround = g_value_get_boolean (value);
      break;
    case ARG_SURROUND_DEPTH:
      modplug->surround_depth = g_value_get_int (value);
      break;
    case ARG_SURROUND_DELAY:
      modplug->surround_delay = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_modplug_get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec )
{
  GstModPlug *modplug;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MODPLUG(object));
  modplug = GST_MODPLUG (object);
  
  switch (id) {
    case ARG_REVERB:
      g_value_set_boolean (value, modplug->reverb);
      break;
    case ARG_REVERB_DEPTH:
      g_value_set_int (value, modplug->reverb_depth);
      break;
    case ARG_REVERB_DELAY:
      g_value_set_int (value, modplug->reverb_delay);
      break;
    case ARG_MEGABASS:
      g_value_set_boolean (value, modplug->megabass);
      break;
    case ARG_MEGABASS_AMOUNT:
      g_value_set_int (value, modplug->megabass_amount);
      break;
    case ARG_MEGABASS_RANGE:
      g_value_set_int (value, modplug->megabass_range);
      break;
    case ARG_SURROUND:
      g_value_set_boolean (value, modplug->surround);
      break;
    case ARG_SURROUND_DEPTH:
      g_value_set_int (value, modplug->surround_depth);
      break;
    case ARG_SURROUND_DELAY:
      g_value_set_int (value, modplug->surround_delay);
      break;
    case ARG_NOISE_REDUCTION:
      g_value_set_boolean (value, modplug->noise_reduction);
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;
  
  return gst_element_register (plugin, "modplug",
			       GST_RANK_PRIMARY, GST_TYPE_MODPLUG);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "modplug",
  ".MOD audio decoding",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
