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

/*
 * Portions derived from maswavplay.c (distributed under the X11
 * license):
 *
 * Copyright (c) 2001-2003 Shiman Associates Inc. All Rights Reserved.
 * Copyright (c) 2000, 2001 by Shiman Associates Inc. and Sun
 * Microsystems, Inc. All Rights Reserved.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmassink.h"


/* Signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_MUTE,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, " "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", " "signed = (boolean) TRUE, "      /* We dont deal with unsigned creatures */
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = [ 8000, 96000 ], "
        "channels = [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) TRUE, "
        "width = (int) 8, "
        "depth = (int) 8, " "rate = [ 8000, 96000 ], " "channels = [ 1, 2 ]"));

static void gst_massink_base_init (gpointer g_class);
static void gst_massink_class_init (GstMassinkClass * klass);
static void gst_massink_init (GstMassink * massink);
static void gst_massink_set_clock (GstElement * element, GstClock * clock);
static gboolean gst_massink_open_audio (GstMassink * sink);
static void gst_massink_close_audio (GstMassink * sink);
static GstElementStateReturn gst_massink_change_state (GstElement * element);
static gboolean gst_massink_sync_parms (GstMassink * massink);
static GstPadLinkReturn gst_massink_sinkconnect (GstPad * pad,
    const GstCaps * caps);

static void gst_massink_chain (GstPad * pad, GstData * _data);

static void gst_massink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_massink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_massink_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_massink_get_type (void)
{
  static GType massink_type = 0;

  if (!massink_type) {
    static const GTypeInfo massink_info = {
      sizeof (GstMassinkClass),
      gst_massink_base_init,
      NULL,
      (GClassInitFunc) gst_massink_class_init,
      NULL,
      NULL,
      sizeof (GstMassink),
      0,
      (GInstanceInitFunc) gst_massink_init,
    };

    massink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstMassink", &massink_info,
        0);
  }
  return massink_type;
}

static void
gst_massink_base_init (gpointer g_class)
{
  static GstElementDetails massink_details =
      GST_ELEMENT_DETAILS ("MAS audio sink",
      "Sink/Audio",
      "Plays audio to a MAS server",
      "Zeeshan Ali <zeenix@gmail.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &massink_details);
}

static void
gst_massink_class_init (GstMassinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MUTE, g_param_spec_boolean ("mute", "mute", "mute", FALSE, G_PARAM_READWRITE));  /* CHECKME */

  gobject_class->set_property = gst_massink_set_property;
  gobject_class->get_property = gst_massink_get_property;

  gstelement_class->change_state = gst_massink_change_state;
  gstelement_class->set_clock = gst_massink_set_clock;
}

static void
gst_massink_set_clock (GstElement * element, GstClock * clock)
{
  GstMassink *massink;

  massink = GST_MASSINK (element);

  massink->clock = clock;
}

static void
gst_massink_init (GstMassink * massink)
{
  gint32 err;
  guint32 from, to;

  massink->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_factory),
      "sink");
  gst_element_add_pad (GST_ELEMENT (massink), massink->sinkpad);
  gst_pad_set_chain_function (massink->sinkpad,
      GST_DEBUG_FUNCPTR (gst_massink_chain));
  gst_pad_set_link_function (massink->sinkpad, gst_massink_sinkconnect);

  massink->mute = FALSE;
  massink->depth = MASSINK_DEFAULT_DEPTH;
  massink->channels = MASSINK_DEFAULT_CHANNELS;
  massink->frequency = MASSINK_DEFAULT_FREQUENCY;

  /* MAS API connects to the MAS server on library initialization and
   * there is no way of connecting/disconnecting with the server
   * after the library initialization. So there is no point
   * in having this code in gst_massink_open_audio
   */
  GST_DEBUG ("Connecting to MAS server..\n");
  masc_log_verbosity (MAS_VERBLVL_DEBUG);
  err = mas_init ();

  if (err < 0) {
    GST_DEBUG ("Connection with MAS server failed.");
    /* What else should/can I do to signal an error from an instance init 
     * function?
     */
    exit (1);
  }

  /* Inititialize everything to 0 */
  from = G_STRUCT_OFFSET (GstMassink, audio_channel);
  to = G_STRUCT_OFFSET (GstMassink, data) + sizeof (massink->data);
  memset ((gchar *) massink + from, 0, to - from);
}

static gboolean
gst_massink_sync_parms (GstMassink * massink)
{
  g_return_val_if_fail (massink != NULL, FALSE);
  g_return_val_if_fail (GST_IS_MASSINK (massink), FALSE);

  if (GST_FLAG_IS_SET (GST_ELEMENT (massink), GST_MASSINK_OPEN)) {
    gst_massink_close_audio (massink);
    return gst_massink_open_audio (massink);
  }

  else {
    return TRUE;
  }
}

static GstPadLinkReturn
gst_massink_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstMassink *massink;
  GstStructure *structure;

  massink = GST_MASSINK (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "depth", &massink->depth);

  gst_structure_get_int (structure, "endianness", &massink->endianness);
  //gst_structure_get_boolean (structure, "signed", &massink->sign);

  gst_structure_get_int (structure, "channels", &massink->channels);
  gst_structure_get_int (structure, "rate", &massink->frequency);

  if (gst_massink_sync_parms (massink)) {
    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_massink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  gint32 err;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  GstMassink *massink = GST_MASSINK (gst_pad_get_parent (pad));

  if (massink->clock && GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    GST_DEBUG ("massink: clock wait: %llu\n", GST_BUFFER_TIMESTAMP (buf));
    gst_element_wait (GST_ELEMENT (massink), GST_BUFFER_TIMESTAMP (buf));
  }

  if (GST_BUFFER_DATA (buf) != NULL) {
    if (!massink->mute) {
      GST_DEBUG ("massink: data=%p size=%d", GST_BUFFER_DATA (buf),
          GST_BUFFER_SIZE (buf));
      if (GST_BUFFER_SIZE (buf) > MASSINK_BUFFER_SIZE) {
        gst_buffer_unref (buf);
        return;
      }

      massink->data.length = GST_BUFFER_SIZE (buf);

      memcpy (massink->data.segment, GST_BUFFER_DATA (buf),
          GST_BUFFER_SIZE (buf));

      err = mas_send (massink->audio_channel, &massink->data);

      if (err < 0) {
        GST_DEBUG ("Error sending data to MAS server\n");
        gst_buffer_unref (buf);
        return;
      }

      /* FIXME: Please correct the Timestamping if its wrong */
      massink->data.header.media_timestamp += massink->data.length / 4;
      massink->data.header.sequence++;
    }
  }

  gst_buffer_unref (buf);
}

static void
gst_massink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMassink *massink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MASSINK (object));
  massink = GST_MASSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      massink->mute = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_massink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMassink *massink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MASSINK (object));
  massink = GST_MASSINK (object);

  switch (prop_id) {
    case ARG_MUTE:
      g_value_set_boolean (value, massink->mute);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "massink", GST_RANK_NONE,
          GST_TYPE_MASSINK)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "massink",
    "uses MAS for audio output",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN);

static gboolean
gst_massink_open_audio (GstMassink * massink)
{
  gint32 err;
  struct mas_data_characteristic *dc;

  GST_DEBUG ("Establishing audio output channel.");
  err =
      mas_make_data_channel ("Gstreamer", &massink->audio_channel,
      &massink->audio_source, &massink->audio_sink);
  if (err < 0) {
    massink->audio_channel = 0;
    massink->audio_source = massink->audio_sink = 0;
    GST_DEBUG ("Failed to create data channel");
    return FALSE;
  }

  err = mas_asm_get_port_by_name (0, "default_mix_sink", &massink->mix_sink);
  if (err < 0) {
    GST_DEBUG ("Failed to get default_mix_sink");
    massink->mix_sink = 0;
    return FALSE;
  }

  GST_DEBUG ("Instantiating endian device.");
  err = mas_asm_instantiate_device ("endian", 0, 0, &massink->endian);
  if (err < 0) {
    GST_DEBUG ("Failed to instantiate endian device");
    massink->endian = 0;
    return FALSE;
  }

  err =
      mas_asm_get_port_by_name (massink->endian, "sink", &massink->endian_sink);
  if (err < 0) {
    GST_DEBUG ("Failed to get sink port from endian device");
    massink->endian_sink = 0;
    return FALSE;
  }

  err =
      mas_asm_get_port_by_name (massink->endian, "source",
      &massink->endian_source);
  if (err < 0) {
    GST_DEBUG ("Failed to get source port from endian device");
    massink->endian_source = 0;
    return FALSE;
  }

  GST_DEBUG ("Connecting net -> endian.");

  dc = masc_make_audio_basic_dc (
      (massink->depth == 8) ? MAS_ULINEAR_FMT : MAS_LINEAR_FMT,
      massink->frequency,
      massink->depth,
      massink->channels,
      (massink->endianness == G_LITTLE_ENDIAN) ? MAS_LITTLE_ENDIAN_FMT :
      MAS_BIG_ENDIAN_FMT);
  mas_assert (dc != 0, "Memory allocation error");

  err =
      mas_asm_connect_source_sink (massink->audio_source, massink->endian_sink,
      dc);

  masc_strike_dc (dc);
  masc_rtfree (dc);

  if (err < 0) {
    GST_DEBUG ("Failed to connect Channel Converter output to endian");
    return FALSE;
  }

  /* The next device is 'if needed' only. After the following if()
     statement, open_source will contain the current unconnected
     source in the path (will be either endian_source or
     squant_source in this case)
   */

  massink->open_source = massink->endian_source;

  if (massink->channels != 2) {
    GST_DEBUG ("Instantiating Channel Converter device.");
    err =
        mas_asm_instantiate_device ("channelconv", 0, 0, &massink->channelconv);
    if (err < 0) {
      GST_DEBUG ("Failed to instantiate Channel Converter device");
      massink->channelconv = 0;
      return FALSE;
    }

    err =
        mas_asm_get_port_by_name (massink->channelconv, "sink",
        &massink->channelconv_sink);
    if (err < 0) {
      GST_DEBUG ("Failed to get sink port from Channel Converter device");
      massink->channelconv_sink = 0;
      return FALSE;
    }

    err =
        mas_asm_get_port_by_name (massink->channelconv, "source",
        &massink->channelconv_source);
    if (err < 0) {
      GST_DEBUG ("Failed to get source port from Channel Converter device");
      massink->channelconv_source = 0;
      return FALSE;
    }

    GST_DEBUG ("Connecting audio -> channelconv.");

    dc = masc_make_audio_basic_dc (
        (massink->depth == 8) ? MAS_ULINEAR_FMT : MAS_LINEAR_FMT,
        massink->frequency,
        massink->depth, massink->channels, MAS_HOST_ENDIAN_FMT);
    mas_assert (dc != 0, "Memory allocation error");

    err =
        mas_asm_connect_source_sink (massink->open_source,
        massink->channelconv_sink, dc);

    masc_strike_dc (dc);
    masc_rtfree (dc);

    if (err < 0) {
      GST_DEBUG ("Failed to connect endian device to Channel converter");
      return FALSE;
    }

    massink->open_source = massink->channelconv_source;
  }

  if (massink->depth != 16) {
    GST_DEBUG
        ("Sample resolution is not 16 bit/sample, instantiating squant device.");
    err = mas_asm_instantiate_device ("squant", 0, 0, &massink->squant);
    if (err < 0) {
      massink->squant = 0;
      GST_DEBUG ("Failed to instantiate squant device");
      return FALSE;
    }

    err =
        mas_asm_get_port_by_name (massink->squant, "sink",
        &massink->squant_sink);
    if (err < 0) {
      massink->squant_sink = 0;
      GST_DEBUG ("Failed to get sink port from squant device");
      return FALSE;
    }

    err =
        mas_asm_get_port_by_name (massink->squant, "source",
        &massink->squant_source);
    if (err < 0) {
      massink->squant_source = 0;
      GST_DEBUG ("Failed to get source port from squant device");
      return FALSE;
    }

    GST_DEBUG ("Connecting endian -> squant.");

    dc = masc_make_audio_basic_dc (
        (massink->depth == 8) ? MAS_ULINEAR_FMT : MAS_LINEAR_FMT,
        massink->frequency, massink->depth, 2, MAS_HOST_ENDIAN_FMT);
    mas_assert (dc != 0, "Memory allocation error");

    err =
        mas_asm_connect_source_sink (massink->open_source, massink->squant_sink,
        dc);

    masc_strike_dc (dc);
    masc_rtfree (dc);

    if (err < 0) {
      GST_DEBUG ("Failed to connect endian output to squant");
      return FALSE;
    }

    /* sneaky: the squant device is optional -> pretend it isn't there */
    massink->open_source = massink->squant_source;
  }

  /* Another 'if necessary' device, as above */
  if (massink->frequency != 44100) {
    GST_DEBUG ("Sample rate is not 44100, instantiating srate device.");
    err = mas_asm_instantiate_device ("srate", 0, 0, &massink->srate);

    if (err < 0) {
      massink->srate = 0;
      GST_DEBUG ("Failed to instantiate srate device");
      return FALSE;
    }

    mas_asm_get_port_by_name (massink->srate, "sink", &massink->srate_sink);
    if (err < 0) {
      massink->srate_sink = 0;
      GST_DEBUG ("Failed to get sink port from srate device");
      return FALSE;
    }

    mas_asm_get_port_by_name (massink->srate, "source", &massink->srate_source);
    if (err < 0) {
      massink->srate_source = 0;
      GST_DEBUG ("Failed to get source port from srate device");
      return FALSE;
    }

    GST_DEBUG ("Connecting to srate.");

    dc = masc_make_audio_basic_dc (MAS_LINEAR_FMT,
        massink->frequency, 16, 2, MAS_HOST_ENDIAN_FMT);
    mas_assert (dc != 0, "Memory allocation error");

    err =
        mas_asm_connect_source_sink (massink->open_source, massink->srate_sink,
        dc);
    masc_strike_dc (dc);
    masc_rtfree (dc);

    if (err < 0) {
      GST_DEBUG ("Failed to connect to srate");
      return FALSE;
    }


    massink->open_source = massink->srate_source;
  }

  GST_DEBUG ("Connecting to mix.");

  dc = masc_make_audio_basic_dc (MAS_LINEAR_FMT,
      44100, 16, 2, MAS_HOST_ENDIAN_FMT);
  mas_assert (dc != 0, "Memory allocation error");
  err =
      mas_asm_connect_source_sink (massink->open_source, massink->mix_sink, dc);
  masc_strike_dc (dc);
  masc_rtfree (dc);

  if (err < 0) {
    GST_DEBUG ("Failed to connect to mixer");
    return FALSE;
  }

  massink->data.segment = masc_rtalloc (MASSINK_BUFFER_SIZE);
  massink->data.length = MASSINK_BUFFER_SIZE;
  massink->data.allocated_length = MASSINK_BUFFER_SIZE;

  massink->data.header.type = 10;

  massink->data.header.media_timestamp = 0;
  massink->data.header.sequence = 0;

  GST_FLAG_SET (massink, GST_MASSINK_OPEN);

  return TRUE;
}

static void
gst_massink_close_audio (GstMassink * massink)
{
  if (massink->mix_sink) {
    mas_free_port (massink->mix_sink);
    massink->mix_sink = 0;
  }

  if (massink->channelconv_source) {
    mas_free_port (massink->channelconv_source);
    massink->channelconv_source = 0;
  }

  if (massink->channelconv_sink) {
    mas_free_port (massink->channelconv_sink);
    massink->channelconv_sink = 0;
  }

  if (massink->srate_source) {
    mas_free_port (massink->srate_source);
    massink->srate_source = 0;
  }

  if (massink->srate_sink) {
    mas_free_port (massink->srate_sink);
    massink->srate_sink = 0;
  }

  if (massink->audio_source) {
    mas_free_port (massink->audio_source);
    massink->audio_source = 0;
  }

  if (massink->audio_sink) {
    mas_free_port (massink->audio_sink);
    massink->audio_sink = 0;
  }

  if (massink->endian_source) {
    mas_free_port (massink->endian_source);
    massink->endian_source = 0;
  }

  if (massink->endian_sink) {
    mas_free_port (massink->endian_sink);
    massink->endian_sink = 0;
  }

  if (massink->squant_source) {
    mas_free_port (massink->squant_source);
    massink->squant_source = 0;
  }

  if (massink->squant_sink) {
    mas_free_port (massink->squant_sink);
    massink->squant_sink = 0;
  }

  if (massink->channelconv) {
    mas_free_device (massink->channelconv);
    massink->channelconv = 0;
  }

  if (massink->endian) {
    mas_free_device (massink->endian);
    massink->endian = 0;
  }

  if (massink->srate) {
    mas_free_device (massink->srate);
    massink->srate = 0;
  }

  if (massink->squant) {
    mas_free_device (massink->squant);
    massink->squant = 0;
  }

  if (massink->audio_channel) {
    mas_free_channel (massink->audio_channel);
    massink->audio_channel = 0;
  }

  if (massink->data.segment) {
    masc_rtfree (massink->data.segment);
    memset (&massink->data, 0, sizeof (massink->data));
  }

  GST_FLAG_UNSET (massink, GST_MASSINK_OPEN);

  GST_DEBUG ("massink: closed sound channel");
}

static GstElementStateReturn
gst_massink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_MASSINK (element), FALSE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_MASSINK_OPEN))
      gst_massink_close_audio (GST_MASSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_MASSINK_OPEN)) {
      if (!gst_massink_open_audio (GST_MASSINK (element))) {
        gst_massink_close_audio (GST_MASSINK (element));
        return GST_STATE_FAILURE;
      }
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}
