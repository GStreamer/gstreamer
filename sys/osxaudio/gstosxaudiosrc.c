/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstosxaudiosrc.c: 
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
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <CoreAudio/CoreAudio.h>

#include <gstosxaudiosrc.h>
#include <gstosxaudioelement.h>

/* elementfactory information */
static GstElementDetails gst_osxaudiosrc_details =
GST_ELEMENT_DETAILS ("Audio Source (Mac OS X)",
    "Source/Audio",
    "Read from the sound card",
    "Zaheer Abbas Merali <zaheerabbas at merali.org>");


/* Osxaudiosrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate osxaudiosrc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE , "
        "width = (int) 32, " "rate = (int) 44100, " "channels = (int) 2")
    );

static void gst_osxaudiosrc_base_init (gpointer g_class);
static void gst_osxaudiosrc_class_init (GstOsxAudioSrcClass * klass);
static void gst_osxaudiosrc_init (GstOsxAudioSrc * osxaudiosrc);
static void gst_osxaudiosrc_dispose (GObject * object);

static GstElementStateReturn gst_osxaudiosrc_change_state (GstElement *
    element);

static GstData *gst_osxaudiosrc_get (GstPad * pad);

static GstElementClass *parent_class = NULL;

/*static guint gst_osxaudiosrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_osxaudiosrc_get_type (void)
{
  static GType osxaudiosrc_type = 0;

  if (!osxaudiosrc_type) {
    static const GTypeInfo osxaudiosrc_info = {
      sizeof (GstOsxAudioSrcClass),
      gst_osxaudiosrc_base_init,
      NULL,
      (GClassInitFunc) gst_osxaudiosrc_class_init,
      NULL,
      NULL,
      sizeof (GstOsxAudioSrc),
      0,
      (GInstanceInitFunc) gst_osxaudiosrc_init,
    };

    osxaudiosrc_type =
        g_type_register_static (GST_TYPE_OSXAUDIOELEMENT, "GstOsxAudioSrc",
        &osxaudiosrc_info, 0);
  }
  return osxaudiosrc_type;
}

static void
gst_osxaudiosrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_osxaudiosrc_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&osxaudiosrc_src_factory));
}
static void
gst_osxaudiosrc_class_init (GstOsxAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_OSXAUDIOELEMENT);

  gobject_class->dispose = gst_osxaudiosrc_dispose;

  gstelement_class->change_state = gst_osxaudiosrc_change_state;

}

static void
gst_osxaudiosrc_init (GstOsxAudioSrc * osxaudiosrc)
{
  osxaudiosrc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&osxaudiosrc_src_factory), "src");
  gst_pad_set_get_function (osxaudiosrc->srcpad, gst_osxaudiosrc_get);

  gst_element_add_pad (GST_ELEMENT (osxaudiosrc), osxaudiosrc->srcpad);

}

static void
gst_osxaudiosrc_dispose (GObject * object)
{
  GstOsxAudioSrc *osxaudiosrc = (GstOsxAudioSrc *) object;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstData *
gst_osxaudiosrc_get (GstPad * pad)
{
  GstOsxAudioSrc *src;
  GstBuffer *buf;
  glong readbytes;

  src = GST_OSXAUDIOSRC (gst_pad_get_parent (pad));

  buf = gst_buffer_new_and_alloc ((GST_OSXAUDIOELEMENT (src))->buffer_len);

  readbytes = read_buffer (src, (char *) GST_BUFFER_DATA (buf));

  if (readbytes < 0) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  if (readbytes == 0) {
    gst_buffer_unref (buf);
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;

  src->curoffset += readbytes;

  GST_DEBUG ("pushed buffer from soundcard of %ld bytes", readbytes);

  return GST_DATA (buf);
}

static GstElementStateReturn
gst_osxaudiosrc_change_state (GstElement * element)
{
  GstOsxAudioSrc *osxaudiosrc = GST_OSXAUDIOSRC (element);
  OSErr status;

  GST_DEBUG ("osxaudiosrc: state change");

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      osxaudiosrc->curoffset = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      status =
          AudioDeviceStart (GST_OSXAUDIOELEMENT (osxaudiosrc)->device_id,
          inputAudioDeviceIOProc);
      if (status)
        GST_DEBUG ("AudioDeviceStart returned %d\n", (int) status);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      status =
          AudioDeviceStop (GST_OSXAUDIOELEMENT (osxaudiosrc)->device_id,
          inputAudioDeviceIOProc);
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
