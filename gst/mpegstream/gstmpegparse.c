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


/*#define GST_DEBUG_ENABLED*/
#include "gstmpegparse.h"
#include "gstmpegclock.h"

/* elementfactory information */
static GstElementDetails mpeg_parse_details = {
  "MPEG System Parser",
  "Codec/Parser",
  "Parses MPEG1 and MPEG2 System Streams",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999",
};

#define CLASS(o)	GST_MPEG_PARSE_CLASS (G_OBJECT_GET_CLASS (o))

/* GstMPEGParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BIT_RATE,
  ARG_MPEG2,
  ARG_SYNC,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg_parse_sink",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (TRUE)
  )
);

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg_parse_src",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (TRUE),
      "parsed",       GST_PROPS_BOOLEAN (TRUE)
  )
);

static void 		gst_mpeg_parse_class_init	(GstMPEGParseClass *klass);
static void 		gst_mpeg_parse_init		(GstMPEGParse *mpeg_parse);
static GstElementStateReturn
			gst_mpeg_parse_change_state	(GstElement *element);

static void 		gst_mpeg_parse_set_clock 	(GstElement *element, GstClock *clock);
static GstClock* 	gst_mpeg_parse_get_clock 	(GstElement *element);
static GstClockTime	gst_mpeg_parse_get_time 	(GstClock *clock, gpointer data);

static gboolean		gst_mpeg_parse_parse_packhead 	(GstMPEGParse *mpeg_parse, GstBuffer *buffer);
static void 		gst_mpeg_parse_send_data	(GstMPEGParse *mpeg_parse, GstData *data, GstClockTime time);
static void 		gst_mpeg_parse_handle_discont 	(GstMPEGParse *mpeg_parse);

static void 		gst_mpeg_parse_loop 		(GstElement *element);

static void 		gst_mpeg_parse_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);
static void 		gst_mpeg_parse_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_mpeg_parse_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_mpeg_parse_get_type (void)
{
  static GType mpeg_parse_type = 0;

  if (!mpeg_parse_type) {
    static const GTypeInfo mpeg_parse_info = {
      sizeof(GstMPEGParseClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mpeg_parse_class_init,
      NULL,
      NULL,
      sizeof(GstMPEGParse),
      0,
      (GInstanceInitFunc)gst_mpeg_parse_init,
    };
    mpeg_parse_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMPEGParse", &mpeg_parse_info, 0);
  }
  return mpeg_parse_type;
}

static void
gst_mpeg_parse_class_init (GstMPEGParseClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BIT_RATE,
    g_param_spec_uint("bitrate","bitrate","bitrate",
                      0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MPEG2,
    g_param_spec_boolean ("mpeg2", "mpeg2", "is this an mpeg2 stream",
                          FALSE, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
    g_param_spec_boolean ("sync", "Sync", "Synchronize on the stream SCR",
                          FALSE, G_PARAM_READWRITE));

  gobject_class->get_property = gst_mpeg_parse_get_property;
  gobject_class->set_property = gst_mpeg_parse_set_property;

  gstelement_class->change_state = gst_mpeg_parse_change_state;

  klass->parse_packhead = gst_mpeg_parse_parse_packhead;
  klass->parse_syshead 	= NULL;
  klass->parse_packet 	= NULL;
  klass->parse_pes 	= NULL;
  klass->send_data 	= gst_mpeg_parse_send_data;
  klass->handle_discont	= gst_mpeg_parse_handle_discont;
}

static void
gst_mpeg_parse_init (GstMPEGParse *mpeg_parse)
{
  mpeg_parse->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(mpeg_parse),mpeg_parse->sinkpad);

  mpeg_parse->srcpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad(GST_ELEMENT(mpeg_parse),mpeg_parse->srcpad);
  gst_pad_set_event_function (mpeg_parse->srcpad, gst_mpeg_parse_handle_src_event);
  gst_pad_set_query_function (mpeg_parse->srcpad, gst_mpeg_parse_handle_src_query);

  gst_element_set_loop_function (GST_ELEMENT (mpeg_parse), gst_mpeg_parse_loop);

  /* initialize parser state */
  mpeg_parse->packetize = NULL;
  mpeg_parse->current_scr = 0;
  mpeg_parse->previous_scr = 0;
  mpeg_parse->sync = FALSE;

  /* zero counters (should be done at RUNNING?) */
  mpeg_parse->bit_rate = 0;
  mpeg_parse->discont_pending = FALSE;
  mpeg_parse->scr_pending = TRUE;
  mpeg_parse->provided_clock = gst_mpeg_clock_new ("MPEGParseClock", 
		  gst_mpeg_parse_get_time, mpeg_parse);

  GST_ELEMENT (mpeg_parse)->getclockfunc    = gst_mpeg_parse_get_clock;
  GST_ELEMENT (mpeg_parse)->setclockfunc    = gst_mpeg_parse_set_clock;

  GST_FLAG_SET (mpeg_parse, GST_ELEMENT_EVENT_AWARE);
}

static GstClock*
gst_mpeg_parse_get_clock (GstElement *element)
{   
  //GstMPEGParse *parse = GST_MPEG_PARSE (element);

  //return parse->provided_clock;
  return NULL;
}

static void
gst_mpeg_parse_set_clock (GstElement *element, GstClock *clock)
{   
  GstMPEGParse *parse = GST_MPEG_PARSE (element);

  parse->clock = clock;
} 

static GstClockTime
gst_mpeg_parse_get_time (GstClock *clock, gpointer data)
{   
  GstMPEGParse *parse = GST_MPEG_PARSE (data);

  return MPEGTIME_TO_GSTTIME (parse->previous_scr);
}

static void
gst_mpeg_parse_send_data (GstMPEGParse *mpeg_parse, GstData *data, GstClockTime time)
{
  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      default:
	gst_pad_event_default (mpeg_parse->sinkpad, event);
	break;
    }
  }
  else {
    if (!GST_PAD_CAPS (mpeg_parse->srcpad)) {
      gboolean mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);

      gst_pad_try_set_caps (mpeg_parse->srcpad,
		      GST_CAPS_NEW (
    			"mpeg_parse_src",
    			"video/mpeg",
    			  "mpegversion",  GST_PROPS_INT (mpeg2 ? 2 : 1),
    			  "systemstream", GST_PROPS_BOOLEAN (TRUE),
    			  "parsed",       GST_PROPS_BOOLEAN (TRUE)
			      ));
    }

    GST_BUFFER_TIMESTAMP (data) = time;
    GST_DEBUG (0, "mpeg_parse: current_scr %lld", time);

    gst_pad_push (mpeg_parse->srcpad, GST_BUFFER (data));
  }
}

static void
gst_mpeg_parse_handle_discont (GstMPEGParse *mpeg_parse)
{
  GstEvent *event;

  event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
		  MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr), NULL);

  gst_pad_push (mpeg_parse->srcpad, GST_BUFFER (event));
}

static gboolean
gst_mpeg_parse_parse_packhead (GstMPEGParse *mpeg_parse, GstBuffer *buffer)
{
  guint8 *buf;
  guint64 scr;
  guint32 scr1, scr2;
  guint32 new_rate;

  GST_DEBUG (0, "mpeg_parse: in parse_packhead");

  buf = GST_BUFFER_DATA (buffer);
  buf += 4;

  scr1 = GUINT32_FROM_BE (*(guint32*) buf);
  scr2 = GUINT32_FROM_BE (*(guint32*) (buf+4));

  if (GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize)) {

    /* :2=01 ! scr:3 ! marker:1==1 ! scr:15 ! marker:1==1 ! scr:15 */
    scr  = (scr1 & 0x38000000) << 3;
    scr |= (scr1 & 0x03fff800) << 4;
    scr |= (scr1 & 0x000003ff) << 5;
    scr |= (scr2 & 0xf8000000) >> 27;
    
    buf += 6;
    new_rate = (GUINT32_FROM_BE ((*(guint32 *) buf)) & 0xfffffc00) >> 10;
    //new_rate *= 133; /* FIXME trial and error */
    new_rate *= 223; /* FIXME trial and error */
  }
  else {
    scr  = (scr1 & 0x0e000000) << 5;
    scr |= (scr1 & 0x00fffe00) << 6;
    scr |= (scr1 & 0x000000ff) << 7;
    scr |= (scr2 & 0xfe000000) >> 25;
    
    buf += 5;
    new_rate = (GUINT32_FROM_BE ((*(guint32 *) buf)) & 0x7ffffe00) >> 9;
    new_rate *= 400;
  }

  mpeg_parse->previous_scr = mpeg_parse->current_scr;
  mpeg_parse->current_scr = scr;

  GST_DEBUG (0, "mpeg_parse: SCR is %llu (%llu)", scr, 
		  MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr));

  if (mpeg_parse->previous_scr > mpeg_parse->current_scr) {
    mpeg_parse->discont_pending = TRUE;
  }
		  
  mpeg_parse->scr_pending = FALSE;

  if (mpeg_parse->bit_rate != new_rate) {
    mpeg_parse->bit_rate = new_rate;

    g_object_notify (G_OBJECT (mpeg_parse), "bitrate");
  }

  GST_DEBUG (0, "mpeg_parse: stream is %1.3fMbs",
	     (mpeg_parse->bit_rate) / 1000000.0);

  return TRUE;
}

static void
gst_mpeg_parse_loop (GstElement *element)
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (element);
  GstData *data;
  guint id;
  gboolean mpeg2;
  GstClockTime time;

  data = gst_mpeg_packetize_read (mpeg_parse->packetize);

  id = GST_MPEG_PACKETIZE_ID (mpeg_parse->packetize);
  mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize);
    
  if (GST_IS_BUFFER (data)) {
    GstBuffer *buffer = GST_BUFFER (data);

    GST_DEBUG (0, "mpeg2demux: have chunk 0x%02X", id);

    switch (id) {
      case 0xba:
        if (CLASS (mpeg_parse)->parse_packhead) {
	  CLASS (mpeg_parse)->parse_packhead (mpeg_parse, buffer);
	}
	break;
      case 0xbb:
	if (CLASS (mpeg_parse)->parse_syshead) {
	  CLASS (mpeg_parse)->parse_syshead (mpeg_parse, buffer);
	}
	break;
      default:
        if (mpeg2 && ((id < 0xBD) || (id > 0xFE))) {
          g_warning ("mpeg2demux: ******** unknown id 0x%02X", id); 
        }
	else {
	  if (mpeg2) {
	    if (CLASS (mpeg_parse)->parse_pes) {
	      CLASS (mpeg_parse)->parse_pes (mpeg_parse, buffer);
	    }
	  }
	  else {
	    if (CLASS (mpeg_parse)->parse_packet) {
	      CLASS (mpeg_parse)->parse_packet (mpeg_parse, buffer);
	    }
	  }
        }
    }
  }

  time = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr);

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
        GST_DEBUG (GST_CAT_EVENT, "event: %d\n", GST_EVENT_TYPE (data));

        mpeg_parse->discont_pending = TRUE;
        mpeg_parse->scr_pending = TRUE;
        mpeg_parse->packetize->resync = TRUE;
	gst_event_free (event);
	return;
      default:
	break;
    }
    if (CLASS (mpeg_parse)->send_data)
      CLASS (mpeg_parse)->send_data (mpeg_parse, data, time);
  }
  else {
    guint64 size;
	    
    /* we're not sending data as long as no new SCR was found */
    if (mpeg_parse->discont_pending) {
      if (!mpeg_parse->scr_pending) {
        if (mpeg_parse->clock && mpeg_parse->sync) {
          gst_clock_handle_discont (mpeg_parse->clock, MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr));
        }
	if (CLASS (mpeg_parse)->handle_discont) {
	  CLASS (mpeg_parse)->handle_discont (mpeg_parse);
	}
        mpeg_parse->discont_pending = FALSE;
      }
      else {
	GST_DEBUG (0, "waiting for SCR\n");
      }
      gst_buffer_unref (GST_BUFFER (data));
      return;
    }

    if (CLASS (mpeg_parse)->send_data)
      CLASS (mpeg_parse)->send_data (mpeg_parse, data, time);

    if (mpeg_parse->clock && mpeg_parse->sync && !mpeg_parse->discont_pending) {
      GST_DEBUG (GST_CAT_CLOCK, "syncing mpegparse");
      gst_element_clock_wait (GST_ELEMENT (mpeg_parse), mpeg_parse->clock, time, NULL);
    }

    size = GST_BUFFER_SIZE (data);
    
    /* we are interpolating the scr here */
    /* mpeg_parse->current_scr += ((size * 90000LL) / (mpeg_parse->bit_rate)); */
  }
}

gboolean
gst_mpeg_parse_handle_src_query (GstPad *pad, GstPadQueryType type, 
			         GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
	{
          GstFormat peer_format;
	  gint64 peer_value;

	  if (mpeg_parse->bit_rate == 0) 
	    return FALSE;

	  peer_format = GST_FORMAT_BYTES;
	  if (gst_pad_query (GST_PAD_PEER (mpeg_parse->sinkpad),
			     GST_PAD_QUERY_TOTAL, &peer_format, &peer_value)) 
	  {
            /* multiply bywith 8 because vbr is in bits/second */
            *value = peer_value * 8 * GST_SECOND / mpeg_parse->bit_rate;
          }
	  else 
	    res = FALSE;
	  break;
	}
	default:
	  res = FALSE;
	  break;
      }
      break;
    }
    case GST_PAD_QUERY_POSITION:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
          *value = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr);
	  break;
	default:
	  res = FALSE;
	  break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

gboolean
gst_mpeg_parse_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      guint64 desired_offset;

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_TIME) {
        return FALSE;
      }

      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH) {
      }
      desired_offset = mpeg_parse->bit_rate * GST_EVENT_SEEK_OFFSET (event) / (8 * GST_SECOND);

      if (!gst_bytestream_seek (mpeg_parse->packetize->bs, desired_offset, GST_SEEK_METHOD_SET)) {
	return FALSE;
      }
      mpeg_parse->discont_pending = TRUE;
      mpeg_parse->scr_pending = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

static GstElementStateReturn
gst_mpeg_parse_change_state (GstElement *element) 
{
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      if (!mpeg_parse->packetize) {
        mpeg_parse->packetize = gst_mpeg_packetize_new (mpeg_parse->sinkpad, GST_MPEG_PACKETIZE_SYSTEM);
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (mpeg_parse->packetize) {
        gst_mpeg_packetize_destroy (mpeg_parse->packetize);
        mpeg_parse->packetize = NULL;
      }
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static void
gst_mpeg_parse_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMPEGParse *mpeg_parse;

  /* it's not null if we got it, but it might not be ours */
  mpeg_parse = GST_MPEG_PARSE(object);

  switch (prop_id) {
    case ARG_BIT_RATE: 
      g_value_set_uint (value, mpeg_parse->bit_rate); 
      break;
    case ARG_MPEG2:
      if (mpeg_parse->packetize)
        g_value_set_boolean (value, GST_MPEG_PACKETIZE_IS_MPEG2 (mpeg_parse->packetize));
      else
        g_value_set_boolean (value, FALSE);
      break;
    case ARG_SYNC: 
      g_value_set_boolean (value, mpeg_parse->sync);
      break;
    default: 
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeg_parse_set_property (GObject *object, guint prop_id, 
			     const GValue *value, GParamSpec *pspec)
{
  GstMPEGParse *mpeg_parse;

  /* it's not null if we got it, but it might not be ours */
  mpeg_parse = GST_MPEG_PARSE(object);

  switch (prop_id) {
    case ARG_SYNC: 
      mpeg_parse->sync = g_value_get_boolean (value); 
      break;
    default: 
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
gst_mpeg_parse_plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* this filter needs the bytestream package */
  if (!gst_library_load("gstbytestream")) {
    gst_info("mpeg_parse:: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }

  /* create an elementfactory for the mpeg_parse element */
  factory = gst_element_factory_new("mpegparse",GST_TYPE_MPEG_PARSE,
                                   &mpeg_parse_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
