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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-audioparse
 *
 * Converts a byte stream into audio frames.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as g_value_array stuff
 * for now with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "gstaudioparse.h"

#include <string.h>

typedef enum
{
  GST_AUDIO_PARSE_FORMAT_RAW,
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
  PROP_RAW_FORMAT,
  PROP_RATE,
  PROP_CHANNELS,
  PROP_INTERLEAVED,
  PROP_CHANNEL_POSITIONS,
  PROP_USE_SINK_CAPS
};

#define GST_AUDIO_PARSE_FORMAT (gst_audio_parse_format_get_type ())
static GType
gst_audio_parse_format_get_type (void)
{
  static GType audio_parse_format_type = 0;
  static const GEnumValue format_types[] = {
    {GST_AUDIO_PARSE_FORMAT_RAW, "Raw", "raw"},
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


#define gst_audio_parse_parent_class parent_class
G_DEFINE_TYPE (GstAudioParse, gst_audio_parse, GST_TYPE_RAW_PARSE);

static void
gst_audio_parse_class_init (GstAudioParseClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstRawParseClass *rp_class = GST_RAW_PARSE_CLASS (klass);
  GstCaps *caps;

  gobject_class->set_property = gst_audio_parse_set_property;
  gobject_class->get_property = gst_audio_parse_get_property;
  gobject_class->finalize = gst_audio_parse_finalize;

  rp_class->get_caps = gst_audio_parse_get_caps;

  g_object_class_install_property (gobject_class, PROP_FORMAT,
      g_param_spec_enum ("format", "Format",
          "Format of audio samples in raw stream", GST_AUDIO_PARSE_FORMAT,
          GST_AUDIO_PARSE_FORMAT_RAW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RAW_FORMAT,
      g_param_spec_enum ("raw-format", "Raw Format",
          "Format of audio samples in raw stream", GST_TYPE_AUDIO_FORMAT,
          GST_AUDIO_FORMAT_S16, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RATE,
      g_param_spec_int ("rate", "Rate", "Rate of audio samples in raw stream",
          1, INT_MAX, 44100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CHANNELS,
      g_param_spec_int ("channels", "Channels",
          "Number of channels in raw stream", 1, 64, 2,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INTERLEAVED,
      g_param_spec_boolean ("interleaved", "Interleaved Layout",
          "True if audio has interleaved layout", TRUE,
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

  g_object_class_install_property (gobject_class, PROP_USE_SINK_CAPS,
      g_param_spec_boolean ("use-sink-caps", "Use sink caps",
          "Use the sink caps for the format, only performing timestamping",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Audio Parse",
      "Filter/Audio",
      "Converts stream into audio frames",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  caps = gst_caps_from_string (GST_AUDIO_CAPS_MAKE (GST_AUDIO_FORMATS_ALL)
      ", layout = (string) { interleaved, non-interleaved }; "
      "audio/x-alaw, rate=(int)[1,MAX], channels=(int)[1,MAX]; "
      "audio/x-mulaw, rate=(int)[1,MAX], channels=(int)[1,MAX]");

  gst_raw_parse_class_set_src_pad_template (rp_class, caps);
  gst_raw_parse_class_set_multiple_frames_per_buffer (rp_class, TRUE);
  gst_caps_unref (caps);

  GST_DEBUG_CATEGORY_INIT (gst_audio_parse_debug, "audioparse", 0,
      "audioparse element");
}

static void
gst_audio_parse_init (GstAudioParse * ap)
{
  ap->format = GST_AUDIO_PARSE_FORMAT_RAW;
  ap->raw_format = GST_AUDIO_FORMAT_S16;
  ap->channels = 2;
  ap->interleaved = TRUE;

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
    case PROP_RAW_FORMAT:
      ap->raw_format = g_value_get_enum (value);
      break;
    case PROP_RATE:
      gst_raw_parse_set_fps (GST_RAW_PARSE (ap), g_value_get_int (value), 1);
      break;
    case PROP_CHANNELS:
      ap->channels = g_value_get_int (value);
      break;
    case PROP_INTERLEAVED:
      ap->interleaved = g_value_get_boolean (value);
      break;
    case PROP_CHANNEL_POSITIONS:
      if (ap->channel_positions)
        g_value_array_free (ap->channel_positions);

      ap->channel_positions = g_value_dup_boxed (value);
      break;
    case PROP_USE_SINK_CAPS:
      ap->use_sink_caps = g_value_get_boolean (value);
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
    case PROP_RAW_FORMAT:
      g_value_set_enum (value, ap->raw_format);
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
    case PROP_INTERLEAVED:
      g_value_set_boolean (value, ap->interleaved);
      break;
    case PROP_CHANNEL_POSITIONS:
      g_value_set_boxed (value, ap->channel_positions);
      break;
    case PROP_USE_SINK_CAPS:
      g_value_set_boolean (value, ap->use_sink_caps);
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

  g_free (ap->channel_pos);
  g_free (ap->channel_order);

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
    case GST_AUDIO_PARSE_FORMAT_RAW:
    default:
    {
      GstAudioInfo info;

      gst_audio_info_init (&info);
      /* rate, etc do not really matter here */
      gst_audio_info_set_format (&info, ap->raw_format, 44100, ap->channels,
          NULL);
      width = GST_AUDIO_INFO_WIDTH (&info);
      break;
    }
  }

  framesize = (width / 8) * ap->channels;

  gst_raw_parse_set_framesize (GST_RAW_PARSE (ap), framesize);
}

static GstAudioChannelPosition *
gst_audio_parse_get_channel_positions (GValueArray * positions)
{
  gint i;
  guint channels;
  GstAudioChannelPosition *pos;

  channels = positions->n_values;
  pos = g_new (GstAudioChannelPosition, positions->n_values);

  for (i = 0; i < channels; i++) {
    GValue *v = g_value_array_get_nth (positions, i);

    pos[i] = g_value_get_enum (v);
  }

  return pos;
}

static void
gst_audio_parse_setup_channel_positions (GstAudioParse * ap)
{
  GstAudioChannelPosition *pos, *to;

  g_free (ap->channel_pos);
  g_free (ap->channel_order);
  ap->channel_pos = NULL;
  ap->channel_order = NULL;

  if (!ap->channel_positions) {
    GST_DEBUG_OBJECT (ap, "no channel positions");
    /* implicit mapping for 1- and 2-channel audio is okay */
    /* will come up with one in other cases also */
    return;
  }

  pos = gst_audio_parse_get_channel_positions (ap->channel_positions);
  if (ap->channels != ap->channel_positions->n_values ||
      !gst_audio_check_valid_channel_positions (pos, ap->channels, FALSE)) {
    GST_DEBUG_OBJECT (ap, "invalid channel position");
    g_free (pos);
    return;
  }

  /* ok, got something we can work with now */
  to = g_new (GstAudioChannelPosition, ap->channels);
  memcpy (to, pos, ap->channels * sizeof (to[0]));
  gst_audio_channel_positions_to_valid_order (to, ap->channels);

  ap->channel_pos = pos;
  ap->channel_order = to;
}

static GstCaps *
gst_audio_parse_get_caps (GstRawParse * rp)
{
  GstAudioParse *ap = GST_AUDIO_PARSE (rp);
  GstCaps *caps, *ncaps;
  GstAudioInfo info;
  gint fps_n, fps_d;
  const GValue *val;

  if (ap->use_sink_caps) {
    gint rate;
    GstCaps *caps = gst_pad_get_current_caps (rp->sinkpad);
    if (!caps) {
      GST_WARNING_OBJECT (ap,
          "Sink pad has no caps, but we were asked to use its caps");
      return NULL;
    }
    if (!gst_audio_info_from_caps (&info, caps)) {
      GST_WARNING_OBJECT (ap, "Failed to parse caps %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);
      return NULL;
    }

    ap->format = GST_AUDIO_PARSE_FORMAT_RAW;
    ap->raw_format = GST_AUDIO_INFO_FORMAT (&info);
    ap->channels = GST_AUDIO_INFO_CHANNELS (&info);
    ap->interleaved = info.layout == GST_AUDIO_LAYOUT_INTERLEAVED;

    rate = GST_AUDIO_INFO_RATE (&info);
    gst_raw_parse_set_fps (GST_RAW_PARSE (ap), rate, 1);
    gst_audio_parse_update_frame_size (ap);

    return caps;
  }

  gst_raw_parse_get_fps (rp, &fps_n, &fps_d);
  gst_audio_parse_setup_channel_positions (ap);

  /* yes, even when format not raw */
  gst_audio_info_init (&info);
  gst_audio_info_set_format (&info, ap->raw_format, fps_n, ap->channels,
      ap->channel_order);
  info.layout = ap->interleaved ? GST_AUDIO_LAYOUT_INTERLEAVED :
      GST_AUDIO_LAYOUT_NON_INTERLEAVED;
  caps = gst_audio_info_to_caps (&info);

  switch (ap->format) {
    case GST_AUDIO_PARSE_FORMAT_RAW:
      break;
    case GST_AUDIO_PARSE_FORMAT_ALAW:
      ncaps = gst_caps_new_simple ("audio/x-alaw",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels, NULL);
      /* pick mask stuff from faked raw format */
      val = gst_structure_get_value (gst_caps_get_structure (caps, 0),
          "channel-mask");
      if (val)
        gst_caps_set_value (ncaps, "channel-mask", val);
      gst_caps_unref (caps);
      caps = ncaps;
      break;
    case GST_AUDIO_PARSE_FORMAT_MULAW:
      ncaps = gst_caps_new_simple ("audio/x-mulaw",
          "rate", G_TYPE_INT, fps_n,
          "channels", G_TYPE_INT, ap->channels, NULL);
      /* pick mask stuff from faked raw format */
      val = gst_structure_get_value (gst_caps_get_structure (caps, 0),
          "channel-mask");
      if (val)
        gst_caps_set_value (ncaps, "channel-mask", val);
      gst_caps_unref (caps);
      caps = ncaps;
      break;
    default:
      caps = gst_caps_new_empty ();
      GST_ERROR_OBJECT (rp, "unexpected format %d", ap->format);
      break;
  }

  return caps;
}
