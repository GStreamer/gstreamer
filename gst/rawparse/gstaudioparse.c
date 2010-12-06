/* GStreamer
 * Copyright (C) 2007 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstaudioparse.c:
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
/**
 * SECTION:element-audioparse
 *
 * Converts a byte stream into audio frames.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstaudioparse.h"

#include <gst/audio/multichannel.h>

typedef enum
{
  GST_AUDIO_PARSE_FORMAT_INT,
  GST_AUDIO_PARSE_FORMAT_FLOAT,
  GST_AUDIO_PARSE_FORMAT_MULAW,
  GST_AUDIO_PARSE_FORMAT_ALAW
} GstAudioParseFormat;

typedef enum
{
  GST_AUDIO_PARSE_ENDIANNESS_LITTLE = 1234,
  GST_AUDIO_PARSE_ENDIANNESS_BIG = 4321
} GstAudioParseEndianness;

static void gst_audio_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_audio_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_audio_parse_finalize (GObject * object);

static GstCaps *gst_audio_parse_get_caps (GstRawParse * rp);

static void gst_audio_parse_update_frame_size (GstAudioParse * ap);

GST_DEBUG_CATEGORY_STATIC (gst_audio_parse_debug);
#define GST_CAT_DEFAULT gst_audio_parse_debug

enum
{
  PROP_0,
  PROP_FORMAT,
  PROP_RATE,
  PROP_CHANNELS,
  PROP_ENDIANNESS,
  PROP_WIDTH,
  PROP_DEPTH,
  PROP_SIGNED,
  PROP_CHANNEL_POSITIONS
};


#define GST_AUDIO_PARSE_FORMAT (gst_audio_parse_format_get_type ())
static GType
gst_audio_parse_format_get_type (void)
{
  static GType audio_parse_format_type = 0;
  static const GEnumValue format_types[] = {
    {GST_AUDIO_PARSE_FORMAT_INT, "Integer", "int"},
    {GST_AUDIO_PARSE_FORMAT_FLOAT, "Floating Point", "float"},
    {GST_AUDIO_PARSE_FORMAT_ALAW, "A-Law", "alaw"},
    {GST_AUDIO_PARSE_FORMAT_MULAW, "\302\265-Law", "mulaw"},
    {0, NULL, NULL}
  };

  if (!audio_parse_format_type) {
    audio_parse_format_type =
        g_enum_register_static ("GstAudioParseFormat", format_types);
  }

  return audio_parse_format_type;
}

#define GST_AUDIO_PARSE_ENDIANNESS (gst_audio_parse_endianness_get_type ())
static GType
gst_audio_parse_endianness_get_type (void)
{
  static GType audio_parse_endianness_type = 0;
  static const GEnumValue endian_types[] = {
    {GST_AUDIO_PARSE_ENDIANNESS_LITTLE, "Little Endian", "little"},
    {GST_AUDIO_PARSE_ENDIANNESS_BIG, "Big Endian", "big"},
    {0, NULL, NULL}
  };

  if (!audio_parse_endianness_type) {
    audio_parse_endianness_type =
        g_enum_register_static ("GstAudioParseEndianness", endian_types);
  }

  return audio_parse_endianness_type;
}

GST_BOILERPLATE (GstAudioParse, gst_audio_parse, GstRawParse,
    GST_TYPE_RAW_PARSE);

static void
gst_audio_parse_base_init (gpointer g_class)
{
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (g_class);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *caps;

  GST_DEBUG_CATEGORY_INIT (gst_audio_parse_debug, "audioparse", 0,
      "audioparse element");

  gst_element_class_set_details_simple (gstelement_class, "Audio Parse",
      "Filter/Audio",
      "Converts stream into audio frames",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  caps =
      gst_caps_from_string ("audio/x-raw-int,"
      " depth=(int) [ 1, 32 ],"
      " width=(int) { 8, 16, 24, 32 },"
      " endianness=(int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      " signed=(bool) { TRUE, FALSE },"
      " rate=(int) [ 1, MAX ],"
      " channels=(int) [ 1, MAX ]; "
      "audio/x-raw-float,"
      " width=(int) { 32, 64 },"
      " endianness=(int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      " rate=(int)[1,MAX], channels=(int)[1,MAX]; "
      "audio/x-alaw, rate=(int)[1,MAX], channels=(int)[1,MAX]; "
      "audio/x-mulaw, rate=(int)[1,MAX], channels=(int)[1,MAX]");

  gst_raw_parse_class_set_src_pad_template (rp_class, caps);
  gst_raw_parse_class_set_multiple_frames_per_buffer (rp_class, TRUE);
  gst_caps_unref (caps);
}

static void
gst_audio_parse_class_init (GstAudioParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (klass);

  gobject_class->set_property = gst_audio_parse_set_property;
  gobject_class->get_property = gst_audio_parse_get_property;
  gobject_class->finalize = gst_audio_parse_finalize;

  rp_class->get_caps = gst_audio_parse_get_caps;

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format",
          "Format of audio samples in raw stream", GST_AUDIO_PARSE_FORMAT,
          GST_AUDIO_PARSE_FORMAT_INT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RATE,
      g_param_spec_int ("rate", "Rate", "Rate of audio samples in raw stream",
          1, INT_MAX, 44100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CHANNELS,
      g_param_spec_int ("channels", "Channels",
          "Number of channels in raw stream", 1, INT_MAX, 2,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "Width",
          "Width of audio samples in raw stream", 1, INT_MAX, 16,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEPTH,
      g_param_spec_int ("depth", "Depth",
          "Depth of audio samples in raw stream", 1, INT_MAX, 16,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SIGNED,
      g_param_spec_boolean ("signed", "signed",
          "Sign of audio samples in raw stream", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ENDIANNESS,
      g_param_spec_enum ("endianness", "Endianness",
          "Endianness of audio samples in raw stream",
          GST_AUDIO_PARSE_ENDIANNESS, G_BYTE_ORDER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CHANNEL_POSITIONS,
      g_param_spec_value_array ("channel-positions", "Channel positions",
          "Channel positions used on the output",
          g_param_spec_enum ("channel-position", "Channel position",
              "Channel position of the n-th input",
              GST_TYPE_AUDIO_CHANNEL_POSITION,
              GST_AUDIO_CHANNEL_POSITION_NONE,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_audio_parse_init (GstAudioParse * ap, GstAudioParseClass * g_class)
{
  ap->format = GST_AUDIO_PARSE_FORMAT_INT;
  ap->channels = 2;
  ap->width = 16;
  ap->depth = 16;
  ap->signedness = TRUE;
  ap->endianness = G_BYTE_ORDER;

  gst_audio_parse_update_frame_size (ap);
  gst_raw_parse_set_fps (GST_RAW_PARSE (ap), 44100, 1);
}

static void
gst_audio_parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioParse *ap = GST_AUDIO_PARSE (object);

  g_return_if_fail (!gst_raw_parse_is_negotiated (GST_RAW_PARSE (ap)));

  switch (prop_id) {
    case PROP_FORMAT:
      ap->format = g_value_get_enum (value);
      break;
    case PROP_RATE:
      gst_raw_parse_set_fps (GST_RAW_PARSE (ap), g_value_get_int (value), 1);
      break;
    case PROP_CHANNELS:
      ap->channels = g_value_get_int (value);
      break;
    case PROP_WIDTH:
      ap->width = g_value_get_int (value);
      break;
    case PROP_DEPTH:
      ap->depth = g_value_get_int (value);
      break;
    case PROP_SIGNED:
      ap->signedness = g_value_get_boolean (value);
      break;
    case PROP_ENDIANNESS:
      ap->endianness = g_value_get_enum (value);
      break;
    case PROP_CHANNEL_POSITIONS:
      if (ap->channel_positions)
        g_value_array_free (ap->channel_positions);

      ap->channel_positions = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_audio_parse_update_frame_size (ap);
}

static void
gst_audio_parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAudioParse *ap = GST_AUDIO_PARSE (object);

  switch (prop_id) {
    case PROP_FORMAT:
      g_value_set_enum (value, ap->format);
      break;
    case PROP_RATE:{
      gint fps_n, fps_d;

      gst_raw_parse_get_fps (GST_RAW_PARSE (ap), &fps_n, &fps_d);
      g_value_set_int (value, fps_n);
      break;
    }
    case PROP_CHANNELS:
      g_value_set_int (value, ap->channels);
      break;
    case PROP_WIDTH:
      g_value_set_int (value, ap->width);
      break;
    case PROP_DEPTH:
      g_value_set_int (value, ap->depth);
      break;
    case PROP_SIGNED:
      g_value_set_boolean (value, ap->signedness);
      break;
    case PROP_ENDIANNESS:
      g_value_set_enum (value, ap->endianness);
      break;
    case PROP_CHANNEL_POSITIONS:
      g_value_set_boxed (value, ap->channel_positions);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_audio_parse_finalize (GObject * object)
{
  GstAudioParse *ap = GST_AUDIO_PARSE (object);

  if (ap->channel_positions) {
    g_value_array_free (ap->channel_positions);
    ap->channel_positions = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gst_audio_parse_update_frame_size (GstAudioParse * ap)
{
  gint framesize, width;

  switch (ap->format) {
    case GST_AUDIO_PARSE_FORMAT_ALAW:
    case GST_AUDIO_PARSE_FORMAT_MULAW:
      width = 8;
      break;
    case GST_AUDIO_PARSE_FORMAT_INT:
    case GST_AUDIO_PARSE_FORMAT_FLOAT:
    default:
      width = ap->width;
      break;
  }

  framesize = (width / 8) * ap->channels;

  gst_raw_parse_set_framesize (GST_RAW_PARSE (ap), framesize);
}

static gboolean
gst_audio_parse_check_channel_positions (GValueArray * positions)
{
  gint i;
  guint channels;
  GstAudioChannelPosition *pos;
  gboolean ret;

  channels = positions->n_values;
  pos = g_new (GstAudioChannelPosition, positions->n_values);

  for (i = 0; i < channels; i++) {
    GValue *v = g_value_array_get_nth (positions, i);

    pos[i] = g_value_get_enum (v);
  }

  ret = gst_audio_check_channel_positions (pos, channels);
  g_free (pos);

  return ret;
}

static void
gst_audio_parse_set_channel_positions (GstAudioParse * ap, GstStructure * s)
{
  GValue pos_array = { 0, };
  gint i;

  if (!ap->channel_positions && ap->channels <= 2) {
    /* Implicit mapping for 1- and 2-channel audio is okay */
    return;
  }

  g_value_init (&pos_array, GST_TYPE_ARRAY);

  if (ap->channel_positions
      && ap->channels == ap->channel_positions->n_values
      && gst_audio_parse_check_channel_positions (ap->channel_positions)) {
    GST_DEBUG_OBJECT (ap, "Using provided channel positions");
    for (i = 0; i < ap->channels; i++)
      gst_value_array_append_value (&pos_array,
          g_value_array_get_nth (ap->channel_positions, i));
  } else {
    /* >2 channels and no explicit mapping */
    GValue pos_none = { 0, };

    GST_WARNING_OBJECT (ap, "Using NONE channel positions");

    g_value_init (&pos_none, GST_TYPE_AUDIO_CHANNEL_POSITION);
    g_value_set_enum (&pos_none, GST_AUDIO_CHANNEL_POSITION_NONE);

    for (i = 0; i < ap->channels; i++)
      gst_value_array_append_value (&pos_array, &pos_none);

    g_value_unset (&pos_none);
  }
  gst_structure_set_value (s, "channel-positions", &pos_array);
  g_value_unset (&pos_array);
}

static GstCaps *
gst_audio_parse_get_caps (GstRawParse * rp)
{
  GstAudioParse *ap = GST_AUDIO_PARSE (rp);
  GstCaps *caps;

  gint fps_n, fps_d;

  gst_raw_parse_get_fps (rp, &fps_n, &fps_d);

  switch (ap->format) {
    case GST_AUDIO_PARSE_FORMAT_INT:
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels,
          "width", G_TYPE_INT, ap->width,
          "depth", G_TYPE_INT, ap->depth,
          "signed", G_TYPE_BOOLEAN, ap->signedness,
          "endianness", G_TYPE_INT, ap->endianness, NULL);
      break;
    case GST_AUDIO_PARSE_FORMAT_FLOAT:
      caps = gst_caps_new_simple ("audio/x-raw-float",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels,
          "width", G_TYPE_INT, ap->width,
          "endianness", G_TYPE_INT, ap->endianness, NULL);
      break;
    case GST_AUDIO_PARSE_FORMAT_ALAW:
      caps = gst_caps_new_simple ("audio/x-alaw",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels, NULL);
      break;
    case GST_AUDIO_PARSE_FORMAT_MULAW:
      caps = gst_caps_new_simple ("audio/x-mulaw",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels, NULL);
      break;
    default:
      caps = gst_caps_new_empty ();
      GST_ERROR_OBJECT (rp, "unexpected format %d", ap->format);
      break;
  }

  gst_audio_parse_set_channel_positions (ap, gst_caps_get_structure (caps, 0));

  return caps;
}
