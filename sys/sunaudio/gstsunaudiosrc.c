/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsunaudiosrc.c:
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
#include <unistd.h>
#include <string.h>
#include <sys/audioio.h>

#include <gstsunaudiosrc.h>
#include <gstsunelement.h>

/* elementfactory information */
static GstElementDetails gst_sunaudiosrc_details =
GST_ELEMENT_DETAILS ("SunAudioSource",
    "Source/Audio",
    "Audio source for Sun Audio devices",
    "Balamurali Viswanathan <balamurali.viswanathan@wipro.com>");

enum
{
  ARG_0,
  ARG_DEVICE,
  ARG_BUFFER_SIZE
};

static GstStaticPadTemplate gst_sunaudiosrc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 5510, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_sunaudiosrc_base_init (gpointer g_class);
static void gst_sunaudiosrc_class_init (GstSunAudioSrcClass * klass);
static void gst_sunaudiosrc_init (GstSunAudioSrc * sunaudiosrc);

static void gst_sunaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sunaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_sunaudiosrc_change_state (GstElement * element);

static gboolean gst_sunaudiosrc_setparams (GstSunAudioSrc * sunaudiosrc);
static GstData *gst_sunaudiosrc_get (GstPad * pad);
static GstCaps *gst_sunaudiosrc_getcaps (GstPad * pad);
static GstPadLinkReturn gst_sunaudiosrc_pad_link (GstPad * pad,
    const GstCaps * caps);

static GstElementClass *parent_class = NULL;

GType
gst_sunaudiosrc_get_type (void)
{
  static GType sunaudiosrc_type = 0;

  if (!sunaudiosrc_type) {
    static const GTypeInfo sunaudiosrc_info = {
      sizeof (GstSunAudioSrcClass),
      gst_sunaudiosrc_base_init,
      NULL,
      (GClassInitFunc) gst_sunaudiosrc_class_init,
      NULL,
      NULL,
      sizeof (GstSunAudioSrc),
      0,
      (GInstanceInitFunc) gst_sunaudiosrc_init,
    };

    sunaudiosrc_type = g_type_register_static (GST_TYPE_SUNAUDIOELEMENT,
        "GstSunAudioSrc", &sunaudiosrc_info, 0);
  }
  return sunaudiosrc_type;
}

static void
gst_sunaudiosrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_sunaudiosrc_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_sunaudiosrc_src_factory));
}

static void
gst_sunaudiosrc_class_init (GstSunAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_SUNAUDIOELEMENT);

  gobject_class->set_property = gst_sunaudiosrc_set_property;
  gobject_class->get_property = gst_sunaudiosrc_get_property;

  gstelement_class->change_state = gst_sunaudiosrc_change_state;

  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device", "Audio Device (/dev/audio)",
          "/dev/audio", G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BUFFER_SIZE,
      g_param_spec_int ("buffer_size", "Buffer Size", "Buffer Size",
          1, G_MAXINT, 64, G_PARAM_READWRITE));

}


static void
gst_sunaudiosrc_init (GstSunAudioSrc * sunaudiosrc)
{
  const char *audiodev;

  sunaudiosrc->srcpad = gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_sunaudiosrc_src_factory), "src");

  gst_pad_set_get_function (sunaudiosrc->srcpad, gst_sunaudiosrc_get);
  gst_pad_set_getcaps_function (sunaudiosrc->srcpad, gst_sunaudiosrc_getcaps);
  gst_pad_set_link_function (sunaudiosrc->srcpad, gst_sunaudiosrc_pad_link);

  gst_element_add_pad (GST_ELEMENT (sunaudiosrc), sunaudiosrc->srcpad);

  sunaudiosrc->buffer_size = 64;

  audiodev = g_getenv ("AUDIODEV");
  if (audiodev == NULL)
    audiodev = "/dev/audio";
  sunaudiosrc->device = g_strdup (audiodev);

}

static void
gst_sunaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSunAudioSrc *sunaudiosrc;

  g_return_if_fail (GST_IS_SUNAUDIOSRC (object));
  sunaudiosrc = GST_SUNAUDIOSRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      if (gst_element_get_state (GST_ELEMENT (sunaudiosrc)) == GST_STATE_NULL) {
        g_free (sunaudiosrc->device);
        sunaudiosrc->device = g_strdup (g_value_get_string (value));
      }
      break;
    case ARG_BUFFER_SIZE:
      if (gst_element_get_state (GST_ELEMENT (sunaudiosrc)) == GST_STATE_NULL) {
        sunaudiosrc->buffer_size = g_value_get_int (value);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sunaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSunAudioSrc *sunaudiosrc;

  g_return_if_fail (GST_IS_SUNAUDIOSRC (object));
  sunaudiosrc = GST_SUNAUDIOSRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, sunaudiosrc->device);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_int (value, sunaudiosrc->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_sunaudiosrc_change_state (GstElement * element, GstStateChange transition)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIOSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;

}

static gboolean
gst_sunaudiosrc_negotiate (GstPad * pad)
{
  GstSunAudioSrc *sunaudiosrc;
  GstCaps *allowed;

  sunaudiosrc = GST_SUNAUDIOSRC (gst_pad_get_parent (pad));

  allowed = gst_pad_get_allowed_caps (pad);

  if (gst_pad_try_set_caps (sunaudiosrc->srcpad,
          gst_caps_new_simple ("audio/x-raw-int",
              "width", G_TYPE_INT, sunaudiosrc->width,
              "rate", G_TYPE_INT, sunaudiosrc->rate,
              "channels", G_TYPE_INT, sunaudiosrc->channels, NULL)) <= 0) {
    return FALSE;
  }
  return TRUE;
}

static GstData *
gst_sunaudiosrc_get (GstPad * pad)
{
  GstBuffer *buf;
  glong readbytes;
  glong readsamples;
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIOSRC (gst_pad_get_parent (pad));

  buf = gst_buffer_new_and_alloc (sunaudiosrc->buffer_size);

  if (!GST_PAD_CAPS (pad)) {
    /* nothing was negotiated, we can decide on a format */
    if (!gst_sunaudiosrc_negotiate (pad)) {
      gst_buffer_unref (buf);
      GST_ELEMENT_ERROR (sunaudiosrc, CORE, NEGOTIATION, (NULL), (NULL));
      return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
    }
  }

  readbytes =
      read (GST_SUNAUDIOELEMENT (sunaudiosrc)->fd, GST_BUFFER_DATA (buf),
      sunaudiosrc->buffer_size);

  if (readbytes < 0) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  if (readbytes == 0) {
    gst_buffer_unref (buf);
    gst_element_set_eos (GST_ELEMENT (sunaudiosrc));
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  readsamples = readbytes * sunaudiosrc->rate;

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = sunaudiosrc->curoffset;
  GST_BUFFER_OFFSET_END (buf) = sunaudiosrc->curoffset + readsamples;
  GST_BUFFER_DURATION (buf) = readsamples * GST_SECOND / sunaudiosrc->rate;

  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;

  sunaudiosrc->curoffset += readsamples;

  return GST_DATA (buf);
}

static GstCaps *
gst_sunaudiosrc_getcaps (GstPad * pad)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIOSRC (gst_pad_get_parent (pad));
  GstCaps *caps;

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GST_DEBUG ("getcaps called on %" GST_PTR_FORMAT ", returning %"
      GST_PTR_FORMAT, pad, caps);

  return caps;
}

static GstPadLinkReturn
gst_sunaudiosrc_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstSunAudioSrc *sunaudiosrc = GST_SUNAUDIOSRC (gst_pad_get_parent (pad));
  GstPadLinkReturn ret;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "rate", &sunaudiosrc->rate);
  gst_structure_get_int (structure, "width", &sunaudiosrc->width);
  gst_structure_get_int (structure, "channels", &sunaudiosrc->channels);

  if (gst_sunaudiosrc_setparams (sunaudiosrc)) {
    ret = GST_PAD_LINK_OK;
  } else {
    ret = GST_PAD_LINK_REFUSED;
  }
  GST_DEBUG ("pad_link called on %" GST_PTR_FORMAT " with caps %"
      GST_PTR_FORMAT ", returning %d", pad, caps, ret);

  return ret;
}

static gboolean
gst_sunaudiosrc_setparams (GstSunAudioSrc * sunaudiosrc)
{
  audio_info_t ainfo;
  int ret;

  AUDIO_INITINFO (&ainfo);

  ainfo.record.sample_rate = sunaudiosrc->rate;
  ainfo.record.channels = sunaudiosrc->channels;
  ainfo.record.precision = sunaudiosrc->width;
  ainfo.record.encoding = AUDIO_ENCODING_LINEAR;
  ainfo.record.port = AUDIO_MICROPHONE;
  ainfo.record.buffer_size = sunaudiosrc->buffer_size;
  /* ainfo.output_muted = 0; */

  ret = ioctl (GST_SUNAUDIOELEMENT (sunaudiosrc)->fd, AUDIO_SETINFO, &ainfo);
  if (ret == -1) {
    GST_ELEMENT_ERROR (sunaudiosrc, RESOURCE, SETTINGS, (NULL), ("%s",
            strerror (errno)));
    return FALSE;
  }

  return TRUE;
}
