/* GStreamer
 * Copyright (C) <2003> Laurent Vivier <Laurent.Vivier@bull.net>
 * Copyright (C) <2004> Arwed v. Merkatz <v.merkatz@gmx.net>
 *
 * Based on esdsink.c:
 * Copyright (C) <2001> Richard Boulton <richard-gst@tartarus.org>
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
#include <gst/gst.h>
#include <string.h>
#include <audio/audiolib.h>
#include <audio/soundlib.h>
#include "nassink.h"

#define NAS_SOUND_PORT_DURATION	(2)

GST_DEBUG_CATEGORY(NAS);
/* Signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUTE,
  ARG_HOST
};

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(
    "audio/x-raw-int, "
      "endianess = (int) BYTE_ORDER, "
      "signed = (boolean) TRUE, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "rate = (int) [ 8000, 96000 ], "
      "channels = (int) [ 1, 2 ]; "
    "audio/x-raw-int, "
      "signed = (boolean) FALSE, "
      "width = (int) 8, "
      "depth = (int) 8, "
      "rate = (int) [ 8000, 96000 ], "
      "channels = (int) [ 1, 2 ]"
  )
);

static void                     gst_nassink_base_init           (gpointer g_class);
static void			gst_nassink_class_init		(GstNassinkClass *klass);
static void			gst_nassink_init		(GstNassink *nassink);

static gboolean			gst_nassink_open_audio		(GstNassink *sink);
static void			gst_nassink_close_audio		(GstNassink *sink);
static GstElementStateReturn	gst_nassink_change_state	(GstElement *element);
static gboolean			gst_nassink_sync_parms		(GstNassink *nassink);
static GstPadLinkReturn		gst_nassink_sinkconnect		(GstPad *pad, const GstCaps *caps);

static void			gst_nassink_chain		(GstPad *pad, GstData *_data);

static void			gst_nassink_set_property	(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void			gst_nassink_get_property	(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static void			NAS_flush			(GstNassink *sink);
static void			NAS_sendData			(GstNassink *sink, AuUint32 numBytes);
static AuBool			NAS_EventHandler		(AuServer *aud, AuEvent *ev, AuEventHandlerRec *handler);
static AuDeviceID		NAS_getDevice			(AuServer* aud, int numTracks);
static int			NAS_allocBuffer			(GstNassink *sink);
static int			NAS_createFlow			(GstNassink *sink, unsigned char format, unsigned short rate, int numTracks);

static GstElementClass *parent_class = NULL;

GType
gst_nassink_get_type (void)
{
  static GType nassink_type = 0;

  if (!nassink_type) {
    static const GTypeInfo nassink_info = {
      sizeof(GstNassinkClass),
      gst_nassink_base_init,
      NULL,
      (GClassInitFunc)gst_nassink_class_init,
      NULL,
      NULL,
      sizeof(GstNassink),
      0,
      (GInstanceInitFunc)gst_nassink_init,
    };
    nassink_type = g_type_register_static(GST_TYPE_ELEMENT, "GstNassink", &nassink_info, 0);
  }

  return nassink_type;
}

static void
gst_nassink_base_init (gpointer g_class)
{
  static GstElementDetails nassink_details = {
    "NAS sink",
    "Sink/Audio",
    "Plays audio to a Network Audio Server",
    "Laurent Vivier <Laurent.Vivier@bull.net>, "
    "Arwed v. Merkatz <v.merkatz@gmx.net>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &nassink_details);
  GST_DEBUG_CATEGORY_INIT(NAS, "NAS", 0, NULL);
}

static void
gst_nassink_class_init (GstNassinkClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  if (parent_class == NULL)
    parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_nassink_set_property;
  gobject_class->get_property = gst_nassink_get_property;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_HOST,
    g_param_spec_string("host","host","host",
                        NULL, G_PARAM_READWRITE)); /* CHECKME */

  gstelement_class->change_state = gst_nassink_change_state;
}

static void
gst_nassink_init(GstNassink *nassink)
{
  GST_CAT_DEBUG(NAS,"nassink: init");
  nassink->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(nassink), nassink->sinkpad);
  gst_pad_set_chain_function(nassink->sinkpad, GST_DEBUG_FUNCPTR(gst_nassink_chain));
  gst_pad_set_link_function(nassink->sinkpad, gst_nassink_sinkconnect);

  nassink->mute = FALSE;
  nassink->depth = 16;
  nassink->tracks = 2;
  nassink->rate = 44100;
  nassink->host = g_strdup (getenv("AUDIOSERVER"));
  if (nassink->host == NULL)
    nassink->host = g_strdup (getenv("DISPLAY"));

  nassink->audio = NULL;
  nassink->flow = AuNone;
  nassink->size = 0;
  nassink->pos = 0;
  nassink->buf = NULL;
}

static gboolean
gst_nassink_sync_parms (GstNassink *nassink)
{
  gint ret;
  unsigned char format;
  g_return_val_if_fail (nassink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_NASSINK (nassink), FALSE);

  if (nassink->audio == NULL) return TRUE;

  GST_CAT_DEBUG(NAS,"depth=%i rate=%i", nassink->depth, nassink->rate);
  if (nassink->flow != AuNone)
  {
    GST_CAT_DEBUG(NAS,"flushing buffer");
    while (nassink->pos && nassink->buf)
      NAS_flush(nassink);
    AuStopFlow( nassink->audio, nassink->flow, NULL);
    AuReleaseScratchFlow(nassink->audio, nassink->flow, NULL);
    nassink->flow = AuNone;
  }

  if (nassink->depth == 16)
#if G_BYTE_ORDER == G_BIG_ENDIAN
    format = AuFormatLinearSigned16MSB;
#else
    format = AuFormatLinearSigned16LSB;
#endif
  else
    format = AuFormatLinearUnsigned8;

  ret = NAS_createFlow(nassink, format, nassink->rate, nassink->tracks);

  return ret >= 0;
}

static GstPadLinkReturn
gst_nassink_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstNassink *nassink;
  GstStructure *structure;

  nassink = GST_NASSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "depth", &nassink->depth);
  gst_structure_get_int (structure, "channels", &nassink->tracks);
  gst_structure_get_int (structure, "rate", &nassink->rate);

  if (!gst_nassink_sync_parms(nassink))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static void
gst_nassink_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  int pos = 0;
  int remaining;
  int available;
  GstNassink *nassink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  nassink = GST_NASSINK (gst_pad_get_parent (pad));

  g_return_if_fail(nassink->buf != NULL);

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!nassink->mute && nassink->audio != NULL) {

      remaining = GST_BUFFER_SIZE (buf);
      while ((nassink->flow != AuNone) && ( remaining > 0)) {

        /* number of bytes we can copy to buffer */
     
        available = remaining > nassink->size - nassink->pos ?
		    nassink->size - nassink->pos : remaining;

	/* fill the buffer */

	memcpy (nassink->buf + nassink->pos, GST_BUFFER_DATA (buf) + pos, available);

	nassink->pos += available;
	pos += available;

	remaining -= available;

	/* if we have more bytes, need to flush the buffer */

	if (remaining > 0) {
	  while ((nassink->flow != AuNone) && (nassink->pos == nassink->size)) {
	    NAS_flush(nassink);
	  }
	}
      }

      /* give some time to event handler */

      AuSync(nassink->audio, AuFalse);

    }
  }
  gst_buffer_unref (buf);
}

static void
gst_nassink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstNassink *nassink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_NASSINK(object));
  nassink = GST_NASSINK(object);

  switch (prop_id) {
  case ARG_MUTE:
    nassink->mute = g_value_get_boolean (value);
    break;
  case ARG_HOST:
    if (nassink->host != NULL) g_free(nassink->host);
    if (g_value_get_string (value) == NULL)
        nassink->host = NULL;
    else
        nassink->host = g_strdup (g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_nassink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstNassink *nassink;

  g_return_if_fail(GST_IS_NASSINK(object));

  nassink = GST_NASSINK(object);

  switch (prop_id) {
  case ARG_MUTE:
    g_value_set_boolean (value, nassink->mute);
    break;
  case ARG_HOST:
    g_value_set_string (value, nassink->host);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "nassink", GST_RANK_NONE,
        GST_TYPE_NASSINK)){
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "nassink",
  "uses NAS for audio output",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
);

static gboolean
gst_nassink_open_audio (GstNassink *sink)
{
  /* Open Server */

  sink->audio = AuOpenServer(sink->host, 0, NULL, 0, NULL, NULL);
  if (sink->audio == NULL)
    return FALSE;

  sink->flow = AuNone;
  sink->size = 0;
  sink->pos = 0;
  sink->buf = NULL;

  /* Start a flow */

  GST_FLAG_SET (sink, GST_NASSINK_OPEN);

  GST_CAT_DEBUG(NAS,"opened audio device");
  return TRUE;
}

static void
gst_nassink_close_audio (GstNassink *sink)
{
  if (sink->audio == NULL) return;

  if (sink->flow != AuNone) {
    while (sink->pos && sink->buf) {
      NAS_flush(sink);
    }

    AuStopFlow( sink->audio, sink->flow, NULL);
    AuReleaseScratchFlow(sink->audio, sink->flow, NULL);
    sink->flow = AuNone;
  }

  if (sink->buf != NULL)
  {
    free(sink->buf);
    sink->buf = NULL;
  }

  AuCloseServer(sink->audio);
  sink->audio = NULL;

  GST_FLAG_UNSET (sink, GST_NASSINK_OPEN);

  GST_CAT_DEBUG (NAS,"closed audio device");
}

static GstElementStateReturn
gst_nassink_change_state (GstElement *element)
{
  GstNassink *nassink;
  g_return_val_if_fail (GST_IS_NASSINK (element), FALSE);

  nassink = GST_NASSINK (element);

  switch (GST_STATE_PENDING (element)) {
  case GST_STATE_NULL:
    if (GST_FLAG_IS_SET (element, GST_NASSINK_OPEN))
      gst_nassink_close_audio (nassink);
    break;

  case GST_STATE_READY:
    if (!GST_FLAG_IS_SET (element, GST_NASSINK_OPEN))
      gst_nassink_open_audio (nassink);
    break;

  case GST_STATE_PAUSED:
    while (nassink->pos && nassink->buf)
      NAS_flush(nassink);
    break;

  case GST_STATE_PLAYING:
    break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
NAS_flush(GstNassink *sink)
{
  AuEvent ev;

  AuNextEvent(sink->audio, AuTrue, &ev);
  AuDispatchEvent(sink->audio, &ev);
}

static void
NAS_sendData(GstNassink *sink, AuUint32 numBytes)
{
  if (numBytes < (sink->pos)) {

    AuWriteElement(sink->audio, sink->flow, 0,
		   numBytes, sink->buf, AuFalse, NULL);

    memmove(sink->buf, sink->buf + numBytes,
	    sink->pos - numBytes);

    sink->pos = sink->pos - numBytes;

  } else
  {
    AuWriteElement(sink->audio, sink->flow, 0,
		   sink->pos, sink->buf,
		   (numBytes > sink->pos), NULL);
    sink->pos = 0;
  }
}

static AuBool
NAS_EventHandler(AuServer *aud, AuEvent *ev, AuEventHandlerRec *handler)
{
  GstNassink *sink = (GstNassink *)handler->data;
  AuElementNotifyEvent *notify;

  switch (ev->type) {

  case AuEventTypeElementNotify:

    notify = (AuElementNotifyEvent *) ev;

    switch(notify->kind) {

    case AuElementNotifyKindLowWater:
      NAS_sendData(sink, notify->num_bytes);
      break;

    case AuElementNotifyKindState:

      switch(notify->cur_state) {

      case AuStateStop:
	 
	if (sink->flow != AuNone) {
	  if (notify->reason == AuReasonEOF)
	    AuStopFlow(handler->aud, sink->flow, NULL);
	  AuReleaseScratchFlow(handler->aud, sink->flow, NULL);
	  sink->flow = AuNone;
	}
	AuUnregisterEventHandler(handler->aud, handler);
        break;

      case AuStatePause:

        switch(notify->reason) {
	case AuReasonUnderrun:
	case AuReasonOverrun:
	case AuReasonEOF:
	case AuReasonWatermark:

	  NAS_sendData(sink, notify->num_bytes);

	  break;

	case AuReasonHardware:

	  if (AuSoundRestartHardwarePauses)
	    AuStartFlow(handler->aud, sink->flow, NULL);
	  else
	    AuStopFlow(handler->aud, sink->flow, NULL);

	  break;
	}
        break;
      }
      break;
    }
    break;
  }

  return AuTrue;
}

static AuDeviceID
NAS_getDevice(AuServer* aud, int numTracks)
{
  int i;

  for (i = 0; i < AuServerNumDevices(aud); i++) {
    if ( (AuDeviceKind(AuServerDevice(aud, i))
	 == AuComponentKindPhysicalOutput) &&
         (AuDeviceNumTracks(AuServerDevice(aud, i)) == numTracks )) {

      return AuDeviceIdentifier(AuServerDevice(aud, i));

    }
  }

  return AuNone;
}

static int
NAS_allocBuffer(GstNassink *sink)
{
  if (sink->buf != NULL) {
    free(sink->buf);
  }

  sink->buf = (char *) malloc(sink->size);
  if (sink->buf == NULL) {
    return -1;
  }

  sink->pos = 0;

  return 0;
}

static int
NAS_createFlow(GstNassink *sink, unsigned char format, unsigned short rate, int numTracks)
{
  AuDeviceID device;
  AuElement elements[2];
  AuUint32 buf_samples;

  GST_CAT_DEBUG(NAS,"getting device");
  device = NAS_getDevice(sink->audio, numTracks);
  if (device == AuNone) {
    GST_CAT_DEBUG(NAS,"no device found");
    return -1;
  }

  sink->flow = AuGetScratchFlow(sink->audio, NULL);
  if (sink->flow == 0) {
    GST_CAT_DEBUG(NAS,"couldn't get flow");
    return -1;
  }

  /* free old Elements and reconnet to server, needed to change samplerate */
  {
    AuBool clocked;
    int num_elements;
    AuStatus status;
    AuElement *oldelems;
    oldelems = AuGetElements(sink->audio, sink->flow, &clocked, &num_elements, &status);
    if (num_elements > 0) {
      GST_CAT_DEBUG(NAS,"GetElements status: %i", status);
      if (oldelems)
        AuFreeElements(sink->audio, num_elements, oldelems);
      gst_nassink_close_audio(sink);
      gst_nassink_open_audio(sink);
      sink->flow = AuGetScratchFlow(sink->audio, NULL);
      if (sink->flow == 0) {
        GST_CAT_DEBUG(NAS,"couldn't get flow");
        return -1;
      }
    }
  }

  buf_samples = rate * NAS_SOUND_PORT_DURATION;


  AuMakeElementImportClient( &elements[0],		/* element */
                             rate,			/* rate */
                             format,			/* format */
                             numTracks,			/* number of tracks */
                             AuTrue,			/* discart */
                             buf_samples,		/* max samples */
                             (AuUint32) (buf_samples / 100
                                      * AuSoundPortLowWaterMark),
							/* low water mark */
                             0,				/* num actions */
                             NULL);

  AuMakeElementExportDevice( &elements[1],		/* element */
                             0,				/* input */
                             device,			/* device */
                             rate,			/* rate */
                             AuUnlimitedSamples,	/* num samples */
                             0,				/* num actions */
                             NULL);			/* actions */

  AuSetElements( sink->audio,				/* server */
                 sink->flow,				/* flow ID */
                 AuTrue,				/* clocked */
                 2,					/* num elements */
                 elements,				/* elements */
                 NULL);

  AuRegisterEventHandler( sink->audio,			/* server */
                          AuEventHandlerIDMask,		/* value mask */
                          0,				/* type */
                          sink->flow,			/* flow ID */
                          NAS_EventHandler,		/* callback */
                          (AuPointer)sink);		/* data */

  sink->size = buf_samples * numTracks * AuSizeofFormat(format);

  if (NAS_allocBuffer(sink) < 0) {

    AuReleaseScratchFlow(sink->audio, sink->flow, NULL);

    return -1;
  }

  AuStartFlow(sink->audio, sink->flow, NULL);

  return 0;
}
