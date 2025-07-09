/*
 * Copyright (C) 2025 Fraunhofer Institute for Integrated Circuits IIS
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeghdec.h"

#include <gst/pbutils/pbutils.h>
#include <string.h>

#define MAX_NUM_OUTPUT_CHANNELS 24
#define MAX_AUDIO_FRAME_SIZE 3072
#define MAX_OUTBUF_SIZE (MAX_NUM_OUTPUT_CHANNELS * MAX_AUDIO_FRAME_SIZE)

typedef struct
{
  gint channels;
  GstAudioChannelPosition positions[24];
} GstMpeghChannelLayout;

static const GstMpeghChannelLayout channel_layouts[] = {
  /* CICP 1: Mono */
  {1, {GST_AUDIO_CHANNEL_POSITION_MONO}},
  /* CICP 2: Stereo */
  {2, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          }},
  /* CICP 3: */
  {3, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          }},
  /* CICP 4: */
  {4, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
          }},
  /* CICP 5: */
  {5, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          }},
  /* CICP 6: */
  {6, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          }},
  /* CICP 7: */
  {8, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT,
          }},
  /* CICP 8: not defined */
  {0, {
          }},
  /* CICP 9: */
  {3, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
          }},
  /* CICP 10: */
  {4, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          }},
  /* CICP 11: */
  {7, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
          }},
  /* CICP 12: */
  {8, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          }},
  /* CICP 13: */
  {24, {
              GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE2,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
              GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_RIGHT,
          }},
  /* CICP 14: */
  {8, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
          }},
  /* CICP 15: */
  {12, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_LFE2,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER,
          }},
  /* CICP 16: */
  {10, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
          }},
  /* CICP 17: */
  {12, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
          }},
  /* CICP 18: */
  {14, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_CENTER,
          }},
  /* CICP 19: */
  {12, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
          }},
  /* CICP 20: */
  {14, {
              GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
              GST_AUDIO_CHANNEL_POSITION_LFE1,
              GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
              GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT,
              GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT,
              GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT,
          }},
};

enum
{
  PROP_0,
  PROP_MPEGH_TARGET_LAYOUT,
  PROP_MPEGH_TARGET_REFERENCE_LEVEL,
  PROP_MPEGH_DRC_EFFECT_TYPE,
  PROP_MPEGH_DRC_ATTENUATION_FACTOR,
  PROP_MPEGH_DRC_BOOST_FACTOR,
  PROP_MPEGH_ALBUM_MODE
};

#define PROP_DEFAULT_MPEGH_TARGET_LAYOUT (6)
#define PROP_DEFAULT_MPEGH_TARGET_REFERENCE_LEVEL (-24.0)
#define PROP_DEFAULT_MPEGH_DRC_EFFECT_TYPE (GST_MPEGH_DRC_EFFECT_TYPE_GENERAL)
#define PROP_DEFAULT_MPEGH_DRC_ATTENUATION_FACTOR (1.0)
#define PROP_DEFAULT_MPEGH_DRC_BOOST_FACTOR (1.0)
#define PROP_DEFAULT_MPEGH_ALBUM_MODE (FALSE)

/* Notes on MPEG-D DRC
 *
 * Suggested Target Reference Level + Effect Types + default based on device classes:
 *  Mobile Device: -16 LKFS, [2, 3], default: 3
 *  TV: -24 LKFS, [-1, 1, 2, 6], default: 6
 *  AVR: -31 LKFS. [-1, 1, 2, 6], default: 6
 */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "stream-format = (string) { mhas, raw }, "
        "framed = (boolean) true, "
        "stream-type = (string) single, "
        "profile = (string) baseline, "
        "level = (int) { 1, 2, 3, 4 }, " "rate = (int) 48000")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format=(string) " GST_AUDIO_NE (S32) ", "
        "layout=(string) interleaved, "
        "channels = (int) [ 1, 24 ], " "rate = (int) 48000")
    );

GST_DEBUG_CATEGORY_STATIC (gst_mpeghdec_debug);
#define GST_CAT_DEFAULT gst_mpeghdec_debug

static void gst_mpeghdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeghdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_mpeghdec_start (GstAudioDecoder * dec);
static gboolean gst_mpeghdec_stop (GstAudioDecoder * dec);
static gboolean gst_mpeghdec_set_format (GstAudioDecoder * dec, GstCaps * caps);
static GstFlowReturn gst_mpeghdec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * inbuf);
static void gst_mpeghdec_flush (GstAudioDecoder * dec, gboolean hard);

G_DEFINE_TYPE (GstMpeghDec, gst_mpeghdec, GST_TYPE_AUDIO_DECODER);

#define GST_MPEGH_EFFECT_TYPE (gst_mpegh_effect_type_get_type())
static GType
gst_mpegh_effect_type_get_type (void)
{
  static GType mpegh_drc_effect_type = 0;
  static const GEnumValue drc_effect_types[] = {
    {GST_MPEGH_DRC_EFFECT_TYPE_OFF, "Off", "off"},
    {GST_MPEGH_DRC_EFFECT_TYPE_NONE, "None", "none"},
    {GST_MPEGH_DRC_EFFECT_TYPE_NIGHT, "Late night", "night"},
    {GST_MPEGH_DRC_EFFECT_TYPE_NOISY, "Noisy environment", "noisy"},
    {GST_MPEGH_DRC_EFFECT_TYPE_LIMITED, "Limited playback range", "limited"},
    {GST_MPEGH_DRC_EFFECT_TYPE_LOWLEVEL, "Low playback level", "lowlevel"},
    {GST_MPEGH_DRC_EFFECT_TYPE_DIALOG, "Dialog enhancement", "dialog"},
    {GST_MPEGH_DRC_EFFECT_TYPE_GENERAL, "General compression", "general"},
    {0, NULL, NULL}
  };

  if (!mpegh_drc_effect_type) {
    mpegh_drc_effect_type =
        g_enum_register_static ("GstMpeghEffectType", drc_effect_types);
  }
  return mpegh_drc_effect_type;
}

static void
gst_mpeghdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeghDec *self = GST_MPEGHDEC (object);
  MPEGH_DECODER_ERROR err;
  GST_DEBUG_OBJECT (self, "set_property: property_id = %d", prop_id);
  switch (prop_id) {
    case PROP_MPEGH_TARGET_LAYOUT:
      GST_OBJECT_LOCK (object);
      self->target_layout = g_value_get_int (value);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_TARGET_REFERENCE_LEVEL:
      GST_OBJECT_LOCK (object);
      self->target_reference_level = g_value_get_float (value);
      /* If decoder is already initialized, also set on API directly to switch during runtime */
      if (self->dec) {
        /* Note: mpeghdec API needs the loudness value mapped to an int [40...127] */
        gint loudness = self->target_reference_level * -4;
        err =
            mpeghdecoder_setParam (self->dec,
            MPEGH_DEC_PARAM_TARGET_REFERENCE_LEVEL, loudness);
        if (err != MPEGH_DEC_OK) {
          GST_ERROR_OBJECT (self,
              "Failed to set drc reference level %d with error: %d", loudness,
              err);
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_EFFECT_TYPE:
      GST_OBJECT_LOCK (object);
      self->drc_effect_type = g_value_get_enum (value);
      /* If decoder is already initialized, also set on API directly to switch during runtime */
      if (self->dec) {
        err =
            mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_EFFECT_TYPE,
            self->drc_effect_type);
        if (err != MPEGH_DEC_OK) {
          GST_ERROR_OBJECT (self,
              "Failed to set drc effect type %d with error: %d",
              self->drc_effect_type, err);
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_ATTENUATION_FACTOR:
      GST_OBJECT_LOCK (object);
      self->drc_attenuation_factor = g_value_get_float (value);
      /* If decoder is already initialized, also set on API directly to switch during runtime */
      if (self->dec) {
        /* Note: FDK API needs the attenuation factor mapped to an int [0...127] */
        gint attenuation = self->drc_attenuation_factor * 127;
        err =
            mpeghdecoder_setParam (self->dec,
            MPEGH_DEC_PARAM_ATTENUATION_FACTOR, attenuation);
        if (err != MPEGH_DEC_OK) {
          GST_ERROR_OBJECT (self,
              "Failed to set drc attenuation factor %d with error: %d",
              attenuation, err);
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_BOOST_FACTOR:
      GST_OBJECT_LOCK (object);
      self->drc_boost_factor = g_value_get_float (value);
      /* If decoder is already initialized, also set on API directly to switch during runtime */
      if (self->dec) {
        /* Note: FDK API needs the boost factor mapped to an int [0...127] */
        gint boost = self->drc_boost_factor * 127;
        err =
            mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_BOOST_FACTOR,
            boost);
        if (err != MPEGH_DEC_OK) {
          GST_ERROR_OBJECT (self,
              "Failed to set drc boost factor %d with error: %d", boost, err);
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_ALBUM_MODE:
      GST_OBJECT_LOCK (object);
      self->album_mode = g_value_get_boolean (value);
      /* If decoder is already initialized, also set on API directly to switch during runtime */
      if (self->dec) {
        gint album_mode = self->album_mode ? 1 : 0;
        err =
            mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_ALBUM_MODE,
            album_mode);
        if (err != MPEGH_DEC_OK) {
          GST_ERROR_OBJECT (self,
              "Failed to set album mode %d with error: %d", album_mode, err);
        }
      }
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeghdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMpeghDec *self = GST_MPEGHDEC (object);
  GST_DEBUG_OBJECT (self, "get_property: property_id = %d", prop_id);
  switch (prop_id) {
    case PROP_MPEGH_TARGET_LAYOUT:
      GST_OBJECT_LOCK (object);
      g_value_set_int (value, self->target_layout);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_TARGET_REFERENCE_LEVEL:
      GST_OBJECT_LOCK (object);
      g_value_set_float (value, self->target_reference_level);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_EFFECT_TYPE:
      GST_OBJECT_LOCK (object);
      g_value_set_enum (value, self->drc_effect_type);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_ATTENUATION_FACTOR:
      GST_OBJECT_LOCK (object);
      g_value_set_float (value, self->drc_attenuation_factor);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_DRC_BOOST_FACTOR:
      GST_OBJECT_LOCK (object);
      g_value_set_float (value, self->drc_boost_factor);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_MPEGH_ALBUM_MODE:
      GST_OBJECT_LOCK (object);
      g_value_set_boolean (value, self->album_mode);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_mpeghdec_start (GstAudioDecoder * dec)
{
  GstMpeghDec *self = GST_MPEGHDEC (dec);
  GST_DEBUG_OBJECT (self, "start");

  self->samplerate = 0;
  self->channels = 0;
  return TRUE;
}

static gboolean
gst_mpeghdec_stop (GstAudioDecoder * dec)
{
  GstMpeghDec *self = GST_MPEGHDEC (dec);
  GST_DEBUG_OBJECT (self, "stop");

  if (self->dec)
    mpeghdecoder_destroy (self->dec);
  self->dec = NULL;
  return TRUE;
}

static gboolean
gst_mpeghdec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstMpeghDec *self = GST_MPEGHDEC (dec);
  GST_DEBUG_OBJECT (self, "set_format");

  gboolean ret = TRUE;
  gboolean is_raw = FALSE;
  GstStructure *s;
  MPEGH_DECODER_ERROR err;

  if (self->dec) {
    /* drain */
    gst_mpeghdec_handle_frame (dec, NULL);
    mpeghdecoder_destroy (self->dec);
    self->dec = NULL;
  }

  s = gst_caps_get_structure (caps, 0);
  const gchar *stream_format = gst_structure_get_string (s, "stream-format");
  if (strcmp (stream_format, "raw") == 0) {
    is_raw = TRUE;
  } else if (strcmp (stream_format, "mhas") == 0) {
    is_raw = FALSE;
  } else {
    g_assert_not_reached ();
  }

  GST_OBJECT_LOCK (dec);
  int target_layout = self->target_layout;
  GST_OBJECT_UNLOCK (dec);
  self->dec = mpeghdecoder_init (target_layout);
  if (!self->dec) {
    GST_ERROR_OBJECT (self,
        "mpeghdecoder_init FAILED! Maybe unsupported target layout(%d)",
        target_layout);
    ret = FALSE;
    goto out;
  }

  if (is_raw) {
    GstBuffer *codec_data = NULL;
    GstMapInfo map;
    const guint8 *data;
    guint size;
    gst_structure_get (s, "codec_data", GST_TYPE_BUFFER, &codec_data, NULL);
    if (!codec_data) {
      GST_ERROR_OBJECT (self, "MHA1 without codec_data not supported");
      ret = FALSE;
      goto out;
    }

    gst_buffer_map (codec_data, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;
    err = mpeghdecoder_setMhaConfig (self->dec, data, size);
    if (err != MPEGH_DEC_OK) {
      gst_buffer_unmap (codec_data, &map);
      gst_buffer_unref (codec_data);
      GST_ERROR_OBJECT (self, "Invalid codec_data: %d", err);
      ret = FALSE;
      goto out;
    }
    gst_buffer_unmap (codec_data, &map);
    gst_buffer_unref (codec_data);
  }

  /* Configure default target reference level parameter. */
  /* Note: FDK API needs the loudness value mapped to a int [40...127] */
  GST_OBJECT_LOCK (dec);
  gint loudness = self->target_reference_level * -4;
  GST_OBJECT_UNLOCK (dec);
  err =
      mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_TARGET_REFERENCE_LEVEL,
      loudness);
  if (err != MPEGH_DEC_OK) {
    GST_ERROR_OBJECT (self,
        "Failed to set drc reference level %d with error: %d", loudness, err);
    ret = FALSE;
    goto out;
  }

  /* Configure default drc target effect type parameter (only applied for xHE-AAC) */
  GST_OBJECT_LOCK (dec);
  int drc_effect_type = self->drc_effect_type;
  GST_OBJECT_UNLOCK (dec);
  err =
      mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_EFFECT_TYPE,
      drc_effect_type);
  if (err != MPEGH_DEC_OK) {
    GST_ERROR_OBJECT (self, "Failed to set drc effect type %d with error: %d",
        drc_effect_type, err);
    ret = FALSE;
    goto out;
  }

  /* Configure default drc attenuation factor */
  /* Note: FDK API needs the attenuation factor mapped to an int [0...127] */
  GST_OBJECT_LOCK (dec);
  gint attenuation = self->drc_attenuation_factor * 127;
  GST_OBJECT_UNLOCK (dec);
  err =
      mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_ATTENUATION_FACTOR,
      attenuation);
  if (err != MPEGH_DEC_OK) {
    GST_ERROR_OBJECT (self,
        "Failed to set drc attenuation factor %d with error: %d", attenuation,
        err);
    ret = FALSE;
    goto out;
  }

  /* Configure default drc boost factor */
  /* Note: FDK API needs the boost factor mapped to an int [0...127] */
  GST_OBJECT_LOCK (dec);
  gint boost = self->drc_boost_factor * 127;
  GST_OBJECT_UNLOCK (dec);
  err = mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_BOOST_FACTOR, boost);
  if (err != MPEGH_DEC_OK) {
    GST_ERROR_OBJECT (self, "Failed to set drc boost factor %d with error: %d",
        boost, err);
    ret = FALSE;
    goto out;
  }

  /* Configure default album mode */
  GST_OBJECT_LOCK (dec);
  gint album_mode = self->album_mode ? 1 : 0;
  GST_OBJECT_UNLOCK (dec);
  err =
      mpeghdecoder_setParam (self->dec, MPEGH_DEC_PARAM_ALBUM_MODE, album_mode);
  if (err != MPEGH_DEC_OK) {
    GST_ERROR_OBJECT (self, "Failed to set drc album mode %d with error: %d",
        album_mode, err);
    ret = FALSE;
    goto out;
  }

out:
  return ret;
}

static gboolean
gst_mpeghdec_map_channels (GstMpeghDec * self, int channels)
{
  GST_OBJECT_LOCK (self);
  int target_layout = self->target_layout;
  GST_OBJECT_UNLOCK (self);
  if (channel_layouts[target_layout - 1].channels == 0
      || channels != channel_layouts[target_layout - 1].channels) {
    return FALSE;
  }

  memset (self->positions, 0, sizeof (self->positions));
  memcpy (self->positions, channel_layouts[target_layout - 1].positions,
      channels * sizeof (GstAudioChannelPosition));
  return TRUE;
}

static gboolean
gst_mpeghdec_update_info (GstMpeghDec * self, int channels, int samplerate)
{
  if (!gst_mpeghdec_map_channels (self, channels)) {
    GST_ERROR_OBJECT (self, "Failed to get channel positions");
    return FALSE;
  }

  if (self->channels != channels || self->samplerate != samplerate
      || memcmp (self->mapped_positions, self->positions,
          sizeof (self->positions)) != 0) {
    self->channels = channels;
    self->samplerate = samplerate;

    memcpy (self->mapped_positions, self->positions, sizeof (self->positions));
    if (!gst_audio_channel_positions_to_valid_order (self->mapped_positions,
            self->channels)) {
      GST_ERROR_OBJECT (self, "Failed to reorder channels");
      return FALSE;
    }

    gst_audio_info_set_format (&self->info, GST_AUDIO_FORMAT_S32,
        self->samplerate, self->channels, self->mapped_positions);

    if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (self),
            &self->info)) {
      GST_ERROR_OBJECT (self, "Failed to set output format");
      return FALSE;
    }

    self->need_reorder = memcmp (self->mapped_positions, self->positions,
        sizeof (self->positions)) != 0;
  }
  return TRUE;
}

static GstFlowReturn
gst_mpeghdec_handle_frame (GstAudioDecoder * dec, GstBuffer * inbuf)
{
  GstMpeghDec *self = GST_MPEGHDEC (dec);
  GST_DEBUG_OBJECT (self, "handle_frame");
  GstMapInfo imap;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *outbuf;
  GstMapInfo omap;
  MPEGH_DECODER_ERROR err;
  MPEGH_DECODER_OUTPUT_INFO out_info;

  if (inbuf) {
    gst_buffer_map (inbuf, &imap, GST_MAP_READ);

    /* feed decoder with data */
    GST_DEBUG_OBJECT (self, "inbuf pts %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_PTS (inbuf)));
    err =
        mpeghdecoder_process (self->dec, imap.data, imap.size,
        GST_BUFFER_PTS (inbuf));
    gst_buffer_unmap (inbuf, &imap);
    if (err != MPEGH_DEC_OK) {
      GST_ERROR_OBJECT (self, "mpeghdecoder_process failed with %d", err);
      goto out;
    }
  } else {
    GST_DEBUG_OBJECT (self, "input buffer is NULL; assuming EOS!");
    err = mpeghdecoder_flushAndGet (self->dec);
    if (err != MPEGH_DEC_OK) {
      GST_ERROR_OBJECT (self, "mpeghdecoder_flushAndGet failed with %d", err);
      goto out;
    }
  }

  while (err == MPEGH_DEC_OK) {
    int out_samples_per_channel;
    int out_channels;
    int out_samplerate;

    outbuf =
        gst_audio_decoder_allocate_output_buffer (dec,
        MAX_OUTBUF_SIZE * sizeof (gint32));
    gst_buffer_map (outbuf, &omap, GST_MAP_WRITE);

    err =
        mpeghdecoder_getSamples (self->dec, (gint32 *) omap.data,
        MAX_OUTBUF_SIZE, &out_info);
    gst_buffer_unmap (outbuf, &omap);
    if (err != MPEGH_DEC_OK && err != MPEGH_DEC_FEED_DATA) {
      GST_ERROR_OBJECT (self, "mpeghdecoder_getSamples failed with %d", err);
      goto out;
    } else {
      out_samples_per_channel = out_info.numSamplesPerChannel;
      out_samplerate = out_info.sampleRate;
      out_channels = out_info.numChannels;
      if (err == MPEGH_DEC_FEED_DATA) {
        continue;
      }
    }

    gst_buffer_resize (outbuf, 0,
        out_samples_per_channel * out_channels * sizeof (gint32));

    if (!gst_mpeghdec_update_info (self, out_channels, out_samplerate)) {
      ret = GST_FLOW_NOT_NEGOTIATED;
      goto out;
    }

    if (self->need_reorder) {
      gst_audio_buffer_reorder_channels (outbuf,
          GST_AUDIO_INFO_FORMAT (&self->info),
          GST_AUDIO_INFO_CHANNELS (&self->info),
          self->positions, self->mapped_positions);
    }

    GST_DEBUG_OBJECT (self, "gst_buffer_get_size = %lu",
        gst_buffer_get_size (outbuf));
    GST_DEBUG_OBJECT (self, "output buffer = %" GST_PTR_FORMAT,
        (void *) outbuf);

    ret = gst_audio_decoder_finish_frame (dec, outbuf, 1);
  }

out:
  return ret;
}

static void
gst_mpeghdec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstMpeghDec *self = GST_MPEGHDEC (dec);
  GST_DEBUG_OBJECT (self, "flush");
  if (self->dec) {
    MPEGH_DECODER_ERROR err;
    err = mpeghdecoder_flush (self->dec);
    if (err != MPEGH_DEC_OK) {
      GST_ERROR_OBJECT (self, "flushing error: %d", err);
    }
  }
}

static void
gst_mpeghdec_init (GstMpeghDec * self)
{
  GST_DEBUG_OBJECT (self, "init");
  self->dec = NULL;
  self->target_layout = PROP_DEFAULT_MPEGH_TARGET_LAYOUT;
  self->target_reference_level = PROP_DEFAULT_MPEGH_TARGET_REFERENCE_LEVEL;
  self->drc_effect_type = PROP_DEFAULT_MPEGH_DRC_EFFECT_TYPE;
  self->drc_attenuation_factor = PROP_DEFAULT_MPEGH_DRC_ATTENUATION_FACTOR;
  self->drc_boost_factor = PROP_DEFAULT_MPEGH_DRC_BOOST_FACTOR;
  self->album_mode = PROP_DEFAULT_MPEGH_ALBUM_MODE;

  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
}

static void
gst_mpeghdec_class_init (GstMpeghDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *base_class = GST_AUDIO_DECODER_CLASS (klass);
  GObjectClass *gobject_class = (GObjectClass *) klass;

  base_class->start = GST_DEBUG_FUNCPTR (gst_mpeghdec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_mpeghdec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_mpeghdec_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_mpeghdec_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_mpeghdec_flush);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_mpeghdec_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_mpeghdec_get_property);

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class, "MPEG-H audio decoder",
      "Codec/Decoder/Audio", "MPEG-H audio decoder",
      "<mpeg-h-techsupport@iis.fraunhofer.de>");

  g_object_class_install_property (gobject_class, PROP_MPEGH_TARGET_LAYOUT,
      g_param_spec_int ("target-layout", "Target Layout",
          "Target Layout (can only be set at initialization)", 1, 20,
          PROP_DEFAULT_MPEGH_TARGET_LAYOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MPEGH_TARGET_REFERENCE_LEVEL, g_param_spec_float ("target-ref-level",
          "Target Reference Level", "Desired Target Reference Level", -31.75,
          -10.0, PROP_DEFAULT_MPEGH_TARGET_REFERENCE_LEVEL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MPEGH_DRC_EFFECT_TYPE,
      g_param_spec_enum ("drc-effect-type", "MPEG-D DRC Effect Type",
          "Desired MPEG-D DRC Effect Type", GST_MPEGH_EFFECT_TYPE,
          PROP_DEFAULT_MPEGH_DRC_EFFECT_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_MPEGH_DRC_ATTENUATION_FACTOR, g_param_spec_float ("drc-cut-level",
          "DRC Attenuation Factor",
          "Attenuation scaling factor applied to attenuation DRC gains", 0.0,
          1.0, PROP_DEFAULT_MPEGH_DRC_ATTENUATION_FACTOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MPEGH_DRC_BOOST_FACTOR,
      g_param_spec_float ("drc-boost-level", "DRC Boost Factor",
          "Boost scaling factor applied to amplification DRC gains", 0.0, 1.0,
          PROP_DEFAULT_MPEGH_DRC_BOOST_FACTOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MPEGH_ALBUM_MODE,
      g_param_spec_boolean ("album-mode", "Album Mode",
          "Enable/Disable album mode", PROP_DEFAULT_MPEGH_ALBUM_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* Register new types */
  gst_type_mark_as_plugin_api (GST_MPEGH_EFFECT_TYPE, 0);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpeghdec_debug, "mpeghdec", 0, "MPEG-H Decoder");
  return gst_element_register (plugin, "mpeghdec", GST_RANK_PRIMARY,
      GST_TYPE_MPEGHDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, mpeghdec,
    "MPEG-H Decoder", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
