/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosxaudiosink.c: 
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
#include <CoreAudio/CoreAudio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "gstosxaudiosink.h"

/* elementfactory information */
static GstElementDetails gst_osxaudiosink_details =
GST_ELEMENT_DETAILS ("Audio Sink (Mac OS X)",
    "Sink/Audio",
    "Output to a Mac OS X CoreAudio Sound Device",
    "Zaheer Abbas Merali <zaheerabbas at merali.org>");

static void gst_osxaudiosink_base_init (gpointer g_class);
static void gst_osxaudiosink_class_init (GstOsxAudioSinkClass * klass);
static void gst_osxaudiosink_init (GstOsxAudioSink * osxaudiosink);
static void gst_osxaudiosink_dispose (GObject * object);

static GstElementStateReturn gst_osxaudiosink_change_state (GstElement *
    element);

static void gst_osxaudiosink_chain (GstPad * pad, GstData * _data);

/* OssSink signals and args */
enum
{
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

static GstStaticPadTemplate osxaudiosink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 32, " "rate = (int) 44100, " "channels = (int) 2")
    );

static GstElementClass *parent_class = NULL;
static guint gst_osssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_osxaudiosink_get_type (void)
{
  static GType osxaudiosink_type = 0;

  if (!osxaudiosink_type) {
    static const GTypeInfo osxaudiosink_info = {
      sizeof (GstOsxAudioSinkClass),
      gst_osxaudiosink_base_init,
      NULL,
      (GClassInitFunc) gst_osxaudiosink_class_init,
      NULL,
      NULL,
      sizeof (GstOsxAudioSink),
      0,
      (GInstanceInitFunc) gst_osxaudiosink_init,
    };

    osxaudiosink_type =
        g_type_register_static (GST_TYPE_OSXAUDIOELEMENT, "GstOsxAudioSink",
        &osxaudiosink_info, 0);
  }

  return osxaudiosink_type;
}

static void
gst_osxaudiosink_dispose (GObject * object)
{
  /* GstOsxAudioSink *osxaudiosink = (GstOsxAudioSink *) object; */

  /*gst_object_unparent (GST_OBJECT (osxaudiosink->provided_clock)); */

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_osxaudiosink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_osxaudiosink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osxaudiosink_sink_factory));
}
static void
gst_osxaudiosink_class_init (GstOsxAudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OSXAUDIOELEMENT);

  gst_osssink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstOsxAudioSinkClass, handoff), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->dispose = gst_osxaudiosink_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_osxaudiosink_change_state);
}

static void
gst_osxaudiosink_init (GstOsxAudioSink * osxaudiosink)
{
  osxaudiosink->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&osxaudiosink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (osxaudiosink), osxaudiosink->sinkpad);

  gst_pad_set_chain_function (osxaudiosink->sinkpad, gst_osxaudiosink_chain);

  GST_DEBUG ("initializing osxaudiosink");

  GST_FLAG_SET (osxaudiosink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (osxaudiosink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_osxaudiosink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstOsxAudioSink *osxaudiosink;
  guchar *data;
  guint to_write;
  gint amount_written;

  /* this has to be an audio buffer */
  osxaudiosink = GST_OSXAUDIOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        gst_pad_event_default (pad, event);
        return;
      case GST_EVENT_DISCONTINUOUS:
        /* pass-through */
      default:
        gst_pad_event_default (pad, event);
        return;
    }
    g_assert_not_reached ();
  }

  data = GST_BUFFER_DATA (buf);
  to_write = GST_BUFFER_SIZE (buf);
  amount_written = 0;

  while (amount_written < to_write) {
    data += amount_written;
    to_write -= amount_written;
    amount_written =
        write_buffer (GST_OSXAUDIOELEMENT (osxaudiosink), data, to_write);
  }
  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_osxaudiosink_change_state (GstElement * element)
{
  GstOsxAudioSink *osxaudiosink;
  OSErr status;

  osxaudiosink = GST_OSXAUDIOSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      status =
          AudioDeviceStart (GST_OSXAUDIOELEMENT (osxaudiosink)->device_id,
          outputAudioDeviceIOProc);
      if (status)
        GST_DEBUG ("AudioDeviceStart returned %d\n", (int) status);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      status =
          AudioDeviceStop (GST_OSXAUDIOELEMENT (osxaudiosink)->device_id,
          outputAudioDeviceIOProc);
      if (status)
        GST_DEBUG ("AudioDeviceStop returned %d\n", (int) status);
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
