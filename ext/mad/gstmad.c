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
#include "gstmad.h"

/* elementfactory information */
static GstElementDetails gst_mad_details = {
  "mad mp3 decoder",
  "Filter/Decoder/Audio",
  "Uses mad code to decode mp3 streams",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};


/* Mad signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (mad_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mad_src",
    "audio/raw",
      "format",   GST_PROPS_STRING ("int"),
      "law",         GST_PROPS_INT (0),
      "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
      "signed",      GST_PROPS_BOOLEAN (TRUE),
      "width",       GST_PROPS_INT (16),
      "depth",       GST_PROPS_INT (16),
      "rate",        GST_PROPS_INT_RANGE (11025, 48000),
      "channels",    GST_PROPS_INT_RANGE (1, 2)
  )
)

GST_PADTEMPLATE_FACTORY (mad_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mad_sink",
    "audio/mp3",
    NULL
  )
)


static void 		gst_mad_class_init	(GstMadClass *klass);
static void 		gst_mad_init		(GstMad *mad);

static void 		gst_mad_loop 		(GstElement *element);

static enum mad_flow 	gst_mad_input 		(void *data, struct mad_stream *stream);
static enum mad_flow 	gst_mad_output 		(void *data, struct mad_header const *header, 
						 struct mad_pcm *pcm);
static enum mad_flow 	gst_mad_error 		(void *data, struct mad_stream *stream, 
						 struct mad_frame *frame);


static GstElementClass *parent_class = NULL;
//static guint gst_mad_signals[LAST_SIGNAL] = { 0 };

GType
gst_mad_get_type (void)
{
  static GType mad_type = 0;

  if (!mad_type) {
    static const GTypeInfo mad_info = {
      sizeof(GstMadClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mad_class_init,
      NULL,
      NULL,
      sizeof(GstMad),
      0,
      (GInstanceInitFunc)gst_mad_init,
    };
    mad_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMad", &mad_info, 0);
  }
  return mad_type;
}

static void
gst_mad_class_init (GstMadClass *klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void
gst_mad_init (GstMad *mad)
{
  /* create the sink and src pads */
  mad->sinkpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (mad_sink_template_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(mad),mad->sinkpad);
  gst_pad_set_caps (mad->sinkpad, gst_pad_get_padtemplate_caps (mad->sinkpad));

  mad->srcpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (mad_src_template_factory), "src");
  gst_element_add_pad(GST_ELEMENT(mad),mad->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (mad), GST_DEBUG_FUNCPTR(gst_mad_loop));
  // the MAD API is broken, so we have to set this
  //GST_FLAG_SET(GST_ELEMENT(mad), GST_ELEMENT_NO_ENTRY);

  mad_decoder_init (&mad->decoder, mad, 
                     gst_mad_input, 0 /* header */, 0 /* filter */, gst_mad_output,
                     gst_mad_error, 0 /* message */);

  mad->tempbuffer = g_malloc (8192);
  mad->tempsize = 0;
  mad->need_sync = TRUE;
  mad->last_time = 0;
  mad->framestamp = 0;
  mad->new_header = TRUE;
}

static enum mad_flow 
gst_mad_input (void *user_data,
               struct mad_stream *stream)
{
  GstMad *mad;
  GstBuffer *buffer = NULL;
  gchar *data;
  glong size;
  gint offset = 0;

  mad = GST_MAD (user_data);

  /* we yield here because the loop function doesn't return */
  gst_element_yield (GST_ELEMENT (mad));

  do {
    GstBuffer *inbuf;
    inbuf = gst_pad_pull (mad->sinkpad);

    /* deal with events */
    if (GST_IS_EVENT (inbuf)) {
      GstEvent *event = GST_EVENT (inbuf);

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_DISCONTINUOUS:
	  mad->need_sync = TRUE;
        case GST_EVENT_EOS:
	  if (buffer) {
	    gst_buffer_unref (buffer);
	    buffer = NULL;
          }
        default:
	  gst_pad_event_default (mad->sinkpad, event);
	  break;
      }
      return MAD_FLOW_STOP;
    }

    if (buffer) {
      buffer = gst_buffer_append (buffer, inbuf);
      gst_buffer_unref (inbuf);
    }
    else
      buffer = inbuf;
  }
  while (GST_BUFFER_SIZE (buffer) < MAD_BUFFER_MDLEN);

  mad->last_time = GST_BUFFER_TIMESTAMP (buffer);

  /* thomas added this bit to implement timestamps */
#ifdef DEBUG_TIMESTAMP
  if (GST_BUFFER_TIMESTAMP (buffer) == 0)
  {
    GST_BUFFER_TIMESTAMP (buffer) = mad->framestamp * 1E9
				  / gst_audio_frame_rate (mad->srcpad);
    printf ("DEBUG: mad: timestamp set on input  buffer: %f sec\n",
	GST_BUFFER_TIMESTAMP (buffer) / 1E9);
  }
#endif
  /* end of new bit */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (stream->next_frame != NULL && !mad->need_sync) {
    offset = mad->tempsize - (stream->next_frame - mad->tempbuffer);
    memmove (mad->tempbuffer, stream->next_frame, offset);
  }

  memcpy (mad->tempbuffer+offset, data, size);
  mad->tempsize = offset + size;

  gst_buffer_unref (buffer);
  
  GST_DEBUG (0, "decoder_in %ld %p %p\n", mad->tempsize, mad->tempbuffer, stream->next_frame);

  mad_stream_buffer (stream, mad->tempbuffer, mad->tempsize);

  /* this doesn't seem to work very well.. */
  /*if (mad->need_sync)
      mad_stream_sync (stream); */
     
  GST_DEBUG (0, "decoder_in done %p\n", stream->next_frame);

  return MAD_FLOW_CONTINUE;
}

static inline signed int 
scale (mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static gchar *layers[]   = { "unknown", "I", "II", "III" };
static gchar *modes[]    = { "single channel", "dual channel", "joint stereo", "stereo" };
static gchar *emphases[] = { "none", "50/15 microseconds", "CCITT J.17" };

static void
gst_mad_update_info (GstMad *mad, struct mad_header const *header)
{
#define CHECK_HEADER(h1,str,prop) 				\
G_STMT_START{							\
  if (mad->header.h1 != header->h1 || mad->new_header) {	\
    mad->header.h1 = header->h1;				\
    gst_element_send_event (GST_ELEMENT (mad),			\
	gst_event_new_info (str, prop, NULL));			\
  };								\
}G_STMT_END

  CHECK_HEADER (layer, 	    "layer",      GST_PROPS_STRING (layers[header->layer]));
  CHECK_HEADER (mode, 	    "mode",       GST_PROPS_STRING (modes[header->mode]));
  CHECK_HEADER (emphasis,   "emphasis",   GST_PROPS_STRING (emphases[header->emphasis]));
  CHECK_HEADER (bitrate,    "bitrate",    GST_PROPS_INT (header->bitrate));
  CHECK_HEADER (samplerate, "samplerate", GST_PROPS_INT (header->samplerate));
  if (mad->channels != MAD_NCHANNELS (header) || mad->new_header) {
    mad->channels = MAD_NCHANNELS (header);
    gst_element_send_event (GST_ELEMENT (mad),
	gst_event_new_info ("channels", GST_PROPS_INT (mad->channels), NULL));

  }

  mad->new_header = FALSE;
}

static enum mad_flow 
gst_mad_output (void *data,
                struct mad_header const *header,
                struct mad_pcm *pcm)
{
  unsigned int nchannels, nsamples;
  mad_fixed_t const *left_ch, *right_ch;
  GstMad *mad;
  GstBuffer *buffer;
  gint16 *outdata;

  mad = GST_MAD (data);

  GST_DEBUG (0, "decoder_out\n");

  /* header->sfreq or header->samplerate contains the sampling frequency */
  nchannels = MAD_NCHANNELS (header);
  nsamples  = pcm->length;
  left_ch   = pcm->samples[0];
  right_ch  = pcm->samples[1];

  gst_mad_update_info (mad, header);

  buffer = gst_buffer_new ();
  outdata = (gint16 *) GST_BUFFER_DATA (buffer) = g_malloc (nsamples*nchannels*2);
  GST_BUFFER_SIZE (buffer) = nsamples*nchannels*2;
  GST_BUFFER_TIMESTAMP (buffer) = mad->last_time;
  
  /* end of new bit */
  while (nsamples--) {
    /* output sample(s) in 16-bit signed native-endian PCM */
    *outdata++ = scale(*left_ch++) & 0xffff;

    if (nchannels == 2) {
      *outdata++ = scale(*right_ch++) & 0xffff;
    }
  }
  if (GST_PAD_CAPS (mad->srcpad) == NULL) {
    gst_pad_set_caps (mad->srcpad,
  	gst_caps_new (
  	  "mad_src",
    	  "audio/raw",
	  gst_props_new (
    	    "format",   GST_PROPS_STRING ("int"),
      	     "law",         GST_PROPS_INT (0),
      	     "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
      	     "signed",      GST_PROPS_BOOLEAN (TRUE),
      	     "width",       GST_PROPS_INT (16),
      	     "depth",       GST_PROPS_INT (16),
#if MAD_VERSION_MINOR <= 12
      	     "rate",        GST_PROPS_INT (header->sfreq),
#else
      	     "rate",        GST_PROPS_INT (header->samplerate),
#endif
      	     "channels",    GST_PROPS_INT (nchannels),
	     NULL)));
  }

  if (mad->need_sync) {
    /* use an event FIXME */
    mad->need_sync = FALSE;
  }
  if (GST_PAD_CONNECTED (mad->srcpad))
    gst_pad_push (mad->srcpad, buffer);
  else
    gst_buffer_unref (buffer);

  return MAD_FLOW_CONTINUE;
}

static enum mad_flow 
gst_mad_error (void *data,
	       struct mad_stream *stream,
	       struct mad_frame *frame)
{
  GST_DEBUG (0, "decoding error 0x%04x at byte offset %p\n",
         stream->error, stream->this_frame);

  return MAD_FLOW_CONTINUE;
}

static void
gst_mad_loop (GstElement *element)
{
  GstMad *mad;
  gint ret;

  mad = GST_MAD (element);

  GST_DEBUG (0, "decoder_run\n");
  ret = mad_decoder_run (&mad->decoder, MAD_DECODER_MODE_SYNC);
  GST_DEBUG (0, "decoder_run done %d\n", ret);
}


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the mad element */
  factory = gst_elementfactory_new("mad",GST_TYPE_MAD,
                                   &gst_mad_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (mad_sink_template_factory));
  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (mad_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mad",
  plugin_init
};
