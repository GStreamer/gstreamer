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

#include <gst/gst.h>

#include <string.h>
#include <mad.h>

#define GST_TYPE_MAD \
  (gst_mad_get_type())
#define GST_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MAD,GstMad))
#define GST_MAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MAD,GstMad))
#define GST_IS_MAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MAD))
#define GST_IS_MAD_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MAD))

typedef struct _GstMad GstMad;
typedef struct _GstMadClass GstMadClass;

struct _GstMad {
  GstElement element;

  /* pads */
  GstPad *sinkpad,*srcpad;

  /* state */
  struct mad_stream stream;
  struct mad_frame frame;
  struct mad_synth synth;
  guchar *tempbuffer;
  glong tempsize;
  gboolean need_sync;
  guint64 last_time;
  guint64 framestamp;	/* timestamp-like, but counted in frames */
  guint64 sync_point; 
  guint64 total_samples; /* the number of samples since the sync point */

  /* info */
  struct mad_header header;
  gboolean new_header;
  gint channels;
};

struct _GstMadClass {
  GstElementClass parent_class;
};

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
static void 		gst_mad_dispose 	(GObject *object);

static void 		gst_mad_chain 		(GstPad *pad, GstBuffer *buffer);

static GstElementStateReturn
			gst_mad_change_state (GstElement *element);


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
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_mad_dispose;

  gstelement_class->change_state = gst_mad_change_state;
}

static void
gst_mad_init (GstMad *mad)
{
  /* create the sink and src pads */
  mad->sinkpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (mad_sink_template_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(mad),mad->sinkpad);
  gst_pad_set_chain_function (mad->sinkpad, GST_DEBUG_FUNCPTR(gst_mad_chain));

  mad->srcpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (mad_src_template_factory), "src");
  gst_element_add_pad(GST_ELEMENT(mad),mad->srcpad);

  mad->tempbuffer = g_malloc (MAD_BUFFER_MDLEN * 3);
  mad->tempsize = 0;
  mad->need_sync = TRUE;
  mad->last_time = 0;
  mad->framestamp = 0;
  mad->total_samples = 0;
  mad->sync_point = 0;
  mad->new_header = TRUE;
}

static void
gst_mad_dispose (GObject *object)
{
  GstMad *mad = GST_MAD (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (mad->tempbuffer);
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

static void
gst_mad_chain (GstPad *pad, GstBuffer *buffer)
{
  GstMad *mad;
  gchar *data;
  glong size;

  mad = GST_MAD (gst_pad_get_parent (pad));

  /* end of new bit */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  if (!GST_PAD_IS_CONNECTED (mad->srcpad)) {
    gst_buffer_unref (buffer);
    return;
  }

  while (size > 0) {
    gint tocopy;
    guchar *mad_input_buffer;

    /* cut the buffer in MDLEN pieces */
    tocopy = MIN (MAD_BUFFER_MDLEN, size);
	  
    memcpy (mad->tempbuffer + mad->tempsize, data, tocopy);
    mad->tempsize += tocopy;

    size -= tocopy;
    data += tocopy;

    mad_input_buffer = mad->tempbuffer;

    /* it we have enough data we can proceed */
    while (mad->tempsize >= MAD_BUFFER_MDLEN) {
      gint consumed;
      guint nchannels, nsamples;
      mad_fixed_t const *left_ch, *right_ch;
      GstBuffer *outbuffer;
      gint16 *outdata;

      mad_stream_buffer (&mad->stream, mad_input_buffer, mad->tempsize);

      if (mad_frame_decode (&mad->frame, &mad->stream) == -1) {
        if (!MAD_RECOVERABLE (mad->stream.error)) {
          gst_element_error (GST_ELEMENT (mad), "fatal error decoding stream");
          return;
        }
	else {
	  goto next;
	}
      }
      mad_synth_frame (&mad->synth, &mad->frame);

      nchannels = MAD_NCHANNELS (&mad->frame.header);
      nsamples  = mad->synth.pcm.length;
      left_ch   = mad->synth.pcm.samples[0];
      right_ch  = mad->synth.pcm.samples[1];

      gst_mad_update_info (mad, &mad->frame.header);

      outbuffer = gst_buffer_new ();
      outdata = (gint16 *) GST_BUFFER_DATA (outbuffer) = g_malloc (nsamples * nchannels * 2);
      GST_BUFFER_SIZE (outbuffer) = nsamples * nchannels * 2;

      mad->total_samples += nsamples;

      if (GST_BUFFER_TIMESTAMP (buffer) != -1) {
        if (GST_BUFFER_TIMESTAMP (buffer) > mad->sync_point) {
          mad->sync_point = GST_BUFFER_TIMESTAMP (buffer);
	  mad->total_samples = 0;
	}
      }
      GST_BUFFER_TIMESTAMP (outbuffer) = mad->sync_point + 
	      				 mad->total_samples * 1000000LL / mad->frame.header.samplerate;

      /* end of new bit */
      while (nsamples--) {
        /* output sample(s) in 16-bit signed native-endian PCM */
        *outdata++ = scale(*left_ch++) & 0xffff;

        if (nchannels == 2) {
          *outdata++ = scale(*right_ch++) & 0xffff;
        }
      }
      if (GST_PAD_CAPS (mad->srcpad) == NULL) {
        gst_pad_try_set_caps (mad->srcpad,
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
      	         "rate",        GST_PROPS_INT (mad->header.sfreq),
#else
      	         "rate",        GST_PROPS_INT (mad->header.samplerate),
#endif
      	         "channels",    GST_PROPS_INT (nchannels),
	         NULL)));
      }

      gst_pad_push (mad->srcpad, outbuffer);
next:
      /* figure out how many bytes mad consumed */
      consumed = mad->stream.next_frame - mad_input_buffer;

      /* move out pointer to where mad want the next data */
      mad_input_buffer += consumed;
      mad->tempsize -= consumed;
    }
    memmove (mad->tempbuffer, mad_input_buffer, mad->tempsize);
  }

  gst_buffer_unref (buffer);
}

static GstElementStateReturn
gst_mad_change_state (GstElement *element)
{
  GstMad *mad;

  mad = GST_MAD (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      mad_stream_init (&mad->stream);
      mad_frame_init (&mad->frame);
      mad_synth_init (&mad->synth);
      mad->tempsize=0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      /* do something to get out of the chain function faster */
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      mad_synth_finish (&mad->synth);
      mad_frame_finish (&mad->frame);
      mad_stream_finish (&mad->stream);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
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
