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


#include <string.h>

#include <gstwavparse.h>

static void		gst_wavparse_class_init	(GstWavParseClass *klass);
static void		gst_wavparse_init	(GstWavParse *wavparse);

static GstCaps*		wav_type_find		(GstBuffer *buf, gpointer private);

static void		gst_wavparse_chain	(GstPad *pad, GstBuffer *buf);

/* elementfactory information */
static GstElementDetails gst_wavparse_details = {
  ".wav parser",
  "Codec/Parser",
  "Parse a .wav file into raw audio",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "wavparse_sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "wavparse_wav",   
    "audio/x-wav",  
    NULL
  )
)

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "wavparse_src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "wavparse_raw",   
    "audio/raw",  
      "format",            GST_PROPS_STRING ("int"),
       "law",              GST_PROPS_INT (0),
       "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
       "signed",           GST_PROPS_BOOLEAN (TRUE),
       "width",            GST_PROPS_LIST (
	                     GST_PROPS_INT (8),
	                     GST_PROPS_INT (16)
			   ),
       "depth",            GST_PROPS_LIST (
	                     GST_PROPS_INT (8),
	                     GST_PROPS_INT (16)
			   ),
       "rate",             GST_PROPS_INT_RANGE (8000, 48000), 
       "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
)

/* typefactory for 'wav' */
static GstTypeDefinition 
wavdefinition = 
{
  "wavparse_audio/x-wav",
  "audio/x-wav",
  ".wav",
  wav_type_find,
};


/* WavParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GstElementClass *parent_class = NULL;
/*static guint gst_wavparse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_wavparse_get_type (void) 
{
  static GType wavparse_type = 0;

  if (!wavparse_type) {
    static const GTypeInfo wavparse_info = {
      sizeof(GstWavParseClass),      NULL,
      NULL,
      (GClassInitFunc) gst_wavparse_class_init,
      NULL,
      NULL,
      sizeof(GstWavParse),
      0,
      (GInstanceInitFunc) gst_wavparse_init,
    };
    wavparse_type = g_type_register_static (GST_TYPE_ELEMENT, "GstWavParse", &wavparse_info, 0);
  }
  return wavparse_type;
}

static void
gst_wavparse_class_init (GstWavParseClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void 
gst_wavparse_init (GstWavParse *wavparse) 
{
  wavparse->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);
  gst_pad_set_chain_function (wavparse->sinkpad, gst_wavparse_chain);

  wavparse->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->srcpad);

  wavparse->riff = NULL;

  wavparse->state = GST_WAVPARSE_UNKNOWN;
  wavparse->riff = NULL;
  wavparse->riff_nextlikely = 0;
  wavparse->size = 0;
  wavparse->bps = 0;
}

static GstCaps*
wav_type_find (GstBuffer *buf, gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);

  if (strncmp (&data[0], "RIFF", 4)) return NULL;
  if (strncmp (&data[8], "WAVE", 4)) return NULL;

  return gst_caps_new ("wav_type_find", "audio/x-wav", NULL);
}


static void
gst_wavparse_chain (GstPad *pad, GstBuffer *buf)
{
  GstWavParse *wavparse;
  gboolean buffer_riffed = FALSE;	/* so we don't parse twice */
  gchar *data;
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));
  GST_DEBUG (0, "gst_wavparse_chain: got buffer in '%s'",
          gst_object_get_name (GST_OBJECT (wavparse)));
  data = (guchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* walk through the states in priority order */
  /* we're in the data region */
  if (wavparse->state == GST_WAVPARSE_DATA) {
    /* if we're expected to see a new chunk in this buffer */
    if ((wavparse->riff_nextlikely - GST_BUFFER_OFFSET (buf)) < GST_BUFFER_SIZE (buf)) {
	    
      GST_BUFFER_SIZE (buf) = wavparse->riff_nextlikely - GST_BUFFER_OFFSET (buf);
      
      wavparse->state = GST_WAVPARSE_OTHER;
      /* I suppose we could signal an EOF at this point, but that may be
         premature.  We've stopped data flow, that's the main thing. */
    } 

    GST_BUFFER_TIMESTAMP (buf) = wavparse->offset * GST_SECOND / wavparse->rate;
    wavparse->offset += GST_BUFFER_SIZE (buf) * 8 / wavparse->width / wavparse->channels;

    gst_pad_push (wavparse->srcpad, buf);
    return;
  }

  if (wavparse->state == GST_WAVPARSE_OTHER) {
    GST_DEBUG (0, "we're in unknown territory here, not passing on");
    return;
  }


  /* here we deal with parsing out the primary state */
  /* these are sequenced such that in the normal case each (RIFF/WAVE,
     fmt, data) will fire in sequence, as they should */

  /* we're in null state now, look for the RIFF header, start parsing */
  if (wavparse->state == GST_WAVPARSE_UNKNOWN) {
    gint retval;

    GST_DEBUG (0, "GstWavParse: checking for RIFF format");

    /* create a new RIFF parser */
    wavparse->riff = gst_riff_new ();
    
    /* give it the current buffer to start parsing */
    retval = gst_riff_next_buffer (wavparse->riff, buf, 0);
    buffer_riffed = TRUE;
    if (retval < 0) {
      GST_DEBUG (0, "sorry, isn't RIFF");
      return;
    }

    /* this has to be a file of form WAVE for us to deal with it */
    if (wavparse->riff->form != gst_riff_fourcc_to_id ("WAVE")) {
      GST_DEBUG (0, "sorry, isn't WAVE");
      return;
    }

    /* at this point we're waiting for the 'fmt ' chunk */
    wavparse->state = GST_WAVPARSE_CHUNK_FMT;
  }

  /* we're now looking for the 'fmt ' chunk to get the audio info */
  if (wavparse->state == GST_WAVPARSE_CHUNK_FMT) {
    GstRiffChunk *fmt;
    GstWavParseFormat *format;

    GST_DEBUG (0, "GstWavParse: looking for fmt chunk");

    /* there's a good possibility we may not have parsed this buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_next_buffer (wavparse->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    /* see if the fmt chunk is available yet */
    fmt = gst_riff_get_chunk (wavparse->riff, "fmt ");

    /* if we've got something, deal with it */
    if (fmt != NULL) {
      GstCaps *caps;


      /* we can gather format information now */
      format = (GstWavParseFormat *)((guchar *) GST_BUFFER_DATA (buf) + fmt->offset);

      /* set the caps on the src pad */
      caps = GST_CAPS_NEW (
			"parsewav_src",
			"audio/raw",
			"format",	GST_PROPS_STRING ("int"),
			  "law",	GST_PROPS_INT (0),		/*FIXME */
			  "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
        		  "signed",     GST_PROPS_BOOLEAN (TRUE), /*FIXME */
			  "width",	GST_PROPS_INT (format->wBitsPerSample),
			  "depth",	GST_PROPS_INT (format->wBitsPerSample),
			  "rate",	GST_PROPS_INT (format->dwSamplesPerSec),
			  "channels",	GST_PROPS_INT (format->wChannels)
		      );

      if (!gst_pad_try_set_caps (wavparse->srcpad, caps)) {
        gst_element_error (GST_ELEMENT (wavparse), "Could not set caps");
        return;
      }

      wavparse->bps = format->wBlockAlign;
      wavparse->rate = format->dwSamplesPerSec;
      wavparse->channels = format->wChannels;
      wavparse->width = format->wBitsPerSample;
      
      GST_DEBUG (0, "frequency %d, channels %d",
		 format->dwSamplesPerSec, format->wChannels); 

      /* we're now looking for the data chunk */
      wavparse->state = GST_WAVPARSE_CHUNK_DATA;
    } else {
      /* otherwise we just sort of give up for this buffer */
      gst_buffer_unref (buf);
      return;
    }
  }

  /* now we look for the data chunk */
  if (wavparse->state == GST_WAVPARSE_CHUNK_DATA) {
    GstBuffer *newbuf;
    GstRiffChunk *datachunk;

    GST_DEBUG (0, "GstWavParse: looking for data chunk");

    /* again, we might need to parse the buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_next_buffer (wavparse->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    datachunk = gst_riff_get_chunk (wavparse->riff, "data");

    if (datachunk != NULL) {
      gulong subsize;

      GST_DEBUG (0, "data begins at %ld", datachunk->offset);

      /* at this point we can ACK that we have data */
      wavparse->state = GST_WAVPARSE_DATA;

      /* now we construct a new buffer for the remainder */
      subsize = size - datachunk->offset;
      GST_DEBUG (0, "sending last %ld bytes along as audio", subsize);
      
      newbuf = gst_buffer_new ();
      GST_BUFFER_DATA (newbuf) = g_malloc (subsize);
      GST_BUFFER_SIZE (newbuf) = subsize;
      GST_BUFFER_TIMESTAMP (newbuf) = wavparse->offset * GST_SECOND / wavparse->rate;
      wavparse->offset += subsize * 8 / wavparse->width / wavparse->channels;
      
      memcpy (GST_BUFFER_DATA (newbuf), GST_BUFFER_DATA (buf) + datachunk->offset, subsize);

      gst_buffer_unref (buf);

      gst_pad_push (wavparse->srcpad, newbuf);

      /* now we're ready to go, the next buffer should start data */
      wavparse->state = GST_WAVPARSE_DATA;

      /* however, we may be expecting another chunk at some point */
      wavparse->riff_nextlikely = gst_riff_get_nextlikely (wavparse->riff);
    } else {
      /* otherwise we just sort of give up for this buffer */
      gst_buffer_unref (buf);
      return;
    }
  }
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the wavparse element */
  factory = gst_element_factory_new ("wavparse", GST_TYPE_WAVPARSE,
                                    &gst_wavparse_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* register src pads */
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_type_factory_new (&wavdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "wavparse",
  plugin_init
};
