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

#include "gstartsdsink.h"

/* elementfactory information */
static GstElementDetails artsdsink_details = {
  "aRtsd audio sink",
  "Sink/Artsdsink",
  "Plays audio to an aRts server",
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
  ARG_NAME,
};

GST_PADTEMPLATE_FACTORY (sink_factory,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,				/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "artsdsink_sink",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (FALSE),
      "width",      GST_PROPS_INT (8),
      "depth",      GST_PROPS_INT (8),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  ),
  GST_CAPS_NEW (
    "artsdsink_sink",				/* the name of the caps */
    "audio/raw",				/* the mime type of the caps */
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (TRUE),
      "width",      GST_PROPS_INT (16),
      "depth",      GST_PROPS_INT (16),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_LIST (GST_PROPS_INT (1), GST_PROPS_INT (2))
  )
);

static void			gst_artsdsink_class_init		(GstArtsdsinkClass *klass);
static void			gst_artsdsink_init			(GstArtsdsink *artsdsink);

static gboolean			gst_artsdsink_open_audio		(GstArtsdsink *sink);
static void			gst_artsdsink_close_audio		(GstArtsdsink *sink);
static GstElementStateReturn	gst_artsdsink_change_state		(GstElement *element);
static gboolean			gst_artsdsink_sync_parms		(GstArtsdsink *artsdsink);

static void			gst_artsdsink_chain			(GstPad *pad, GstBuffer *buf);

static void			gst_artsdsink_set_property		(GObject *object, guint prop_id, 
									 const GValue *value, GParamSpec *pspec);
static void			gst_artsdsink_get_property		(GObject *object, guint prop_id, 
									 GValue *value, GParamSpec *pspec);

#define GST_TYPE_ARTSDSINK_DEPTHS (gst_artsdsink_depths_get_type())
static GType
gst_artsdsink_depths_get_type (void)
{
  static GType artsdsink_depths_type = 0;
  static GEnumValue artsdsink_depths[] = {
    {8, "8", "8 Bits"},
    {16, "16", "16 Bits"},
    {0, NULL, NULL},
  };
  if (!artsdsink_depths_type) {
    artsdsink_depths_type = g_enum_register_static("GstArtsdsinkDepths", artsdsink_depths);
  }
  return artsdsink_depths_type;
}

#define GST_TYPE_ARTSDSINK_CHANNELS (gst_artsdsink_channels_get_type())
static GType
gst_artsdsink_channels_get_type (void)
{
  static GType artsdsink_channels_type = 0;
  static GEnumValue artsdsink_channels[] = {
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    {0, NULL, NULL},
  };
  if (!artsdsink_channels_type) {
    artsdsink_channels_type = g_enum_register_static("GstArtsdsinkChannels", artsdsink_channels);
  }
  return artsdsink_channels_type;
}


static GstElementClass *parent_class = NULL;
/*static guint gst_artsdsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_artsdsink_get_type (void)
{
  static GType artsdsink_type = 0;

  if (!artsdsink_type) {
    static const GTypeInfo artsdsink_info = {
      sizeof(GstArtsdsinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_artsdsink_class_init,
      NULL,
      NULL,
      sizeof(GstArtsdsink),
      0,
      (GInstanceInitFunc)gst_artsdsink_init,
    };
    artsdsink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstArtsdsink", &artsdsink_info, 0);
  }
  return artsdsink_type;
}

static void
gst_artsdsink_class_init (GstArtsdsinkClass *klass)
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
                      GST_TYPE_ARTSDSINK_DEPTHS,16,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_enum("channels","channels","channels",
                      GST_TYPE_ARTSDSINK_CHANNELS,2,G_PARAM_READWRITE)); /* CHECKME! */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_RATE,
    g_param_spec_int("frequency","frequency","frequency",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NAME,
    g_param_spec_string("name","name","name",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_artsdsink_set_property;
  gobject_class->get_property = gst_artsdsink_get_property;

  gstelement_class->change_state = gst_artsdsink_change_state;
}

static void
gst_artsdsink_init(GstArtsdsink *artsdsink)
{
  artsdsink->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(artsdsink), artsdsink->sinkpad);
  gst_pad_set_chain_function(artsdsink->sinkpad, gst_artsdsink_chain);

  artsdsink->connected = FALSE;
  artsdsink->mute = FALSE;

  /* FIXME: get default from somewhere better than just putting them inline. */
  artsdsink->signd = TRUE;
  artsdsink->depth = 16;
  artsdsink->channels = 2;
  artsdsink->frequency = 44100;
  artsdsink->connect_name = NULL;
}

static gboolean
gst_artsdsink_sync_parms (GstArtsdsink *artsdsink)
{
  g_return_val_if_fail (artsdsink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_ARTSDSINK (artsdsink), FALSE);

  if (!artsdsink->connected) return TRUE;

  /* Need to set stream to use new parameters: only way to do this is to reopen. */
  gst_artsdsink_close_audio (artsdsink);
  return gst_artsdsink_open_audio (artsdsink);
}


static void
gst_artsdsink_chain (GstPad *pad, GstBuffer *buf)
{
  GstArtsdsink *artsdsink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  artsdsink = GST_ARTSDSINK (gst_pad_get_parent (pad));

  if (GST_BUFFER_DATA (buf) != NULL) {
    gst_trace_add_entry(NULL, 0, buf, "artsdsink: writing to server");
    if (!artsdsink->mute && artsdsink->connected) {
      int bytes;
      void * bufptr = GST_BUFFER_DATA (buf);
      int bufsize = GST_BUFFER_SIZE (buf);
      GST_DEBUG (0, "artsdsink: stream=%p data=%p size=%d",
		 artsdsink->stream, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

      do {
	bytes = arts_write (artsdsink->stream, bufptr, bufsize);
	if(bytes < 0) {
	  fprintf(stderr,"arts_write error: %s\n", arts_error_text(bytes));
	  gst_buffer_unref (buf);
	  return;
	}
	bufptr += bytes;
	bufsize -= bytes;
      } while (bufsize > 0);
    }
  }
  gst_buffer_unref (buf);
}

static void
gst_artsdsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstArtsdsink *artsdsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ARTSDSINK(object));
  artsdsink = GST_ARTSDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      artsdsink->mute = g_value_get_boolean (value);
      break;
    case ARG_DEPTH:
      artsdsink->depth = g_value_get_enum (value);
      gst_artsdsink_sync_parms (artsdsink);
      break;
    case ARG_CHANNELS:
      artsdsink->channels = g_value_get_enum (value);
      gst_artsdsink_sync_parms (artsdsink);
      break;
    case ARG_RATE:
      artsdsink->frequency = g_value_get_int (value);
      gst_artsdsink_sync_parms (artsdsink);
      break;
    case ARG_NAME:
      if (artsdsink->connect_name != NULL) g_free(artsdsink->connect_name);
      if (g_value_get_string (value) == NULL)
	  artsdsink->connect_name = NULL;
      else
	  artsdsink->connect_name = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void
gst_artsdsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstArtsdsink *artsdsink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_ARTSDSINK(object));
  artsdsink = GST_ARTSDSINK(object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, artsdsink->mute);
      break;
    case ARG_DEPTH:
      g_value_set_enum (value, artsdsink->depth);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, artsdsink->channels);
      break;
    case ARG_RATE:
      g_value_set_int (value, artsdsink->frequency);
      break;
    case ARG_NAME:
      g_value_set_string (value, artsdsink->connect_name);
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

  factory = gst_elementfactory_new("artsdsink", GST_TYPE_ARTSDSINK,
				   &artsdsink_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate(factory, GST_PADTEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "artsdsink",
  plugin_init
};

static gboolean
gst_artsdsink_open_audio (GstArtsdsink *sink)
{
  const char * connname = "gstreamer";
  int errcode;

  /* Name used by aRtsd for this connection. */
  if (sink->connect_name != NULL) connname = sink->connect_name;

  /* FIXME: this should only ever happen once per process. */
  /* Really, artsc needs to be made thread safe to fix this (and other related */
  /* problems). */
  errcode = arts_init();
  if(errcode < 0) {
      fprintf(stderr,"arts_init error: %s\n", arts_error_text(errcode));
      return FALSE;
  }

  GST_DEBUG (0, "artsdsink: attempting to open connection to aRtsd server");
  sink->stream = arts_play_stream(sink->frequency, sink->depth,
				  sink->channels, connname);
  /* FIXME: check connection */
  /*   GST_DEBUG (0, "artsdsink: can't open connection to aRtsd server"); */

  GST_FLAG_SET (sink, GST_ARTSDSINK_OPEN);
  sink->connected = TRUE;

  return TRUE;
}

static void
gst_artsdsink_close_audio (GstArtsdsink *sink)
{
  if (!sink->connected) return;

  arts_close_stream(sink->stream);
  arts_free();
  GST_FLAG_UNSET (sink, GST_ARTSDSINK_OPEN);
  sink->connected = FALSE;

  g_print("artsdsink: closed connection\n");
}

static GstElementStateReturn
gst_artsdsink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_ARTSDSINK (element), FALSE);

  /* if going down into NULL state, close the stream if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_ARTSDSINK_OPEN))
      gst_artsdsink_close_audio (GST_ARTSDSINK (element));
    /* otherwise (READY or higher) we need to open the stream */
  } else {
    if (!GST_FLAG_IS_SET (element, GST_ARTSDSINK_OPEN)) {
      if (!gst_artsdsink_open_audio (GST_ARTSDSINK (element)))
	return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}

