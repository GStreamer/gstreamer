/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <errno.h>
#include <unistd.h>

#include "gstosssink.h"

/* elementfactory information */
static GstElementDetails gst_osssink_details = GST_ELEMENT_DETAILS (
  "Audio Sink (OSS)",
  "Sink/Audio",
  "Output to a sound card via OSS",
  "Erik Walthinsen <omega@cse.ogi.edu>, "
  "Wim Taymans <wim.taymans@chello.be>"
);

static void 			gst_osssink_base_init		(gpointer g_class);
static void 			gst_osssink_class_init		(GstOssSinkClass *klass);
static void 			gst_osssink_init		(GstOssSink *osssink);
static void 			gst_osssink_dispose		(GObject *object);

static GstElementStateReturn 	gst_osssink_change_state	(GstElement *element);
static void		 	gst_osssink_set_clock		(GstElement *element, GstClock *clock);
static GstClock* 		gst_osssink_get_clock 		(GstElement *element);
static GstClockTime 		gst_osssink_get_time 		(GstClock *clock, gpointer data);

static const GstFormat* 	gst_osssink_get_formats 	(GstPad *pad);
static gboolean 		gst_osssink_convert 		(GstPad *pad, GstFormat src_format, gint64 src_value,
	            						 GstFormat *dest_format, gint64 *dest_value);
static const GstQueryType* 	gst_osssink_get_query_types 	(GstPad *pad);
static gboolean 		gst_osssink_query 		(GstElement *element, GstQueryType type, 
								 GstFormat *format, gint64 *value);
static gboolean 		gst_osssink_sink_query 		(GstPad *pad, GstQueryType type,
								 GstFormat *format, gint64 *value);

static GstPadLinkReturn		gst_osssink_sinkconnect		(GstPad *pad, GstCaps *caps);

static void 			gst_osssink_set_property	(GObject *object, guint prop_id, const GValue *value, 
		  						 GParamSpec *pspec);
static void 			gst_osssink_get_property	(GObject *object, guint prop_id, GValue *value, 
								 GParamSpec *pspec);

static void 			gst_osssink_chain		(GstPad *pad,GstData *_data);

/* OssSink signals and args */
enum {
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_FRAGMENT,
  ARG_BUFFER_SIZE,
  ARG_SYNC,
  ARG_CHUNK_SIZE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (osssink_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "osssink_sink",
    "audio/x-raw-int",
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_LIST (
         	      GST_PROPS_BOOLEAN (FALSE),
     		      GST_PROPS_BOOLEAN (TRUE)
      		    ),
      "width",      GST_PROPS_LIST (
         	      GST_PROPS_INT (8),
     		      GST_PROPS_INT (16)
      		    ),
      "depth",      GST_PROPS_LIST (
    		      GST_PROPS_INT (8),
     		      GST_PROPS_INT (16)
     		    ),
      "rate",       GST_PROPS_INT_RANGE (1000, 48000),
      "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
);

static GstElementClass *parent_class = NULL;
static guint gst_osssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_osssink_get_type (void) 
{
  static GType osssink_type = 0;

  if (!osssink_type) {
    static const GTypeInfo osssink_info = {
      sizeof(GstOssSinkClass),
      gst_osssink_base_init,
      NULL,
      (GClassInitFunc)gst_osssink_class_init,
      NULL,
      NULL,
      sizeof(GstOssSink),
      0,
      (GInstanceInitFunc)gst_osssink_init,
    };
    osssink_type = g_type_register_static (GST_TYPE_OSSELEMENT, "GstOssSink", &osssink_info, 0);
  }

  return osssink_type;
}

static GstBufferPool*
gst_osssink_get_bufferpool (GstPad *pad)
{
  GstOssSink *oss;
  
  oss = GST_OSSSINK (gst_pad_get_parent(pad));

  /* 6 buffers per chunk by default */
  if (!oss->sinkpool)
    oss->sinkpool = gst_buffer_pool_get_default (oss->bufsize, 6);

  return oss->sinkpool;
}

static void
gst_osssink_dispose (GObject *object)
{
  GstOssSink *osssink = (GstOssSink *) object;

  gst_object_unparent (GST_OBJECT (osssink->provided_clock));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_osssink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_osssink_details);
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (osssink_sink_factory));
}
static void
gst_osssink_class_init (GstOssSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_OSSELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MUTE,
    g_param_spec_boolean ("mute", "Mute", "Mute the audio",
                          FALSE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SYNC,
    g_param_spec_boolean ("sync", "Sync", "If syncing on timestamps should be enabled",
                          TRUE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAGMENT,
    g_param_spec_int ("fragment", "Fragment", 
	    	      "The fragment as 0xMMMMSSSS (MMMM = total fragments, 2^SSSS = fragment size)",
                      0, G_MAXINT, 6, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFFER_SIZE,
    g_param_spec_uint ("buffer_size", "Buffer size", "Size of buffers in osssink's bufferpool (bytes)",
                       0, G_MAXINT, 4096, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CHUNK_SIZE,
    g_param_spec_uint ("chunk_size", "Chunk size", "Write data in chunk sized buffers",
                       0, G_MAXUINT, 4096, G_PARAM_READWRITE));

  gst_osssink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstOssSinkClass, handoff), NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  
  gobject_class->set_property = gst_osssink_set_property;
  gobject_class->get_property = gst_osssink_get_property;
  gobject_class->dispose      = gst_osssink_dispose;
  
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_osssink_change_state);
  gstelement_class->query 	 = GST_DEBUG_FUNCPTR (gst_osssink_query);
  gstelement_class->set_clock 	 = gst_osssink_set_clock;
  gstelement_class->get_clock 	 = gst_osssink_get_clock;
  
}

static void 
gst_osssink_init (GstOssSink *osssink) 
{
  osssink->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (osssink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (osssink), osssink->sinkpad);
  gst_pad_set_link_function (osssink->sinkpad, gst_osssink_sinkconnect);
  gst_pad_set_bufferpool_function (osssink->sinkpad, gst_osssink_get_bufferpool);
  gst_pad_set_convert_function (osssink->sinkpad, gst_osssink_convert);
  gst_pad_set_query_function (osssink->sinkpad, gst_osssink_sink_query);
  gst_pad_set_query_type_function (osssink->sinkpad, gst_osssink_get_query_types);
  gst_pad_set_formats_function (osssink->sinkpad, gst_osssink_get_formats);

  gst_pad_set_chain_function (osssink->sinkpad, gst_osssink_chain);

  osssink->bufsize = 4096;
  osssink->chunk_size = 4096;
  osssink->resync = FALSE;
  osssink->mute = FALSE;
  osssink->sync = TRUE;
  osssink->sinkpool = NULL;
  osssink->provided_clock = gst_audio_clock_new ("ossclock", gst_osssink_get_time, osssink);
  gst_object_set_parent (GST_OBJECT (osssink->provided_clock), GST_OBJECT (osssink));
  osssink->handled = 0;

  GST_FLAG_SET (osssink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (osssink, GST_ELEMENT_EVENT_AWARE);
}


static GstPadLinkReturn 
gst_osssink_sinkconnect (GstPad *pad, GstCaps *caps) 
{
  GstOssSink *osssink = GST_OSSSINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  if (!gst_osselement_parse_caps (GST_OSSELEMENT (osssink), caps))
    return GST_PAD_LINK_REFUSED;

  if (!gst_osselement_sync_parms (GST_OSSELEMENT (osssink))) {
    return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

static inline gint64 
gst_osssink_get_delay (GstOssSink *osssink) 
{
  gint delay = 0;

  if (GST_OSSELEMENT (osssink)->fd == -1)
    return 0;

  if (ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_GETODELAY, &delay) < 0) {
    audio_buf_info info;
    if (ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_GETOSPACE, &info) < 0) {
      delay = 0;
    }
    else {
      delay = (info.fragstotal * info.fragsize) - info.bytes;	  
    }
  }
  return delay;
}

static GstClockTime 
gst_osssink_get_time (GstClock *clock, gpointer data) 
{
  GstOssSink *osssink = GST_OSSSINK (data);
  gint delay;
  GstClockTime res;

  if (!GST_OSSELEMENT (osssink)->bps)
    return 0;

  delay = gst_osssink_get_delay (osssink);

  /* sometimes delay is bigger than the number of bytes sent to the device, 
   * which screws up this calculation, we assume that everything is still 
   * in the device then */
  if (((guint64)delay) > osssink->handled) {
    delay = osssink->handled;
  }
  res =  (osssink->handled - delay) * GST_SECOND / GST_OSSELEMENT (osssink)->bps;

  return res;
}

static GstClock*
gst_osssink_get_clock (GstElement *element)
{
  GstOssSink *osssink;
	    
  osssink = GST_OSSSINK (element);

  return GST_CLOCK (osssink->provided_clock);
}

static void
gst_osssink_set_clock (GstElement *element, GstClock *clock)
{
  GstOssSink *osssink;
  
  osssink = GST_OSSSINK (element);

  osssink->clock = clock;  
}

static void 
gst_osssink_chain (GstPad *pad, GstData *_data) 
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstOssSink *osssink;
  GstClockTime buftime;

  /* this has to be an audio buffer */
  osssink = GST_OSSSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_SYNC);
	gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock), FALSE);
	gst_pad_event_default (pad, event);
        return;
      case GST_EVENT_DISCONTINUOUS:
      {
	gint64 value;

        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_RESET);
	if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
          if (!gst_clock_handle_discont (osssink->clock, value))
	    gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock), FALSE);
	  osssink->handled = 0;
	}
	osssink->resync = TRUE;

        break;
      }
      default:
	gst_pad_event_default (pad, event);
        return;
    }
    gst_event_unref (event);
    return;
  }

  if (!GST_OSSELEMENT (osssink)->bps) {
    gst_buffer_unref (buf);
    gst_element_error (GST_ELEMENT (osssink), "capsnego was never performed, unknown data type");
    return;
  }

  buftime = GST_BUFFER_TIMESTAMP (buf);

  if (GST_OSSELEMENT (osssink)->fd >= 0) {
    if (!osssink->mute) {
      guchar *data = GST_BUFFER_DATA (buf);
      gint size = GST_BUFFER_SIZE (buf);
      gint to_write = 0;

      if (osssink->clock) {
        gint delay = 0;
	gint64 queued;
    
	delay = gst_osssink_get_delay (osssink);
	queued = delay * GST_SECOND / GST_OSSELEMENT (osssink)->bps;

	if  (osssink->resync && osssink->sync) {
          gst_audio_clock_set_active ((GstAudioClock*)osssink->provided_clock, TRUE);
          gst_clock_handle_discont (osssink->clock, buftime - queued);
	  to_write = size;
	  osssink->resync = FALSE;
	}
	else {
	  to_write = size;
        }
      }
      /* no clock, try to be as fast as possible */
      else {
        audio_buf_info ospace;

        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_GETOSPACE, &ospace);

        if (ospace.bytes >= size) {
	  to_write = size;
	}
      }

      while (to_write > 0) {
        gint done = write (GST_OSSELEMENT (osssink)->fd, data, 
                           MIN (to_write, osssink->chunk_size));

        if (done == -1) {
          if (errno != EINTR)
	    break;
	}
	else {
	  to_write -= done;
	  data += done;
	  osssink->handled += done;
	}
      }
    }
  }

  gst_audio_clock_update_time ((GstAudioClock*)osssink->provided_clock, buftime);

  gst_buffer_unref (buf);
}

static const GstFormat*
gst_osssink_get_formats (GstPad *pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    GST_FORMAT_BYTES,
    0
  };
  return formats;
}

static gboolean
gst_osssink_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
	             GstFormat *dest_format, gint64 *dest_value)
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (gst_pad_get_parent (pad));
  
  return gst_osselement_convert (GST_OSSELEMENT (osssink),
				 src_format, src_value,
		                 dest_format, dest_value);
}

static const GstQueryType*
gst_osssink_get_query_types (GstPad *pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_LATENCY,
    GST_QUERY_POSITION,
    0,
  };
  return query_types;
}

static gboolean
gst_osssink_sink_query (GstPad *pad, GstQueryType type, GstFormat *format, gint64 *value) 
{
  gboolean res = TRUE;
  GstOssSink *osssink;

  osssink = GST_OSSSINK (gst_pad_get_parent (pad));
  
  switch (type) {
    case GST_QUERY_LATENCY:
      if (!gst_osssink_convert (pad, 
			        GST_FORMAT_BYTES, gst_osssink_get_delay (osssink),
		                format, value)) 
      {
        res = FALSE;
      }
      break;
    case GST_QUERY_POSITION:
      if (!gst_osssink_convert (pad, 
			        GST_FORMAT_TIME, gst_clock_get_time (osssink->provided_clock),
		                format, value)) 
      {
        res = FALSE;
      }
      break;
    default:
      res = gst_pad_query (gst_pad_get_peer (osssink->sinkpad), type, format, value);
      break;
  }

  return res;
}

static gboolean
gst_osssink_query (GstElement *element, GstQueryType type, GstFormat *format, gint64 *value) 
{
  GstOssSink *osssink = GST_OSSSINK (element);

  return gst_osssink_sink_query (osssink->sinkpad, type, format, value);
}

static void 
gst_osssink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      osssink->mute = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (osssink), "mute");
      break;
    case ARG_FRAGMENT:
      GST_OSSELEMENT (osssink)->fragment = g_value_get_int (value);
      gst_osselement_sync_parms (GST_OSSELEMENT (osssink));
      break;
    case ARG_BUFFER_SIZE:
      osssink->bufsize = g_value_get_uint (value);
      osssink->sinkpool = gst_buffer_pool_get_default (osssink->bufsize, 6);
      g_object_notify (object, "buffer_size");
      break;
    case ARG_SYNC:
      osssink->sync = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (osssink), "sync");
      break;
    case ARG_CHUNK_SIZE:
      osssink->chunk_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void 
gst_osssink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, osssink->mute);
      break;
    case ARG_FRAGMENT:
      g_value_set_int (value, GST_OSSELEMENT (osssink)->fragment);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_uint (value, osssink->bufsize);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, osssink->sync);
      break;
    case ARG_CHUNK_SIZE:
      g_value_set_uint (value, osssink->chunk_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn 
gst_osssink_change_state (GstElement *element) 
{
  GstOssSink *osssink;

  osssink = GST_OSSSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      osssink->resync = TRUE;
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN)) 
        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_RESET, 0);
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssink->provided_clock), FALSE);
      osssink->resync = TRUE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN))
        ioctl (GST_OSSELEMENT (osssink)->fd, SNDCTL_DSP_RESET, 0);
      gst_osselement_reset (GST_OSSELEMENT (osssink));
      break;
    default:
      break;
  }
      
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
