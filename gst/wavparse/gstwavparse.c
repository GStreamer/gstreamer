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
#include <string.h>

#include <gstwavparse.h>

static void		gst_wavparse_class_init		(GstWavParseClass *klass);
static void		gst_wavparse_init		(GstWavParse *wavparse);

static GstElementStateReturn
			gst_wavparse_change_state 	(GstElement *element);

static GstCaps*		wav_type_find			(GstByteStream *bs,
							 gpointer private);

static const GstFormat*	gst_wavparse_get_formats	(GstPad *pad);
static const GstQueryType *
			gst_wavparse_get_query_types	(GstPad *pad);
static gboolean		gst_wavparse_pad_query		(GstPad *pad, 
		                                	 GstQueryType type,
							 GstFormat *format, 
							 gint64 *value);
static gboolean		gst_wavparse_pad_convert 	(GstPad *pad,
							 GstFormat src_format,
							 gint64 src_value,
							 GstFormat *dest_format,
							 gint64 *dest_value);
static void		gst_wavparse_chain		(GstPad *pad, GstData *_data);

static const GstEventMask*
			gst_wavparse_get_event_masks 	(GstPad *pad);
static gboolean 	gst_wavparse_srcpad_event 	(GstPad *pad, GstEvent *event);
static void             gst_wavparse_get_property       (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* elementfactory information */
static GstElementDetails gst_wavparse_details = {
  ".wav parser",
  "Codec/Parser",
  "LGPL",
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
    "audio/x-raw-int",
       "endianness",       GST_PROPS_INT (G_LITTLE_ENDIAN),
       "signed",           GST_PROPS_LIST (
				GST_PROPS_BOOLEAN (FALSE),
				GST_PROPS_BOOLEAN (TRUE)
			   ),
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
  ),
  GST_CAPS_NEW (
    "wavparse_mpeg",
    "audio/mpeg",
      "rate",              GST_PROPS_INT_RANGE (8000, 48000),
      "channels",          GST_PROPS_INT_RANGE (1, 2),
      "layer",             GST_PROPS_INT_RANGE (1, 3)
  ),
  GST_CAPS_NEW (
    "parsewav_law",
    "audio/x-alaw",
      "rate",              GST_PROPS_INT_RANGE (8000, 48000),
      "channels",          GST_PROPS_INT_RANGE (1, 2)
  ),
  GST_CAPS_NEW (
    "parsewav_law",
    "audio/x-mulaw",
      "rate",              GST_PROPS_INT_RANGE (8000, 48000),
      "channels",          GST_PROPS_INT_RANGE (1, 2)
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
  PROP_0,
  PROP_METADATA
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
  GObjectClass *object_class;
  
  gstelement_class = (GstElementClass*) klass;
  object_class = (GObjectClass *) klass;
  
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  object_class->get_property = gst_wavparse_get_property;
  gstelement_class->change_state = gst_wavparse_change_state;

  g_object_class_install_property (object_class, PROP_METADATA,
				   g_param_spec_boxed ("metadata",
						       "Metadata", "Metadata",
						       GST_TYPE_CAPS,
						       G_PARAM_READABLE));
}

static void 
gst_wavparse_init (GstWavParse *wavparse) 
{
  /* sink */
  wavparse->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->sinkpad);

  gst_pad_set_formats_function (wavparse->sinkpad, gst_wavparse_get_formats);
  gst_pad_set_convert_function (wavparse->sinkpad, gst_wavparse_pad_convert);
  gst_pad_set_query_type_function (wavparse->sinkpad, 
		                   gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->sinkpad, gst_wavparse_pad_query);

  /* source */
  wavparse->srcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (wavparse), wavparse->srcpad);
  gst_pad_set_formats_function (wavparse->srcpad, gst_wavparse_get_formats);
  gst_pad_set_convert_function (wavparse->srcpad, gst_wavparse_pad_convert);
  gst_pad_set_query_type_function (wavparse->srcpad,
		                   gst_wavparse_get_query_types);
  gst_pad_set_query_function (wavparse->srcpad, gst_wavparse_pad_query);
  gst_pad_set_event_function (wavparse->srcpad, gst_wavparse_srcpad_event);
  gst_pad_set_event_mask_function (wavparse->srcpad, gst_wavparse_get_event_masks);

  gst_pad_set_chain_function (wavparse->sinkpad, gst_wavparse_chain);


  wavparse->riff = NULL;
  wavparse->state = GST_WAVPARSE_UNKNOWN;
  wavparse->riff = NULL;
  wavparse->riff_nextlikely = 0;
  wavparse->size = 0;
  wavparse->bps = 0;
  wavparse->offset = 0;
  wavparse->need_discont = FALSE;
}

static void
gst_wavparse_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
  GstWavParse *wavparse;

  wavparse = GST_WAVPARSE (object);

  switch (prop_id) {
  case PROP_METADATA:
    g_value_set_boxed (value, wavparse->metadata);
    break;

  default:
    break;
  }
}

static GstCaps*
wav_type_find (GstByteStream *bs, gpointer private)
{
  GstCaps *new = NULL;
  GstBuffer *buf = NULL;

  if (gst_bytestream_peek (bs, &buf, 12) == 12) {
    gchar *data = GST_BUFFER_DATA (buf);

    if (!strncmp (&data[0], "RIFF", 4) &&
        !strncmp (&data[8], "WAVE", 4)) {
      new = GST_CAPS_NEW ("wav_type_find",
			  "audio/x-wav",
			    NULL);
    }
  }

  if (buf != NULL) {
    gst_buffer_unref (buf);
  }

  return new;
}

static void
demux_metadata (GstWavParse *wavparse,
		const char *data,
		int len)
{
  gst_riff_chunk *temp_chunk, chunk;
  char *name, *type;
  GstPropsEntry *entry;
  GstProps *props;

  props = gst_props_empty_new ();
  
  while (len > 0) {
    temp_chunk = (gst_riff_chunk *) data;
    chunk.id = GUINT32_FROM_LE (temp_chunk->id);
    chunk.size = GUINT32_FROM_LE (temp_chunk->size);

    /* move our pointer on past the header */
    len -= sizeof (gst_riff_chunk);
    data += sizeof (gst_riff_chunk);
    
    if (chunk.size == 0) {
      continue;
    }

    name = g_strndup (data, chunk.size);

    /* move our pointer on past the data ... on an even boundary */
    len -= ((chunk.size + 1) & ~1);
    data += ((chunk.size + 1) & ~1);

    /* We now have an info string in 'name' of type chunk.id
       - find type */

    switch (chunk.id) {
    case GST_RIFF_INFO_IARL:
      type = "Location";
      break;
      
    case GST_RIFF_INFO_IART:
      type = "Artist";
      break;
      
    case GST_RIFF_INFO_ICMS:
      type = "Commissioner";
      break;
      
    case GST_RIFF_INFO_ICMT:
      type = "Comment";
      break;
      
    case GST_RIFF_INFO_ICOP:
      type = "Copyright";
      break;
      
    case GST_RIFF_INFO_ICRD:
      type = "Creation Date";
      break;
      
    case GST_RIFF_INFO_IENG:
      type = "Engineer";
      break;
      
    case GST_RIFF_INFO_IGNR:
      type = "Genre";
      break;
      
    case GST_RIFF_INFO_IKEY:
      type = "Keywords";
      break;
      
    case GST_RIFF_INFO_INAM:
      type = "Title"; /* name */
      break;
      
    case GST_RIFF_INFO_IPRD:
      type = "Product";
      break;
      
    case GST_RIFF_INFO_ISBJ:
      type = "Subject";
      break;
      
    case GST_RIFF_INFO_ISFT:
      type = "Software";
      break;
      
    case GST_RIFF_INFO_ITCH:
      type = "Technician";
      break;
      
    default:
      type = NULL;
      break;
    }
    
    if (type) {
      entry = gst_props_entry_new (type, GST_PROPS_STRING (name));
      gst_props_add_entry (props, entry);
    }

    g_free (name);
  }

  gst_caps_replace_sink (&wavparse->metadata,
			 gst_caps_new ("wav_metadata",
				       "application/x-gst-metadata",
				       props));
  g_object_notify (G_OBJECT (wavparse), "metadata");
}

static void wav_new_chunk_callback(GstRiffChunk *chunk, gpointer data)
{
  GstWavParse *wavparse;
  GstWavParseFormat *format;
  GstCaps *caps = NULL;
  
  wavparse = GST_WAVPARSE (data);

  GST_DEBUG("new tag " GST_FOURCC_FORMAT "\n", GST_FOURCC_ARGS(chunk->id));

  switch (chunk->id) {
  case GST_RIFF_TAG_fmt:
    
    /* we can gather format information now */
    format = (GstWavParseFormat *)((guchar *) GST_BUFFER_DATA (wavparse->buf) + chunk->offset);
    
    wavparse->bps      = GUINT16_FROM_LE(format->wBlockAlign);
    wavparse->rate     = GUINT32_FROM_LE(format->dwSamplesPerSec);
    wavparse->channels = GUINT16_FROM_LE(format->wChannels);
    wavparse->width    = GUINT16_FROM_LE(format->wBitsPerSample);
    wavparse->format	 = GINT16_FROM_LE(format->wFormatTag);
    
    /* set the caps on the src pad */
    /* FIXME: handle all of the other formats as well */
    switch (wavparse->format) {
    case GST_RIFF_WAVE_FORMAT_ALAW:
    case GST_RIFF_WAVE_FORMAT_MULAW: {
      gchar *mime = (wavparse->format == GST_RIFF_WAVE_FORMAT_ALAW) ?
	"audio/x-alaw" : "audio/x-mulaw";
      if (!(wavparse->width == 8)) {
	g_warning("Ignoring invalid width %d",
		  wavparse->width);
	return;
      }
      caps = GST_CAPS_NEW (
	"parsewav_src",
	mime,
	"rate",	GST_PROPS_INT (wavparse->rate),
	"channels",	GST_PROPS_INT (wavparse->channels)
	);
    }
      break;
      
    case GST_RIFF_WAVE_FORMAT_PCM:
      caps = GST_CAPS_NEW (
	"parsewav_src",
	"audio/x-raw-int",
	"endianness",	GST_PROPS_INT (G_LITTLE_ENDIAN),
	"signed",     GST_PROPS_BOOLEAN ((wavparse->width > 8) ? TRUE : FALSE),
	"width",	GST_PROPS_INT (wavparse->width),
	"depth",	GST_PROPS_INT (wavparse->width),
	"rate",	GST_PROPS_INT (wavparse->rate),
	"channels",	GST_PROPS_INT (wavparse->channels)
	);
      break;
    case GST_RIFF_WAVE_FORMAT_MPEGL12:
    case GST_RIFF_WAVE_FORMAT_MPEGL3: {
      gint layer = (wavparse->format == GST_RIFF_WAVE_FORMAT_MPEGL12) ? 2 : 3;
      caps = GST_CAPS_NEW (
	"parsewav_src",
	"audio/mpeg",
	"layer",	GST_PROPS_INT (layer),
	"rate",	GST_PROPS_INT (wavparse->rate),
	"channels",	GST_PROPS_INT (wavparse->channels)
	);
    }
      break;
      
    default:
      gst_element_error (GST_ELEMENT (wavparse), "wavparse: format %d not handled", wavparse->format);
      return;
    }
    
    if (gst_pad_try_set_caps (wavparse->srcpad, caps) <= 0) {
      gst_element_error (GST_ELEMENT (wavparse), "Could not set caps");
      return;
    }
    
    GST_DEBUG ("frequency %d, channels %d",
	       wavparse->rate, wavparse->channels);
    
    /* we're now looking for the data chunk */
    wavparse->state = GST_WAVPARSE_CHUNK_DATA;
    break;

  case GST_RIFF_TAG_LIST:
    if (strncmp (chunk->data, "INFO", 4) == 0) {
      /* We got metadata */
      demux_metadata (wavparse, chunk->data + 4, chunk->size - 4);
    }
    break;

  default:
    break;
  }
}

static void
gst_wavparse_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstWavParse *wavparse;
  gboolean buffer_riffed = FALSE;	/* so we don't parse twice */
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  g_return_if_fail (GST_BUFFER_DATA (buf) != NULL);

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));
  GST_DEBUG ("gst_wavparse_chain: got buffer in '%s'",
          gst_object_get_name (GST_OBJECT (wavparse)));

  size = GST_BUFFER_SIZE (buf);

  wavparse->buf = buf;

  /* walk through the states in priority order */
  /* we're in the data region */
  if (wavparse->state == GST_WAVPARSE_DATA) {
    GstFormat format;
    guint64 maxsize;

    /* we can't go beyond the max length */
    maxsize = wavparse->riff_nextlikely - GST_BUFFER_OFFSET (buf);

    if (maxsize == 0) {
      return;
    } else if (maxsize < size) {
      /* if we're expected to see a new chunk in this buffer */
      GstBuffer *newbuf;

      newbuf = gst_buffer_create_sub (buf, 0, maxsize);
      gst_buffer_unref (buf);
      buf = newbuf;

      size = maxsize;

      wavparse->state = GST_WAVPARSE_OTHER;
      /* I suppose we could signal an EOF at this point, but that may be
         premature.  We've stopped data flow, that's the main thing. */
    }

    if (GST_PAD_IS_USABLE (wavparse->srcpad)) {
      format = GST_FORMAT_TIME;
      gst_pad_convert (wavparse->srcpad,
		       GST_FORMAT_BYTES,
		       wavparse->offset,
		       &format,
		       &GST_BUFFER_TIMESTAMP (buf));

      if (wavparse->need_discont) {
        if (buf && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
          gst_pad_push (wavparse->srcpad, 
	                GST_DATA (gst_event_new_discontinuous (FALSE,
      			      GST_FORMAT_BYTES, wavparse->offset,
			      GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (buf), 
			      NULL)));
	} else {
          gst_pad_push (wavparse->srcpad, 
	                GST_DATA (gst_event_new_discontinuous (FALSE,
      			      GST_FORMAT_BYTES, wavparse->offset,
			      NULL)));
	}
        wavparse->need_discont = FALSE;
      }
      gst_pad_push (wavparse->srcpad, GST_DATA (buf));
    } else {
      gst_buffer_unref (buf);
    }

    wavparse->offset += size;

    return;
  }

  if (wavparse->state == GST_WAVPARSE_OTHER) {
    GST_DEBUG ("we're in unknown territory here, not passing on");

    gst_buffer_unref(buf);
    return;
  }

  /* here we deal with parsing out the primary state */
  /* these are sequenced such that in the normal case each (RIFF/WAVE,
     fmt, data) will fire in sequence, as they should */

  /* we're in null state now, look for the RIFF header, start parsing */
  if (wavparse->state == GST_WAVPARSE_UNKNOWN) {
    gint retval;

    GST_DEBUG ("GstWavParse: checking for RIFF format");

    /* create a new RIFF parser */
    wavparse->riff = gst_riff_parser_new (wav_new_chunk_callback, wavparse);

    /* give it the current buffer to start parsing */
    retval = gst_riff_parser_next_buffer (wavparse->riff, buf, 0);
    buffer_riffed = TRUE;
    if (retval < 0) {
      GST_DEBUG ("sorry, isn't RIFF");
      gst_buffer_unref(buf);
      return;
    }

    /* this has to be a file of form WAVE for us to deal with it */
    if (wavparse->riff->form != gst_riff_fourcc_to_id ("WAVE")) {
      GST_DEBUG ("sorry, isn't WAVE");
      gst_buffer_unref(buf);
      return;
    }

    /* at this point we're waiting for the 'fmt ' chunk */
    /* careful, the state may have changed in the callback */
    if (wavparse->state == GST_WAVPARSE_UNKNOWN){
      wavparse->state = GST_WAVPARSE_CHUNK_FMT;
    }
  }

  /* we're now looking for the 'fmt ' chunk to get the audio info */
  if (wavparse->state == GST_WAVPARSE_CHUNK_FMT) {

    GST_DEBUG ("GstWavParse: looking for fmt chunk");

    /* there's a good possibility we may not have parsed this buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_parser_next_buffer (wavparse->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    return;
  }

  /* now we look for the data chunk */
  if (wavparse->state == GST_WAVPARSE_CHUNK_DATA) {
    GstRiffChunk *datachunk;

    GST_DEBUG ("GstWavParse: looking for data chunk");

    /* again, we might need to parse the buffer */
    if (buffer_riffed == FALSE) {
      gst_riff_parser_next_buffer (wavparse->riff, buf, GST_BUFFER_OFFSET (buf));
      buffer_riffed = TRUE;
    }

    datachunk = gst_riff_parser_get_chunk (wavparse->riff, GST_RIFF_TAG_data);

    if (datachunk != NULL) {
      gulong subsize;
      GstBuffer *newbuf;

      GST_DEBUG ("data begins at %ld", datachunk->offset);

      wavparse->datastart = datachunk->offset;

      /* at this point we can ACK that we have data */
      wavparse->state = GST_WAVPARSE_DATA;

      /* now we construct a new buffer for the remainder */
      subsize = size - datachunk->offset;
      GST_DEBUG ("sending last %ld bytes along as audio", subsize);

      newbuf = gst_buffer_create_sub (buf, datachunk->offset, subsize);
      gst_buffer_unref (buf);

      GST_BUFFER_TIMESTAMP (newbuf) = 0;

      if (GST_PAD_IS_USABLE (wavparse->srcpad))
        gst_pad_push (wavparse->srcpad, GST_DATA (newbuf));
      else
	gst_buffer_unref (newbuf);

      wavparse->offset = subsize;

      /* now we're ready to go, the next buffer should start data */
      wavparse->state = GST_WAVPARSE_DATA;

      /* however, we may be expecting another chunk at some point */
      wavparse->riff_nextlikely = gst_riff_parser_get_nextlikely (wavparse->riff);

      return;
    }
  }

  gst_buffer_unref (buf);
}

/* convert and query stuff */
static const GstFormat *
gst_wavparse_get_formats (GstPad *pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,	/* a "frame", ie a set of samples per Hz */
    0,
    0
  };
  return formats;
}

static gboolean
gst_wavparse_pad_convert (GstPad *pad,
			  GstFormat src_format, gint64 src_value,
			  GstFormat *dest_format, gint64 *dest_value)
{
  gint bytes_per_sample;
  glong byterate;
  GstWavParse *wavparse;

  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));
  
  bytes_per_sample = wavparse->channels * wavparse->width / 8;
  if (bytes_per_sample == 0) {
    GST_DEBUG ("bytes_per_sample is 0, probably an mp3 - channels %d,  width %d\n",
	          wavparse->channels, wavparse->width);
    return FALSE;
  }
  byterate = (glong) (bytes_per_sample * wavparse->rate);
  if (byterate == 0) {
    g_warning ("byterate is 0, internal error\n");
    return FALSE;
  }
  GST_DEBUG ("bytes per sample: %d\n", bytes_per_sample);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      if (*dest_format == GST_FORMAT_DEFAULT)
        *dest_value = src_value / bytes_per_sample;
      else if (*dest_format == GST_FORMAT_TIME)
        *dest_value = src_value * GST_SECOND / byterate;
      else
        return FALSE;
      break;
    case GST_FORMAT_DEFAULT:
      if (*dest_format == GST_FORMAT_BYTES)
        *dest_value = src_value * bytes_per_sample;
      else if (*dest_format == GST_FORMAT_TIME)
        *dest_value = src_value * GST_SECOND / wavparse->rate;
      else
        return FALSE;
      break;
    case GST_FORMAT_TIME:
      if (*dest_format == GST_FORMAT_BYTES)
	*dest_value = src_value * byterate / GST_SECOND;
      else if (*dest_format == GST_FORMAT_DEFAULT)
	*dest_value = src_value * wavparse->rate / GST_SECOND;
      else
        return FALSE;

      *dest_value = *dest_value & ~(bytes_per_sample - 1);
      break;
    default:
      g_warning ("unhandled format for wavparse\n");
      break;
  }
  return TRUE;
}
      
static const GstQueryType *
gst_wavparse_get_query_types (GstPad *pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return types;
}

/* handle queries for location and length in requested format */
static gboolean
gst_wavparse_pad_query (GstPad *pad, GstQueryType type,
			GstFormat *format, gint64 *value)
{
  GstFormat peer_format = GST_FORMAT_BYTES;
  gint64 peer_value;
  GstWavParse *wavparse;

  /* probe sink's peer pad, convert value, and that's it :) */
  /* FIXME: ideally we'd loop over possible formats of peer instead
   * of only using BYTE */
  wavparse = GST_WAVPARSE (gst_pad_get_parent (pad));
  if (!gst_pad_query (GST_PAD_PEER (wavparse->sinkpad), type, 
		      &peer_format, &peer_value)) {
    g_warning ("Could not query sink pad's peer\n");
    return FALSE;
  }
  if (!gst_pad_convert (wavparse->sinkpad, peer_format, peer_value,
		        format, value)) {
    g_warning ("Could not query sink pad's peer\n");
    return FALSE;
  }
  GST_DEBUG ("pad_query done, value %" G_GINT64_FORMAT "\n", *value);
  return TRUE;
}

static const GstEventMask*
gst_wavparse_get_event_masks (GstPad *pad)
{ 
  static const GstEventMask gst_wavparse_src_event_masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET |
                      GST_SEEK_FLAG_FLUSH },
    { 0, }
  };
  return gst_wavparse_src_event_masks;
}   

static gboolean
gst_wavparse_srcpad_event (GstPad *pad, GstEvent *event)
{
  GstWavParse *wavparse = GST_WAVPARSE (GST_PAD_PARENT (pad));
  gboolean res = FALSE;

  GST_DEBUG ("event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 byteoffset;
      GstFormat format;

      /* we can only seek when in the DATA state */
      if (wavparse->state != GST_WAVPARSE_DATA) {
	return FALSE;
      }

      format = GST_FORMAT_BYTES;
      
      /* bring format to bytes for the peer element, 
       * FIXME be smarter here */
      res = gst_pad_convert (pad, 
		             GST_EVENT_SEEK_FORMAT (event),
		             GST_EVENT_SEEK_OFFSET (event),
		             &format,
		             &byteoffset);
      
      if (res) {
	 GstEvent *seek;

	 /* seek to byteoffset + header length */
	 seek = gst_event_new_seek (
			 GST_FORMAT_BYTES |
			 (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK), 
			 byteoffset + (GST_EVENT_SEEK_METHOD (event) == GST_SEEK_METHOD_END ? 0 : wavparse->datastart));

	 res = gst_pad_send_event (GST_PAD_PEER (wavparse->sinkpad), seek);

	 if (res) {
           /* ok, seek worked, update our state */
           wavparse->offset = byteoffset; 
	   wavparse->need_discont = TRUE;
	 }
      }
      break;
    }
    default:
      break;
  }

  gst_event_unref (event);
  return res;
}

static GstElementStateReturn
gst_wavparse_change_state (GstElement *element)
{
  GstWavParse *wavparse = GST_WAVPARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      wavparse->riff = NULL;
      wavparse->state = GST_WAVPARSE_UNKNOWN;
      wavparse->riff_nextlikely = 0;
      wavparse->size = 0;
      wavparse->bps = 0;
      wavparse->offset = 0;
      wavparse->need_discont = FALSE;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  if(!gst_library_load("gstriff")){
    return FALSE;
  }

  /* create an elementfactory for the wavparse element */
  factory = gst_element_factory_new ("wavparse", GST_TYPE_WAVPARSE,
                                    &gst_wavparse_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_SECONDARY);

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
