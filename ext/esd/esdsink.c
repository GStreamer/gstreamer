/* GStreamer
 * Copyright (C) <2001> Richard Boulton <richard-gst@tartarus.org>
 *
 * Based on example.c:
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
#include "esdsink.h"
#include <esd.h>
#include <unistd.h>
#include <errno.h>

/* elementfactory information */
static GstElementDetails esdsink_details = {
  "Esound audio sink",
  "Sink/Audio",
  "Plays audio to an esound server",
  "Richard Boulton <richard-gst@tartarus.org>",
};

/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_HOST,
  ARG_SYNC,
  ARG_FALLBACK,
};

static GstStaticPadTemplate sink_factory = 
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "audio/x-raw-int, "
      "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
      "signed = (boolean) TRUE, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = [ 8000, 96000 ], "
      "channels = [ 1, 2 ]; "
    "audio/x-raw-int, "
      "signed = (boolean) FALSE, "
      "width = (int) 8, "
      "depth = (int) 8, "
      "rate = [ 8000, 96000 ], "
      "channels = [ 1, 2 ]"
  )
);

static void                     gst_esdsink_base_init           (gpointer g_class);
static void			gst_esdsink_class_init		(gpointer g_class, gpointer class_data);
static void			gst_esdsink_init		(GTypeInstance *instance, gpointer g_class);

static gboolean			gst_esdsink_open_audio		(GstEsdsink *sink);
static void			gst_esdsink_close_audio		(GstEsdsink *sink);
static GstElementStateReturn	gst_esdsink_change_state	(GstElement *element);
static GstPadLinkReturn		gst_esdsink_link		(GstPad *pad, const GstCaps *caps);

static GstClockTime		gst_esdsink_get_time		(GstClock *clock, gpointer data);
static GstClock *		gst_esdsink_get_clock		(GstElement *element);
static void                     gst_esdsink_set_clock           (GstElement *element, GstClock *clock);
static void			gst_esdsink_chain		(GstPad *pad, GstData *_data);

static void			gst_esdsink_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_esdsink_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_esdsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_esdsink_get_type (void)
{
  static GType esdsink_type = 0;

  if (!esdsink_type) {
    static const GTypeInfo esdsink_info = {
      sizeof(GstEsdsinkClass),
      gst_esdsink_base_init,
      NULL,
      gst_esdsink_class_init,
      NULL,
      NULL,
      sizeof(GstEsdsink),
      0,
      gst_esdsink_init,
    };
    esdsink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstEsdsink", &esdsink_info, 0);
  }
  return esdsink_type;
}

static void
gst_esdsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, 
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &esdsink_details);
}

static void
gst_esdsink_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  g_object_class_install_property(gobject_class, ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(gobject_class, ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(gobject_class, ARG_SYNC,
    g_param_spec_boolean("sync","sync","Synchronize output to clock",
                         FALSE,G_PARAM_READWRITE));
#if 0
  /* This option is disabled because it is dumb in GStreamer's architecture. */
  g_object_class_install_property(gobject_class, ARG_FALLBACK,
    g_param_spec_boolean("fallback","fallback","Fall back to using OSS if Esound daemon is not present",
                         FALSE,G_PARAM_READWRITE));
#endif

  gobject_class->set_property = gst_esdsink_set_property;
  gobject_class->get_property = gst_esdsink_get_property;

  gstelement_class->change_state = gst_esdsink_change_state;
  gstelement_class->set_clock    = gst_esdsink_set_clock;
  gstelement_class->get_clock    = gst_esdsink_get_clock;
}

static void
gst_esdsink_init(GTypeInstance *instance, gpointer g_class)
{
  GstEsdsink *esdsink = GST_ESDSINK (instance);
  
  esdsink->sinkpad = gst_pad_new_from_template (
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (instance), "sink"), 
      "sink");
  gst_element_add_pad(GST_ELEMENT(esdsink), esdsink->sinkpad);
  gst_pad_set_chain_function(esdsink->sinkpad, GST_DEBUG_FUNCPTR(gst_esdsink_chain));
  gst_pad_set_link_function(esdsink->sinkpad, gst_esdsink_link);

  GST_FLAG_SET (esdsink, GST_ELEMENT_EVENT_AWARE);

  esdsink->mute = FALSE;
  esdsink->fd = -1;
  /* FIXME: get default from somewhere better than just putting them inline. */
  esdsink->negotiated = FALSE;
  esdsink->format = -1;
  esdsink->depth = -1;
  esdsink->channels = -1;
  esdsink->frequency = -1;
  esdsink->host = getenv ("ESPEAKER");
  esdsink->provided_clock = gst_audio_clock_new("esdclock", gst_esdsink_get_time, esdsink);
  gst_object_set_parent(GST_OBJECT(esdsink->provided_clock), GST_OBJECT(esdsink));
  esdsink->sync = FALSE;
  esdsink->fallback = FALSE;
}

static GstPadLinkReturn
gst_esdsink_link (GstPad *pad, const GstCaps *caps)
{
  GstEsdsink *esdsink;
  GstStructure *structure;

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "depth", &esdsink->depth);
  gst_structure_get_int (structure, "channels", &esdsink->channels);
  gst_structure_get_int (structure, "rate", &esdsink->frequency);

  esdsink->bytes_per_sample = esdsink->channels * (esdsink->depth/8);

  gst_esdsink_close_audio (esdsink);
  if (gst_esdsink_open_audio (esdsink)) {
    esdsink->negotiated = TRUE;
    return GST_PAD_LINK_OK;
  }
  /* FIXME: is it supposed to be correct to have closed audio when caps nego 
     failed? */

  return GST_PAD_LINK_REFUSED;
}

static int
gst_esdsink_get_latency (GstEsdsink *esdsink)
{
  /* esd_get_latency() doesn't actually work.  So we return a
   * fake value */
  return 44100/2;
#if 0
  return esd_get_latency (esdsink->fd);
#endif
}

static GstClockTime
gst_esdsink_get_time (GstClock *clock, gpointer data)
{
  GstEsdsink *esdsink = GST_ESDSINK(data);
  GstClockTime res;

  res = (esdsink->handled - gst_esdsink_get_latency(esdsink))
    * GST_SECOND / esdsink->frequency;

  return res;
}

static GstClock *
gst_esdsink_get_clock (GstElement *element)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (element);

  return GST_CLOCK(esdsink->provided_clock);
}

static void
gst_esdsink_set_clock (GstElement *element, GstClock *clock)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (element);

  esdsink->clock = clock;
}

static void
gst_esdsink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  if (!esdsink->negotiated) {
    gst_element_error (esdsink, CORE, NEGOTIATION, NULL,
                       ("element wasn't negotiated before chain function"));
    goto done;
  }

  if (GST_IS_EVENT(buf)){
    GstEvent *event = GST_EVENT(buf);

    switch(GST_EVENT_TYPE(event)){
      case GST_EVENT_EOS:
	gst_audio_clock_set_active (GST_AUDIO_CLOCK (esdsink->provided_clock),
	    FALSE);
	gst_pad_event_default (pad, event);
	return;
      case GST_EVENT_DISCONTINUOUS:
      {
	gint64 value;

	if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
	  gst_element_set_time (GST_ELEMENT (esdsink), value);
	  esdsink->handled = 0;
	}
	esdsink->resync = TRUE;
	break;
      }
      default:
	gst_pad_event_default(pad, event);
	return;
    }
    gst_event_unref(event);
    return;
  }

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!esdsink->mute && esdsink->fd >= 0) {
      guchar *data = GST_BUFFER_DATA (buf);
      gint size = GST_BUFFER_SIZE (buf);
      gint to_write = 0;

      if (esdsink->clock){
	gint delay = 0;
	gint64 queued;

	delay = gst_esdsink_get_latency (esdsink);
	queued = delay * GST_SECOND / esdsink->frequency;

	if (esdsink->resync && esdsink->sync) {
	  gst_element_wait (GST_ELEMENT (esdsink), GST_BUFFER_TIMESTAMP (buf) - queued); 

	}else{
	  to_write = size;
	}
      }

      GST_DEBUG ("esdsink: fd=%d data=%p size=%d",
		 esdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      while (to_write > 0){
	int done;

        done = write (esdsink->fd, data, to_write);

	if(done < 0){
	  if(errno==EINTR){
	    goto done;
	  }
	  g_assert_not_reached();
	}

	to_write -= done;
	data += done;
	esdsink->handled += done / esdsink->bytes_per_sample;
      }

    }
  }

  gst_audio_clock_update_time ((GstAudioClock *)esdsink->provided_clock,
      GST_BUFFER_TIMESTAMP (buf));

done:
  gst_buffer_unref (buf);
}

static void
gst_esdsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstEsdsink *esdsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ESDSINK(object));
  esdsink = GST_ESDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      esdsink->mute = g_value_get_boolean (value);
      break;
    case ARG_HOST:
      g_free(esdsink->host);
      if (g_value_get_string (value) == NULL)
	  esdsink->host = NULL;
      else
	  esdsink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_SYNC:
      esdsink->sync = g_value_get_boolean (value);
      break;
    case ARG_FALLBACK:
      esdsink->fallback = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_esdsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, esdsink->mute);
      break;
    case ARG_HOST:
      g_value_set_string (value, esdsink->host);
      break;
    case ARG_SYNC:
      g_value_set_boolean (value, esdsink->sync);
      break;
    case ARG_FALLBACK:
      g_value_set_boolean (value, esdsink->fallback);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_esdsink_factory_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "esdsink", GST_RANK_NONE, GST_TYPE_ESDSINK))
    return FALSE;

  return TRUE;
}

static gboolean
gst_esdsink_open_audio (GstEsdsink *sink)
{
  /* Name used by esound for this connection. */
  const char * connname = "GStreamer";

  /* Bitmap describing audio format. */
  esd_format_t esdformat = ESD_STREAM | ESD_PLAY;

  g_return_val_if_fail (sink->fd == -1, FALSE);

  if (sink->depth == 16) esdformat |= ESD_BITS16;
  else if (sink->depth == 8) esdformat |= ESD_BITS8;
  else {
    GST_DEBUG ("esdsink: invalid bit depth (%d)", sink->depth);
    return FALSE;
  }

  if (sink->channels == 2) esdformat |= ESD_STEREO;
  else if (sink->channels == 1) esdformat |= ESD_MONO;
  else {
    GST_DEBUG ("esdsink: invalid number of channels (%d)", sink->channels);
    return FALSE;
  }

  GST_DEBUG ("esdsink: attempting to open connection to esound server");
  if(sink->fallback){
    sink->fd = esd_play_stream_fallback(esdformat, sink->frequency, sink->host, connname);
  }else{
    sink->fd = esd_play_stream(esdformat, sink->frequency, sink->host, connname);
  }
  if ( sink->fd < 0 ) {
    GST_DEBUG ("esdsink: can't open connection to esound server");
    return FALSE;
  }

  return TRUE;
}

static void
gst_esdsink_close_audio (GstEsdsink *sink)
{
  if (sink->fd < 0) 
    return;

  close(sink->fd);
  sink->fd = -1;

  GST_DEBUG ("esdsink: closed sound device");
}

static GstElementStateReturn
gst_esdsink_change_state (GstElement *element)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (esdsink->provided_clock),
	  FALSE);
      esdsink->resync = TRUE;
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_esdsink_close_audio (GST_ESDSINK (element));
      esdsink->negotiated = FALSE;
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

