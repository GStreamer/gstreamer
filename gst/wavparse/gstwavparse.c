/* Gnome-Streamer
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

static void		gst_parsewav_class_init	(GstParseWavClass *klass);
static void		gst_parsewav_init	(GstParseWav *parsewav);

static GstCaps*		wav_typefind		(GstBuffer *buf, gpointer private);

static void		gst_parsewav_chain	(GstPad *pad, GstBuffer *buf);

/* elementfactory information */
static GstElementDetails gst_parsewav_details = {
  ".wav parser",
  "Parser/Audio",
  "Parse a .wav file into raw audio",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

GST_PADTEMPLATE_FACTORY (sink_template_factory,
  "parsewav_sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "parsewav_wav",   
    "audio/wav",  
    NULL
  )
)

GST_PADTEMPLATE_FACTORY (src_template_factory,
  "parsewav_src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "parsewav_raw",   
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
  "parsewav_audio/wav",
  "audio/wav",
  ".wav",
  wav_typefind,
};


/* ParseWav signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static GstElementClass *parent_class = NULL;
//static guint gst_parsewav_signals[LAST_SIGNAL] = { 0 };

GType
gst_parsewav_get_type (void) 
{
  static GType parsewav_type = 0;

  if (!parsewav_type) {
    static const GTypeInfo parsewav_info = {
      sizeof(GstParseWavClass),      NULL,
      NULL,
      (GClassInitFunc) gst_parsewav_class_init,
      NULL,
      NULL,
      sizeof(GstParseWav),
      0,
      (GInstanceInitFunc) gst_parsewav_init,
    };
    parsewav_type = g_type_register_static (GST_TYPE_ELEMENT, "GstParseWav", &parsewav_info, 0);
  }
  return parsewav_type;
}

static void
gst_parsewav_class_init (GstParseWavClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void 
gst_parsewav_init (GstParseWav *parsewav) 
{
  parsewav->sinkpad = gst_pad_new_from_template (GST_PADTEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (parsewav), parsewav->sinkpad);
  gst_pad_set_chain_function (parsewav->sinkpad, gst_parsewav_chain);

  parsewav->srcpad = gst_pad_new_from_template (GST_PADTEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (parsewav), parsewav->srcpad);

  parsewav->riff = NULL;

  parsewav->state = GST_PARSEWAV_UNKNOWN;
  parsewav->riff = NULL;
  parsewav->riff_nextlikely = 0;
  parsewav->size = 0;
  parsewav->bps = 0;
}

static GstCaps*
wav_typefind (GstBuffer *buf, gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);

  if (strncmp (&data[0], "RIFF", 4)) return NULL;
  if (strncmp (&data[8], "WAVE", 4)) return NULL;

  return gst_caps_new ("wav_typefind", "audio/wav", NULL);
}


static void
gst_parsewav_chain (GstPad *pad, GstBuffer *buf)
{
  GstParseWav *parsewav;
  gboolean buffer_riffed = FALSE;	/* so we don't parse twice */
  gchar *data;
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

  parsewav = GST_PARSEWAV (gst_pad_get_parent (pad));
  GST_DEBUG (0, "gst_parsewav_chain: got buffer in '%s'\n",
          gst_object_get_name (GST_OBJECT (parsewav)));
  data = (guchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* walk through the states in priority order */
  /* we're in the data region */
  if (parsewav->state == GST_PARSEWAV_DATA) {
    /* if we're expected to see a new chunk in this buffer */
    if ((parsewav->riff_nextlikely - GST_BUFFER_OFFSET (buf)) < GST_BUFFER_SIZE (buf)) {
	    
      GST_BUFFER_SIZE (buf) = parsewav->riff_nextlikely - GST_BUFFER_OFFSET (buf);
      
      parsewav->state = GST_PARSEWAV_OTHER;
      /* I suppose we could signal an EOF at this point, but that may be
         premature.  We've stopped data flow, that's the main thing. */
    } 
    gst_pad_push (parsewav->srcpad, buf);
    return;
  }

  if (parsewav->state == GST_PARSEWAV_OTHER) {
    GST_DEBUG (0, "we're in unknown territory here, not passing on\n");
    return;
  }


  /* here we deal with parsing out the primary state */
  /* these are sequenced such that in the normal case each (RIFF/WAVE,
     fmt, data) will fire in sequence, as they should */

  /* we're in null state now, look for the RIFF header, start parsing */
  if (parsewav->state == GST_PARSEWAV_UNKNOWN) {
    gint retval;

    GST_DEBUG (0, "GstParseWav: checking for RIFF format\n");

    /* create a new RIFF parser */
    parsewav->riff = gst_riff_new ();
    
    /* give it the current buffer to start parsing */
    retval = gst_riff_next_buffer (parsewav->riff, buf, 0);
    buffer_riffed = TRUE;
    if (retval < 0) {
      GST_DEBUG (0, "sorry, isn't RIFF\n");
      return;
    }

    /* this has to be a file of form WAVE for us to deal with it */
    if (parsewav->riff->form != gst_riff_fourcc_to_id ("WAVE")) {
      GST_DEBUG (0, "sorry, isn't WAVE\n");
      return;
    }

    /* at this point we're waiting for the 'fmt ' chunk */
    parsewav->state = GST_PARSEWAV_CHUNK_FMT;
  }

  /* we're now looking for the 'fmt ' chunk to get the audio info */
  if (parsewav->state == GST_PARSEWAV_CHUNK_FMT) {
    GstRiffChunk *fmt;
    GstParseWavFormat *format;

    GST_DEBUG (0, "GstParseWav: looking for fmt chunk\n");

    /* there's a good possibility we may not have parsed this buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_next_buffer (parsewav->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    /* see if the fmt chunk is available yet */
    fmt = gst_riff_get_chunk (parsewav->riff, "fmt ");

    /* if we've got something, deal with it */
    if (fmt != NULL) {

      /* we can gather format information now */
      format = (GstParseWavFormat *)((guchar *) GST_BUFFER_DATA (buf) + fmt->offset);

      /* set the caps on the src pad */
      gst_pad_set_caps (parsewav->srcpad, gst_caps_new (
	"parsewav_src",
	"audio/raw",
	gst_props_new (
	"format",	GST_PROPS_STRING ("int"),
	  "law",	GST_PROPS_INT (0),		//FIXME
	  "endianness",	GST_PROPS_INT (G_BYTE_ORDER),
          "signed",     GST_PROPS_BOOLEAN (TRUE), //FIXME
	  "width",	GST_PROPS_INT (format->wBitsPerSample),
	  "depth",	GST_PROPS_INT (format->wBitsPerSample),
	  "rate",	GST_PROPS_INT (format->dwSamplesPerSec),
	  "channels",	GST_PROPS_INT (format->wChannels),
	  NULL
	)
      ));

      parsewav->bps = format->wBlockAlign;
      GST_DEBUG (0, "frequency %d, channels %d\n",
		 format->dwSamplesPerSec, format->wChannels); 

      /* we're now looking for the data chunk */
      parsewav->state = GST_PARSEWAV_CHUNK_DATA;
    } else {
      /* otherwise we just sort of give up for this buffer */
      gst_buffer_unref (buf);
      return;
    }
  }

  /* now we look for the data chunk */
  if (parsewav->state == GST_PARSEWAV_CHUNK_DATA) {
    GstBuffer *newbuf;
    GstRiffChunk *datachunk;

    GST_DEBUG (0, "GstParseWav: looking for data chunk\n");

    /* again, we might need to parse the buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_next_buffer (parsewav->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    datachunk = gst_riff_get_chunk (parsewav->riff, "data");

    if (datachunk != NULL) {
      gulong subsize;

      GST_DEBUG (0, "data begins at %ld\n", datachunk->offset);

      /* at this point we can ACK that we have data */
      parsewav->state = GST_PARSEWAV_DATA;

      /* now we construct a new buffer for the remainder */
      subsize = size - datachunk->offset;
      GST_DEBUG (0, "sending last %ld bytes along as audio\n", subsize);
      
      newbuf = gst_buffer_new ();
      GST_BUFFER_DATA (newbuf) = g_malloc (subsize);
      GST_BUFFER_SIZE (newbuf) = subsize;
      
      memcpy (GST_BUFFER_DATA (newbuf), GST_BUFFER_DATA (buf) + datachunk->offset, subsize);

      gst_buffer_unref (buf);

      gst_pad_push (parsewav->srcpad, newbuf);

      /* now we're ready to go, the next buffer should start data */
      parsewav->state = GST_PARSEWAV_DATA;

      /* however, we may be expecting another chunk at some point */
      parsewav->riff_nextlikely = gst_riff_get_nextlikely (parsewav->riff);
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

  /* create an elementfactory for the parsewav element */
  factory = gst_elementfactory_new ("parsewav", GST_TYPE_PARSEWAV,
                                    &gst_parsewav_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* register src pads */
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_template_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_typefactory_new (&wavdefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "parsewav",
  plugin_init
};
