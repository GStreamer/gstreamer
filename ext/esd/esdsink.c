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

#include "esdsink.h"
#include <esd.h>

/* elementfactory information */
static GstElementDetails esdsink_details = {
  "Esound audio sink",
  "Sink/Audio",
  "Plays audio to an esound server",
  VERSION,
  "Richard Boulton <richard-gst@tartarus.org>",
  "(C) 2001",
};

/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_DEPTH,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_HOST,
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "esdsink_sink8",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      /* Properties follow: */
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "width",      GST_PROPS_INT (8),
	"depth",      GST_PROPS_INT (8),
	"rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	"channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  ),
  GST_CAPS_NEW (
    "esdsink_sink16",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      /* Properties follow: */
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "signed",     GST_PROPS_BOOLEAN (TRUE),
        "width",      GST_PROPS_INT (16),
	"depth",      GST_PROPS_INT (16),
	"rate",       GST_PROPS_INT_RANGE (8000, 96000),
     	"channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  )
);

static void			gst_esdsink_class_init		(GstEsdsinkClass *klass);
static void			gst_esdsink_init		(GstEsdsink *esdsink);

static gboolean			gst_esdsink_open_audio		(GstEsdsink *sink);
static void			gst_esdsink_close_audio		(GstEsdsink *sink);
static GstElementStateReturn	gst_esdsink_change_state	(GstElement *element);
static gboolean			gst_esdsink_sync_parms		(GstEsdsink *esdsink);
static GstPadConnectReturn	gst_esdsink_sinkconnect		(GstPad *pad, GstCaps *caps);

static void			gst_esdsink_chain		(GstPad *pad, GstBuffer *buf);

static void			gst_esdsink_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_esdsink_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

#define GST_TYPE_ESDSINK_DEPTHS (gst_esdsink_depths_get_type())
static GType
gst_esdsink_depths_get_type (void)
{
  static GType esdsink_depths_type = 0;
  static GEnumValue esdsink_depths[] = {
    {8, "8", "8 Bits"},
    {16, "16", "16 Bits"},
    {0, NULL, NULL},
  };
  if (!esdsink_depths_type) {
    esdsink_depths_type = g_enum_register_static("GstEsdsinkDepths", esdsink_depths);
  }
  return esdsink_depths_type;
}

#define GST_TYPE_ESDSINK_CHANNELS (gst_esdsink_channels_get_type())
static GType
gst_esdsink_channels_get_type (void)
{
  static GType esdsink_channels_type = 0;
  static GEnumValue esdsink_channels[] = {
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    {0, NULL, NULL},
  };
  if (!esdsink_channels_type) {
    esdsink_channels_type = g_enum_register_static("GstEsdsinkChannels", esdsink_channels);
  }
  return esdsink_channels_type;
}


static GstElementClass *parent_class = NULL;
/*static guint gst_esdsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_esdsink_get_type (void)
{
  static GType esdsink_type = 0;

  if (!esdsink_type) {
    static const GTypeInfo esdsink_info = {
      sizeof(GstEsdsinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_esdsink_class_init,
      NULL,
      NULL,
      sizeof(GstEsdsink),
      0,
      (GInstanceInitFunc)gst_esdsink_init,
    };
    esdsink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstEsdsink", &esdsink_info, 0);
  }
  return esdsink_type;
}

static void
gst_esdsink_class_init (GstEsdsinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEPTH,
    g_param_spec_enum("depth","depth","depth",
                      GST_TYPE_ESDSINK_DEPTHS,16,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_enum("channels","channels","channels",
                      GST_TYPE_ESDSINK_CHANNELS,2,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RATE,
    g_param_spec_int("frequency","frequency","frequency",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_esdsink_set_property;
  gobject_class->get_property = gst_esdsink_get_property;

  gstelement_class->change_state = gst_esdsink_change_state;
}

static void
gst_esdsink_init(GstEsdsink *esdsink)
{
  esdsink->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(esdsink), esdsink->sinkpad);
  gst_pad_set_chain_function(esdsink->sinkpad, GST_DEBUG_FUNCPTR(gst_esdsink_chain));
  gst_pad_set_connect_function(esdsink->sinkpad, gst_esdsink_sinkconnect);

  esdsink->mute = FALSE;
  esdsink->fd = -1;
  /* FIXME: get default from somewhere better than just putting them inline. */
  esdsink->format = 16;
  esdsink->depth = 16;
  esdsink->channels = 2;
  esdsink->frequency = 44100;
  esdsink->host = NULL;
}

static gboolean
gst_esdsink_sync_parms (GstEsdsink *esdsink)
{
  g_return_val_if_fail (esdsink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ESDSINK (esdsink), FALSE);

  if (esdsink->fd == -1) return TRUE;

  /* Need to set fd to use new parameters: only way to do this is to reopen. */
  gst_esdsink_close_audio (esdsink);
  return gst_esdsink_open_audio (esdsink);
}

static GstPadConnectReturn
gst_esdsink_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstEsdsink *esdsink;

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "depth", &esdsink->depth);
  gst_caps_get_int (caps, "channels", &esdsink->channels);
  gst_caps_get_int (caps, "rate", &esdsink->frequency);

  if (gst_esdsink_sync_parms (esdsink))
    return GST_PAD_CONNECT_OK;

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_esdsink_chain (GstPad *pad, GstBuffer *buf)
{
  GstEsdsink *esdsink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  esdsink = GST_ESDSINK (gst_pad_get_parent (pad));

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!esdsink->mute && esdsink->fd >= 0) {
      GST_DEBUG (0, "esdsink: fd=%d data=%p size=%d",
		 esdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      write (esdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    }
  }
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
    case ARG_DEPTH:
      esdsink->depth = g_value_get_enum (value);
      gst_esdsink_sync_parms (esdsink);
      break;
    case ARG_CHANNELS:
      esdsink->channels = g_value_get_enum (value);
      gst_esdsink_sync_parms (esdsink);
      break;
    case ARG_RATE:
      esdsink->frequency = g_value_get_int (value);
      gst_esdsink_sync_parms (esdsink);
      break;
    case ARG_HOST:
      if (esdsink->host != NULL) g_free(esdsink->host);
      if (g_value_get_string (value) == NULL)
	  esdsink->host = NULL;
      else
	  esdsink->host = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_esdsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstEsdsink *esdsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ESDSINK(object));
  esdsink = GST_ESDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, esdsink->mute);
      break;
    case ARG_DEPTH:
      g_value_set_enum (value, esdsink->depth);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, esdsink->channels);
      break;
    case ARG_RATE:
      g_value_set_int (value, esdsink->frequency);
      break;
    case ARG_HOST:
      g_value_set_string (value, esdsink->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("esdsink", GST_TYPE_ESDSINK,
				   &esdsink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template(factory, GST_PAD_TEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "esdsink",
  plugin_init
};

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
    GST_DEBUG (0, "esdsink: invalid bit depth (%d)", sink->depth);
    return FALSE;
  }

  if (sink->channels == 2) esdformat |= ESD_STEREO;
  else if (sink->channels == 1) esdformat |= ESD_MONO;
  else {
    GST_DEBUG (0, "esdsink: invalid number of channels (%d)", sink->channels);
    return FALSE;
  }

  GST_DEBUG (0, "esdsink: attempting to open connection to esound server");
  sink->fd = esd_play_stream_fallback(esdformat, sink->frequency, sink->host, connname);
  if ( sink->fd < 0 ) {
    GST_DEBUG (0, "esdsink: can't open connection to esound server");
    return FALSE;
  }

  GST_FLAG_SET (sink, GST_ESDSINK_OPEN);

  return TRUE;
}

static void
gst_esdsink_close_audio (GstEsdsink *sink)
{
  if (sink->fd < 0) return;

  close(sink->fd);
  sink->fd = -1;

  GST_FLAG_UNSET (sink, GST_ESDSINK_OPEN);

  GST_DEBUG (0, "esdsink: closed sound device");
}

static GstElementStateReturn
gst_esdsink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_ESDSINK (element), FALSE);

  /* if going down into NULL state, close the fd if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_ESDSINK_OPEN))
      gst_esdsink_close_audio (GST_ESDSINK (element));
    /* otherwise (READY or higher) we need to open the fd */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_ESDSINK_OPEN)) {
      if (!gst_esdsink_open_audio (GST_ESDSINK (element)))
	return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}

