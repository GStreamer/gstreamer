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
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad, *srcpad;

  /* state */
  struct mad_stream stream;
  struct mad_frame frame;
  struct mad_synth synth;
  guchar 	*tempbuffer;
  glong 	 tempsize;
  gboolean	 need_sync;
  guint64	 base_time;
  guint64	 framestamp;	/* timestamp-like, but counted in frames */
  guint64	 total_samples; /* the number of samples since the sync point */

  gboolean	 restart;

  /* info */
  struct mad_header header;
  gboolean	 new_header;
  gboolean	 can_seek;
  gint		 channels;
  guint		 framecount;
  gint		 vbr_average; /* average bitrate */
  guint64	 vbr_rate; /* average * framecount */

  /* caps */
  gboolean	 caps_set;
};

struct _GstMadClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_mad_details = {
  "mad mp3 decoder",
  "Codec/Audio/Decoder",
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
  ARG_LAYER,
  ARG_MODE,
  ARG_EMPHASIS,
  ARG_BITRATE,
  ARG_SAMPLERATE,
  ARG_CHANNELS,
  ARG_AVERAGE_BITRATE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (mad_src_template_factory,
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

GST_PAD_TEMPLATE_FACTORY (mad_sink_template_factory,
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

static void		gst_mad_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void		gst_mad_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static gboolean 	gst_mad_src_event 	(GstPad *pad, GstEvent *event);
static const GstFormat* gst_mad_get_formats 	(GstPad *pad);
static const GstEventMask* 
			gst_mad_get_event_masks (GstPad *pad);
static const GstPadQueryType* 
			gst_mad_get_query_types (GstPad *pad);

static gboolean 	gst_mad_src_query 	(GstPad *pad, GstPadQueryType type,
		  				 GstFormat *format, gint64 *value);
static gboolean 	gst_mad_convert_sink 	(GstPad *pad, GstFormat src_format, gint64 src_value, 
		     				 GstFormat *dest_format, gint64 *dest_value);
static gboolean 	gst_mad_convert_src 	(GstPad *pad, GstFormat src_format, gint64 src_value, 
		     				 GstFormat *dest_format, gint64 *dest_value);

static void 		gst_mad_chain 		(GstPad *pad, GstBuffer *buffer);

static GstElementStateReturn
			gst_mad_change_state (GstElement *element);


static GstElementClass *parent_class = NULL;
/* static guint gst_mad_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mad_get_type (void)
{
  static GType mad_type = 0;

  if (!mad_type) {
    static const GTypeInfo mad_info = {
      sizeof(GstMadClass),      
      NULL,
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

#define GST_TYPE_MAD_LAYER (gst_mad_layer_get_type())
static GType
gst_mad_layer_get_type(void) {
  static GType mad_layer_type = 0;
  static GEnumValue mad_layer[] = {
    {0,             "0", "Unknown"},
    {MAD_LAYER_I,   "1", "I"},
    {MAD_LAYER_II,  "2", "II"},
    {MAD_LAYER_III, "3", "III"},
    {0, NULL, NULL},
  };
  if (!mad_layer_type) {
    mad_layer_type = g_enum_register_static("GstMadLayer", mad_layer);
  }
  return mad_layer_type;
}

#define GST_TYPE_MAD_CHANNELS (gst_mad_channels_get_type())
static GType
gst_mad_channels_get_type(void) {
  static GType mad_channels_type = 0;
  static GEnumValue mad_channels[] = {
    {0, "0", "Unknown"},
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    { 0, NULL, NULL},
  };
  if (!mad_channels_type) {
    mad_channels_type = g_enum_register_static("GstMadChannels", mad_channels);
  }
  return mad_channels_type;
}

#define GST_TYPE_MAD_MODE (gst_mad_mode_get_type())
static GType
gst_mad_mode_get_type(void) {
  static GType mad_mode_type = 0;
  static GEnumValue mad_mode[] = {
    {-1,                     "-1", "Unknown"},
    {MAD_MODE_SINGLE_CHANNEL, "0", "Single Channel"},
    {MAD_MODE_DUAL_CHANNEL  , "1", "Dual Channel"},
    {MAD_MODE_JOINT_STEREO  , "2", "Joint Stereo"},
    {MAD_MODE_STEREO        , "3", "Stereo"},
    { 0, NULL, NULL},
  };
  if (!mad_mode_type) {
    mad_mode_type = g_enum_register_static("GstMadMode", mad_mode);
  }
  return mad_mode_type;
}

#define GST_TYPE_MAD_EMPHASIS (gst_mad_emphasis_get_type())
static GType
gst_mad_emphasis_get_type(void) {
  static GType mad_emphasis_type = 0;
  static GEnumValue mad_emphasis[] = {
    {-1,                     "-1", "Unknown"},
    {MAD_EMPHASIS_NONE,       "0", "None"},
    {MAD_EMPHASIS_50_15_US,   "1", "50/15 Microseconds"},
    {MAD_EMPHASIS_CCITT_J_17, "2", "CCITT J.17"},
    { 0, NULL, NULL},
  };
  if (!mad_emphasis_type) {
    mad_emphasis_type = g_enum_register_static("GstMadEmphasis", mad_emphasis);
  }
  return mad_emphasis_type;
}

static void
gst_mad_class_init (GstMadClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mad_set_property;
  gobject_class->get_property = gst_mad_get_property;
  gobject_class->dispose = gst_mad_dispose;

  gstelement_class->change_state = gst_mad_change_state;
  
  /* init properties */
  /* currently, string representations are used, we might want to change that */
  /* FIXME: descriptions need to be more technical, default values and ranges need to be selected right */
  g_object_class_install_property (gobject_class, ARG_LAYER,
    g_param_spec_enum ("layer", "Layer", "The audio MPEG Layer this stream is encoded in",
			GST_TYPE_MAD_LAYER, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_MODE,
    g_param_spec_enum ("mode", "Mode", "The current mode of the channels",
		       GST_TYPE_MAD_MODE, -1, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_EMPHASIS,
    g_param_spec_enum ("emphasis", "Emphasis", "Emphasis",
		       GST_TYPE_MAD_EMPHASIS, -1, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE,
    g_param_spec_int ("bitrate", "Bitrate", "current bitrate of the stream",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_AVERAGE_BITRATE,
    g_param_spec_int ("average-bitrate", "average bitrate", "average bitrate of the stream",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_SAMPLERATE,
    g_param_spec_int ("samplerate", "Samplerate", "current samplerate of the stream",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CHANNELS,
    g_param_spec_enum ("channels", "Channels", "number of channels",
                       GST_TYPE_MAD_CHANNELS, 0, G_PARAM_READABLE));
}

static void
gst_mad_init (GstMad *mad)
{
  /* create the sink and src pads */
  mad->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (mad_sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mad), mad->sinkpad);
  gst_pad_set_chain_function (mad->sinkpad, GST_DEBUG_FUNCPTR (gst_mad_chain));
  gst_pad_set_convert_function (mad->sinkpad, GST_DEBUG_FUNCPTR (gst_mad_convert_sink));
  gst_pad_set_formats_function (mad->sinkpad, GST_DEBUG_FUNCPTR (gst_mad_get_formats));

  mad->srcpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (mad_src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (mad), mad->srcpad);
  gst_pad_set_event_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_src_event));
  gst_pad_set_event_mask_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_get_event_masks));
  gst_pad_set_query_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_src_query));
  gst_pad_set_query_type_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_get_query_types));
  gst_pad_set_convert_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_convert_src));
  gst_pad_set_formats_function (mad->srcpad, GST_DEBUG_FUNCPTR (gst_mad_get_formats));

  mad->tempbuffer = g_malloc (MAD_BUFFER_MDLEN * 3);
  mad->tempsize = 0;
  mad->need_sync = TRUE;
  mad->base_time = 0;
  mad->framestamp = 0;
  mad->total_samples = 0;
  mad->new_header = TRUE;
  mad->framecount = 0;
  mad->vbr_average = 0;
  mad->vbr_rate = 0;
  mad->restart = FALSE;
  mad->header.mode = -1;
  mad->header.emphasis = -1;

  GST_FLAG_SET (mad, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_mad_dispose (GObject *object)
{
  GstMad *mad = GST_MAD (object);

  G_OBJECT_CLASS (parent_class)->dispose (object);

  g_free (mad->tempbuffer);
}

static const GstFormat*
gst_mad_get_formats (GstPad *pad)
{
  static const GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_UNITS,
    GST_FORMAT_TIME,
    0
  };
  static const GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  
  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

static const GstEventMask*
gst_mad_get_event_masks (GstPad *pad)
{
  static const GstEventMask gst_mad_src_event_masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET |
                      GST_SEEK_FLAG_FLUSH },
    { 0, }
  };
  return gst_mad_src_event_masks;
}

static gboolean
gst_mad_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value, 
		      GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstMad *mad;

  mad = GST_MAD (gst_pad_get_parent (pad));

  if (mad->vbr_average == 0)
    return FALSE;
  
  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
          /* multiply by 8 because vbr is in bits/second */
          *dest_value = src_value * 8 * GST_SECOND / mad->vbr_average;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
          /* multiply by 8 because vbr is in bits/second */
          *dest_value = src_value * mad->vbr_average / (8 * GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static gboolean
gst_mad_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value, 
		     GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstMad *mad;

  mad = GST_MAD (gst_pad_get_parent (pad));
  
  bytes_per_sample = MAD_NCHANNELS (&mad->frame.header) << 1;
  
  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_UNITS:
	  if (bytes_per_sample == 0)
            return FALSE;
	  *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	{
          gint byterate = bytes_per_sample * mad->frame.header.samplerate;

	  if (byterate == 0)
            return FALSE;
	  *dest_value = src_value * GST_SECOND / byterate;
          break;
	}
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * bytes_per_sample;
	  break;
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  if (mad->frame.header.samplerate == 0)
            return FALSE;
	  *dest_value = src_value * GST_SECOND / mad->frame.header.samplerate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  scale = bytes_per_sample;
	  /* fallthrough */
        case GST_FORMAT_UNITS:
	  *dest_value = src_value * scale * mad->frame.header.samplerate / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

static const GstPadQueryType*
gst_mad_get_query_types (GstPad *pad)
{
  static const GstPadQueryType gst_mad_src_query_types[] = {
    GST_PAD_QUERY_TOTAL,
    GST_PAD_QUERY_POSITION,
    0
  };
  return gst_mad_src_query_types;
}

static gboolean
gst_mad_src_query (GstPad *pad, GstPadQueryType type,
		   GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstMad *mad;
 
  mad = GST_MAD (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
	case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
	  /* fallthrough */
	case GST_FORMAT_BYTES:
	case GST_FORMAT_UNITS:
	case GST_FORMAT_TIME:
        {
	  gint64 peer_value;
          const GstFormat *peer_formats;

	  res = FALSE;

	  peer_formats = gst_pad_get_formats (GST_PAD_PEER (mad->sinkpad));

	  while (peer_formats && *peer_formats && !res) {

	    GstFormat peer_format = *peer_formats;

	    /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (mad->sinkpad), GST_PAD_QUERY_TOTAL,
			       &peer_format, &peer_value)) 
	    {
              GstFormat conv_format;
	      /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
	      res = gst_pad_convert (mad->sinkpad,
				peer_format, peer_value,
				&conv_format, value);
	      /* and to final format */
	      res &= gst_pad_convert (pad,
			GST_FORMAT_TIME, *value,
			format, value);
	    }
	    peer_formats++;
	  }
	  break;
	}
	default:
	  res = FALSE;
	  break;
      }
      break;
    }
    case GST_PAD_QUERY_POSITION:
      switch (*format) {
	case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
	  /* fall through */
	default:
	{
          GstFormat time_format;
	  gint64 samples;

          time_format = GST_FORMAT_UNITS;
	  res = gst_pad_convert (pad,
			GST_FORMAT_TIME, mad->base_time,
			&time_format, &samples);
	  /* we only know about our samples, convert to requested format */
	  res &= gst_pad_convert (pad,
			  GST_FORMAT_UNITS, mad->total_samples + samples,
			  format, value);
	  break;
	}
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_mad_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstMad *mad;
	    
  mad = GST_MAD (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gint64 src_offset;
      gboolean flush;
      GstFormat format;
      const GstFormat *peer_formats;
	  
      format = GST_FORMAT_TIME;

      /* first bring the src_format to TIME */
      if (!gst_pad_convert (pad,
    		GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
    		&format, &src_offset))
      {
	/* didn't work, probably unsupported seek format then */
        res = FALSE;
        break;
      }

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      
      /* assume the worst */
      res = FALSE;

      peer_formats = gst_pad_get_formats (GST_PAD_PEER (mad->sinkpad));

      /* while we did not exhaust our seek formats without result */
      while (peer_formats && *peer_formats && !res) {
        gint64 desired_offset;
	
	format = *peer_formats;

        /* try to convert requested format to one we can seek with on the sinkpad */
        if (gst_pad_convert (mad->sinkpad, GST_FORMAT_TIME, src_offset, &format, &desired_offset)) 
        {
          GstEvent *seek_event;

	  /* conversion succeeded, create the seek */
          seek_event = gst_event_new_seek (format | GST_SEEK_METHOD_SET | flush, desired_offset);
	  /* do the seek */
          if (gst_pad_send_event (GST_PAD_PEER (mad->sinkpad), seek_event)) {
	    /* seek worked, we're done, loop will exit */
	    res = TRUE;
	  }
        }
	/* at this point, either the seek worked or res == FALSE */
	if (res)
          /* we need to break out of the processing loop on flush */
          mad->restart = flush;

	peer_formats++;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  gst_event_unref (event);
  return res;
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

/* do we need this function? */
static void
gst_mad_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMad *mad;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MAD (object));
	      
  mad = GST_MAD (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_mad_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMad *mad;
	    
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MAD (object));
	      
  mad = GST_MAD (object);

  switch (prop_id) {
    case ARG_LAYER:
      g_value_set_enum (value, mad->header.layer);
      break;
    case ARG_MODE:
      g_value_set_enum (value, mad->header.mode);
      break;
    case ARG_EMPHASIS:
      g_value_set_enum (value, mad->header.emphasis);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, mad->header.bitrate);
      break;
    case ARG_AVERAGE_BITRATE:
      g_value_set_int (value, mad->vbr_average);
      break;
    case ARG_SAMPLERATE:
      g_value_set_int (value, mad->header.samplerate);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, mad->channels);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mad_update_info (GstMad *mad)
{
  gint abr = mad->vbr_average;
  struct mad_header *header = &mad->frame.header;

#define CHECK_HEADER(h1,str) 				\
G_STMT_START{							\
  if (mad->header.h1 != header->h1 || mad->new_header) {	\
    mad->header.h1 = header->h1;				\
    g_object_notify (G_OBJECT (mad), str);			\
  };								\
} G_STMT_END
  
  g_object_freeze_notify (G_OBJECT (mad));

  /* update average bitrate */
  if (mad->new_header) {
    mad->framecount = 1;
    mad->vbr_rate = header->bitrate;
    abr = 0;
  } else {
    mad->framecount++;
    mad->vbr_rate += header->bitrate;
  }
  mad->vbr_average = (gint) (mad->vbr_rate / mad->framecount);
  if (abr != mad->vbr_average) {
    g_object_notify (G_OBJECT (mad), "average_bitrate");
  }

  CHECK_HEADER (layer, 	    "layer");
  CHECK_HEADER (mode, 	    "mode");
  CHECK_HEADER (emphasis,   "emphasis");
  CHECK_HEADER (bitrate,    "bitrate");
  CHECK_HEADER (samplerate, "samplerate");
  if (mad->channels != MAD_NCHANNELS (header) || mad->new_header) {
    mad->channels = MAD_NCHANNELS (header);
    g_object_notify (G_OBJECT (mad), "channels");
  }
  mad->new_header = FALSE;
  
  g_object_thaw_notify (G_OBJECT (mad));
  
#undef CHECK_HEADER
}

static void
gst_mad_chain (GstPad *pad, GstBuffer *buffer)
{
  GstMad *mad;
  gchar *data;
  glong size;

  mad = GST_MAD (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buffer)) {
    GstEvent *event = GST_EVENT (buffer);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
	gint n = GST_EVENT_DISCONT_OFFSET_LEN (event);
	gint i;

	if (!GST_PAD_IS_USABLE (mad->srcpad))
          return;

	for (i=0; i<n; i++) {
	  if (gst_pad_handles_format (pad, GST_EVENT_DISCONT_OFFSET(event,i).format))
	  {
            gint64 value = GST_EVENT_DISCONT_OFFSET (event, i).value;
	    gint64 time;
	    GstFormat format;
            GstEvent *discont;

	    /* see how long the input bytes take */
	    format = GST_FORMAT_TIME;
	    if (!gst_pad_convert (pad,
			GST_EVENT_DISCONT_OFFSET (event, i).format, value,
			&format, &time))
	    {
	      time = 0;
	    }

	    mad->base_time = time;

            gst_event_unref (event);
	    discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time, NULL);
            gst_pad_push (mad->srcpad, GST_BUFFER (discont));
	    break;
	  }
	}
        mad->total_samples = 0;
        mad->tempsize = 0;
	/* we don't need to restart when we get here */
        mad->restart = FALSE;
	break;
      }
      default:
	gst_pad_event_default (pad, event);
	break;
    }
    return;
  }

  if (GST_BUFFER_TIMESTAMP (buffer) != -1) {
    mad->base_time = GST_BUFFER_TIMESTAMP (buffer);
    mad->total_samples = 0;
  }

  /* end of new bit */
  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

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

    /* if we have data we can try to proceed */
    while (mad->tempsize >= 0) {
      gint consumed;
      guint nchannels, nsamples;
      mad_fixed_t const *left_ch, *right_ch;
      GstBuffer *outbuffer;
      gint16 *outdata;

      mad_stream_buffer (&mad->stream, mad_input_buffer, mad->tempsize);

      if (mad_frame_decode (&mad->frame, &mad->stream) == -1) {
	/* not enough data, need to wait for next buffer? */
	if (mad->stream.error == MAD_ERROR_BUFLEN) {
	  break;
	} 
        if (!MAD_RECOVERABLE (mad->stream.error)) {
          gst_element_error (GST_ELEMENT (mad), "fatal error decoding stream");
          return;
        }
	mad_frame_mute (&mad->frame);
	mad_synth_mute (&mad->synth);
	mad_stream_sync (&mad->stream);
	/* recoverable errors pass */
	goto next;
      }

      mad_synth_frame (&mad->synth, &mad->frame);

      nchannels = MAD_NCHANNELS (&mad->frame.header);
      nsamples  = mad->synth.pcm.length;
      left_ch   = mad->synth.pcm.samples[0];
      right_ch  = mad->synth.pcm.samples[1];

      /* at this point we can accept seek events */
      mad->can_seek = TRUE;

      gst_mad_update_info (mad);

      outbuffer = gst_buffer_new ();
      outdata = (gint16 *) GST_BUFFER_DATA (outbuffer) = g_malloc (nsamples * nchannels * 2);
      GST_BUFFER_SIZE (outbuffer) = nsamples * nchannels * 2;


      if (mad->frame.header.samplerate == 0) {
	g_warning ("mad->frame.header.samplerate is 0 !");
      }
      else {
        GST_BUFFER_TIMESTAMP (outbuffer) = mad->base_time + 
			 mad->total_samples * GST_SECOND / mad->frame.header.samplerate;
      }

      mad->total_samples += nsamples;

      /* end of new bit */
      while (nsamples--) {
        /* output sample(s) in 16-bit signed native-endian PCM */
        *outdata++ = scale(*left_ch++) & 0xffff;

        if (nchannels == 2) {
          *outdata++ = scale(*right_ch++) & 0xffff;
        }
      }
      if (mad->caps_set == FALSE) {
        if (!gst_pad_try_set_caps (mad->srcpad,
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
                  NULL)))) {
          gst_element_error (GST_ELEMENT (mad), "could not set caps on source pad, aborting...");
        }
        mad->caps_set = TRUE;
      }

      if (GST_PAD_IS_CONNECTED (mad->srcpad)) {
        gst_pad_push (mad->srcpad, outbuffer);
      }
      else {
	gst_buffer_unref (outbuffer);
      }

      if (mad->restart) {
	mad->restart = FALSE;
	mad->tempsize = 0;
	goto end;
      }

next:

      /* figure out how many bytes mad consumed */
      consumed = mad->stream.next_frame - mad_input_buffer;

      /* move out pointer to where mad want the next data */
      mad_input_buffer += consumed;
      mad->tempsize -= consumed;
    }
    memmove (mad->tempbuffer, mad_input_buffer, mad->tempsize);
  }

end:
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
      mad->tempsize = 0;
      mad->can_seek = FALSE;
      mad->total_samples = 0;
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
      mad->vbr_average = 0;
      mad->frame.header.samplerate = 0;
      mad->can_seek = FALSE;
      mad->restart = TRUE;
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
  factory = gst_element_factory_new ("mad", GST_TYPE_MAD, &gst_mad_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (mad_sink_template_factory));
  gst_element_factory_add_pad_template (factory, 
		  GST_PAD_TEMPLATE_GET (mad_src_template_factory));
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mad",
  plugin_init
};
