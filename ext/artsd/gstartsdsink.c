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
#include "gstartsdsink.h"

/* elementfactory information */
static GstElementDetails artsdsink_details = {
  "aRtsd audio sink",
  "Sink/Audio",
  "Plays audio to an aRts server",
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
  ARG_NAME,
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",					/* the name of the pads */
  GST_PAD_SINK,				/* type of the pad */
  GST_PAD_ALWAYS,				/* ALWAYS/SOMETIMES */
  GST_CAPS_NEW (
    "artsdsink_sink",				/* the name of the caps */
    "audio/x-raw-int",				/* the mime type of the caps */
      "format",     GST_PROPS_STRING ("int"),
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_BOOLEAN (FALSE),
      "width",      GST_PROPS_LIST (
		      GST_PROPS_INT (8),
		      GST_PROPS_INT (16)
                    ),
      "depth",      GST_PROPS_LIST (
		      GST_PROPS_INT (8),
		      GST_PROPS_INT (16)
                    ),
      "rate",       GST_PROPS_INT_RANGE (8000, 96000),
      "channels",   GST_PROPS_LIST (
		      GST_PROPS_INT (1),
		      GST_PROPS_INT (2)
		    )
  )
);

static void                     gst_artsdsink_base_init                 (gpointer g_class);
static void			gst_artsdsink_class_init		(GstArtsdsinkClass *klass);
static void			gst_artsdsink_init			(GstArtsdsink *artsdsink);

static gboolean			gst_artsdsink_open_audio		(GstArtsdsink *sink);
static void			gst_artsdsink_close_audio		(GstArtsdsink *sink);
static GstElementStateReturn	gst_artsdsink_change_state		(GstElement *element);
static gboolean			gst_artsdsink_sync_parms		(GstArtsdsink *artsdsink);
static GstPadLinkReturn		gst_artsdsink_link			(GstPad *pad, GstCaps *caps);
static void			gst_artsdsink_chain			(GstPad *pad, GstData *_data);

static void			gst_artsdsink_set_property		(GObject *object, guint prop_id, 
									 const GValue *value, GParamSpec *pspec);
static void			gst_artsdsink_get_property		(GObject *object, guint prop_id, 
									 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_artsdsink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_artsdsink_get_type (void)
{
  static GType artsdsink_type = 0;

  if (!artsdsink_type) {
    static const GTypeInfo artsdsink_info = {
      sizeof(GstArtsdsinkClass),
      gst_artsdsink_base_init,
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
gst_artsdsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_class_set_details (element_class, &artsdsink_details);
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
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(artsdsink), artsdsink->sinkpad);
  gst_pad_set_chain_function(artsdsink->sinkpad, gst_artsdsink_chain);
  gst_pad_set_link_function(artsdsink->sinkpad, gst_artsdsink_link);

  artsdsink->connected = FALSE;
  artsdsink->mute = FALSE;
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

static GstPadLinkReturn
gst_artsdsink_link (GstPad *pad, GstCaps *caps)
{
  GstArtsdsink *artsdsink = GST_ARTSDSINK (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get (caps,
		"rate",     &artsdsink->frequency,
		"depth",    &artsdsink->depth,
		"signed",   &artsdsink->signd,
		"channels", &artsdsink->channels,
		NULL);

  if (gst_artsdsink_sync_parms (artsdsink))
    return GST_PAD_LINK_OK;

  return GST_PAD_LINK_REFUSED;
}

static void
gst_artsdsink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstArtsdsink *artsdsink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  artsdsink = GST_ARTSDSINK (gst_pad_get_parent (pad));

  if (GST_BUFFER_DATA (buf) != NULL) {
    gst_trace_add_entry(NULL, 0, GPOINTER_TO_INT(buf), "artsdsink: writing to server");
    if (!artsdsink->mute && artsdsink->connected) {
      int bytes;
      void * bufptr = GST_BUFFER_DATA (buf);
      int bufsize = GST_BUFFER_SIZE (buf);
      GST_DEBUG ("artsdsink: stream=%p data=%p size=%d",
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
    case ARG_NAME:
      g_value_set_string (value, artsdsink->connect_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "artsdsink", GST_RANK_NONE, GST_TYPE_ARTSDSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "artsdsink",
  "Plays audio to an aRts server",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)

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

  GST_DEBUG ("artsdsink: attempting to open connection to aRtsd server");
  sink->stream = arts_play_stream(sink->frequency, sink->depth,
				  sink->channels, connname);
  /* FIXME: check connection */
  /*   GST_DEBUG ("artsdsink: can't open connection to aRtsd server"); */

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

