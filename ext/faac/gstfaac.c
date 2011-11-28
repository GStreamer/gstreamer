/* GStreamer FAAC (Free AAC Encoder) plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2009 Mark Nauwelaerts <mnauw@users.sourceforge.net>
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
 * SECTION:element-faac
 * @see_also: faad
 *
 * faac encodes raw audio to AAC (MPEG-4 part 3) streams.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc wave=sine num-buffers=100 ! audioconvert ! faac ! matroskamux ! filesink location=sine.mkv
 * ]| Encode a sine beep as aac and write to matroska container.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include <gst/audio/multichannel.h>
#include <gst/pbutils/codec-utils.h>

#include "gstfaac.h"

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000, " \
                    "64000, " \
                    "88200, " \
                    "96000"
#define SINK_CAPS \
    "audio/x-raw-int, "                \
    "endianness = (int) BYTE_ORDER, "  \
    "signed = (boolean) true, "        \
    "width = (int) 16, "               \
    "depth = (int) 16, "               \
    "rate = (int) {" SAMPLE_RATES "}, "   \
    "channels = (int) [ 1, 6 ] "

/* these don't seem to work? */
#if 0
"audio/x-raw-int, "
    "endianness = (int) BYTE_ORDER, "
    "signed = (boolean) true, "
    "width = (int) 32, "
    "depth = (int) { 24, 32 }, "
    "rate = (int) [ 8000, 96000], "
    "channels = (int) [ 1, 6]; "
    "audio/x-raw-float, "
    "endianness = (int) BYTE_ORDER, "
    "width = (int) 32, "
    "rate = (int) [ 8000, 96000], " "channels = (int) [ 1, 6]"
#endif
#define SRC_CAPS \
    "audio/mpeg, "                     \
    "mpegversion = (int) 4, "   \
    "channels = (int) [ 1, 6 ], "      \
    "rate = (int) {" SAMPLE_RATES "}, "   \
    "stream-format = (string) { adts, raw }, " \
    "base-profile = (string) { main, lc, ssr, ltp }; " \
    "audio/mpeg, "                     \
    "mpegversion = (int) 2, "   \
    "channels = (int) [ 1, 6 ], "      \
    "rate = (int) {" SAMPLE_RATES "}, "   \
    "stream-format = (string) { adts, raw }, " \
    "profile = (string) { main, lc }"
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS));

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_BITRATE,
  PROP_RATE_CONTROL,
  PROP_PROFILE,
  PROP_TNS,
  PROP_MIDSIDE,
  PROP_SHORTCTL
};

enum
{
  VBR = 1,
  ABR
};

static void gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_faac_configure_source_pad (GstFaac * faac);
static GstCaps *gst_faac_getcaps (GstAudioEncoder * enc);

static gboolean gst_faac_start (GstAudioEncoder * enc);
static gboolean gst_faac_stop (GstAudioEncoder * enc);
static gboolean gst_faac_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_faac_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);

GST_DEBUG_CATEGORY_STATIC (faac_debug);
#define GST_CAT_DEFAULT faac_debug

#define FAAC_DEFAULT_QUALITY      100
#define FAAC_DEFAULT_BITRATE      128 * 1000
#define FAAC_DEFAULT_RATE_CONTROL VBR
#define FAAC_DEFAULT_TNS          FALSE
#define FAAC_DEFAULT_MIDSIDE      TRUE
#define FAAC_DEFAULT_SHORTCTL     SHORTCTL_NORMAL

GST_BOILERPLATE (GstFaac, gst_faac, GstAudioEncoder, GST_TYPE_AUDIO_ENCODER);

static void
gst_faac_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template);

  gst_element_class_set_details_simple (element_class, "AAC audio encoder",
      "Codec/Encoder/Audio",
      "Free MPEG-2/4 AAC encoder",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  GST_DEBUG_CATEGORY_INIT (faac_debug, "faac", 0, "AAC encoding");
}

#define GST_TYPE_FAAC_RATE_CONTROL (gst_faac_brtype_get_type ())
static GType
gst_faac_brtype_get_type (void)
{
  static GType gst_faac_brtype_type = 0;

  if (!gst_faac_brtype_type) {
    static GEnumValue gst_faac_brtype[] = {
      {VBR, "VBR", "VBR encoding"},
      {ABR, "ABR", "ABR encoding"},
      {0, NULL, NULL},
    };

    gst_faac_brtype_type = g_enum_register_static ("GstFaacBrtype",
        gst_faac_brtype);
  }

  return gst_faac_brtype_type;
}

#define GST_TYPE_FAAC_SHORTCTL (gst_faac_shortctl_get_type ())
static GType
gst_faac_shortctl_get_type (void)
{
  static GType gst_faac_shortctl_type = 0;

  if (!gst_faac_shortctl_type) {
    static GEnumValue gst_faac_shortctl[] = {
      {SHORTCTL_NORMAL, "SHORTCTL_NORMAL", "Normal block type"},
      {SHORTCTL_NOSHORT, "SHORTCTL_NOSHORT", "No short blocks"},
      {SHORTCTL_NOLONG, "SHORTCTL_NOLONG", "No long blocks"},
      {0, NULL, NULL},
    };

    gst_faac_shortctl_type = g_enum_register_static ("GstFaacShortCtl",
        gst_faac_shortctl);
  }

  return gst_faac_shortctl_type;
}

static void
gst_faac_class_init (GstFaacClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_faac_set_property;
  gobject_class->get_property = gst_faac_get_property;

  base_class->start = GST_DEBUG_FUNCPTR (gst_faac_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_faac_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_faac_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_faac_handle_frame);
  base_class->getcaps = GST_DEBUG_FUNCPTR (gst_faac_getcaps);

  /* properties */
  g_object_class_install_property (gobject_class, PROP_QUALITY,
      g_param_spec_int ("quality", "Quality (%)",
          "Variable bitrate (VBR) quantizer quality in %", 1, 1000,
          FAAC_DEFAULT_QUALITY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (bps)",
          "Average Bitrate (ABR) in bits/sec", 8 * 1000, 320 * 1000,
          FAAC_DEFAULT_BITRATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control", "Rate Control (ABR/VBR)",
          "Encoding bitrate type (VBR/ABR)", GST_TYPE_FAAC_RATE_CONTROL,
          FAAC_DEFAULT_RATE_CONTROL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TNS,
      g_param_spec_boolean ("tns", "TNS", "Use temporal noise shaping",
          FAAC_DEFAULT_TNS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIDSIDE,
      g_param_spec_boolean ("midside", "Midside", "Allow mid/side encoding",
          FAAC_DEFAULT_MIDSIDE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHORTCTL,
      g_param_spec_enum ("shortctl", "Block type",
          "Block type encorcing",
          GST_TYPE_FAAC_SHORTCTL, FAAC_DEFAULT_SHORTCTL,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_faac_init (GstFaac * faac, GstFaacClass * klass)
{
}

static void
gst_faac_close_encoder (GstFaac * faac)
{
  if (faac->handle)
    faacEncClose (faac->handle);
  faac->handle = NULL;
}

static gboolean
gst_faac_start (GstAudioEncoder * enc)
{
  GstFaac *faac = GST_FAAC (enc);

  GST_DEBUG_OBJECT (faac, "start");
  return TRUE;
}

static gboolean
gst_faac_stop (GstAudioEncoder * enc)
{
  GstFaac *faac = GST_FAAC (enc);

  GST_DEBUG_OBJECT (faac, "stop");
  gst_faac_close_encoder (faac);
  return TRUE;
}

static const GstAudioChannelPosition aac_channel_positions[][8] = {
  {GST_AUDIO_CHANNEL_POSITION_FRONT_MONO},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      },
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_LFE}
};

static GstCaps *
gst_faac_getcaps (GstAudioEncoder * enc)
{
  static volatile gsize sinkcaps = 0;

  if (g_once_init_enter (&sinkcaps)) {
    GstCaps *tmp = gst_caps_new_empty ();
    GstStructure *s, *t;
    gint i, c;
    static const int rates[] = {
      8000, 11025, 12000, 16000, 22050, 24000,
      32000, 44100, 48000, 64000, 88200, 96000
    };
    GValue rates_arr = { 0, };
    GValue tmp_v = { 0, };

    g_value_init (&rates_arr, GST_TYPE_LIST);
    g_value_init (&tmp_v, G_TYPE_INT);
    for (i = 0; i < G_N_ELEMENTS (rates); i++) {
      g_value_set_int (&tmp_v, rates[i]);
      gst_value_list_append_value (&rates_arr, &tmp_v);
    }
    g_value_unset (&tmp_v);

    s = gst_structure_new ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
    gst_structure_set_value (s, "rate", &rates_arr);

    for (i = 1; i <= 6; i++) {
      GValue chanpos = { 0 };
      GValue pos = { 0 };

      t = gst_structure_copy (s);

      gst_structure_set (t, "channels", G_TYPE_INT, i, NULL);

      g_value_init (&chanpos, GST_TYPE_ARRAY);
      g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);

      for (c = 0; c < i; c++) {
        g_value_set_enum (&pos, aac_channel_positions[i - 1][c]);
        gst_value_array_append_value (&chanpos, &pos);
      }
      g_value_unset (&pos);

      gst_structure_set_value (t, "channel-positions", &chanpos);
      g_value_unset (&chanpos);
      gst_caps_append_structure (tmp, t);
    }
    gst_structure_free (s);
    g_value_unset (&rates_arr);

    GST_DEBUG_OBJECT (enc, "Generated sinkcaps: %" GST_PTR_FORMAT, tmp);

    g_once_init_leave (&sinkcaps, (gsize) tmp);
  }

  return gst_audio_encoder_proxy_getcaps (enc, (GstCaps *) sinkcaps);
}

static gboolean
gst_faac_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstFaac *faac = GST_FAAC (enc);
  faacEncHandle *handle;
  gint channels, samplerate, width;
  gulong samples, bytes, fmt = 0, bps = 0;
  gboolean result = FALSE;

  /* base class takes care */
  channels = GST_AUDIO_INFO_CHANNELS (info);
  samplerate = GST_AUDIO_INFO_RATE (info);
  width = GST_AUDIO_INFO_WIDTH (info);

  if (GST_AUDIO_INFO_IS_INTEGER (info)) {
    switch (width) {
      case 16:
        fmt = FAAC_INPUT_16BIT;
        bps = 2;
        break;
      case 24:
      case 32:
        fmt = FAAC_INPUT_32BIT;
        bps = 4;
        break;
      default:
        g_return_val_if_reached (FALSE);
    }
  } else {
    fmt = FAAC_INPUT_FLOAT;
    bps = 4;
  }

  /* clean up in case of re-configure */
  gst_faac_close_encoder (faac);

  if (!(handle = faacEncOpen (samplerate, channels, &samples, &bytes)))
    goto setup_failed;

  /* mind channel count */
  samples /= channels;

  /* ok, record and set up */
  faac->format = fmt;
  faac->bps = bps;
  faac->handle = handle;
  faac->bytes = bytes;
  faac->samples = samples;
  faac->channels = channels;
  faac->samplerate = samplerate;

  /* finish up */
  result = gst_faac_configure_source_pad (faac);

  /* report needs to base class */
  gst_audio_encoder_set_frame_samples_min (enc, samples);
  gst_audio_encoder_set_frame_samples_max (enc, samples);
  gst_audio_encoder_set_frame_max (enc, 1);

done:
  return result;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (faac, LIBRARY, SETTINGS, (NULL), (NULL));
    goto done;
  }
}

/* check downstream caps to configure format */
static void
gst_faac_negotiate (GstFaac * faac)
{
  GstCaps *caps;

  /* default setup */
  faac->profile = LOW;
  faac->mpegversion = 4;
  faac->outputformat = 0;

  caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (faac));

  GST_DEBUG_OBJECT (faac, "allowed caps: %" GST_PTR_FORMAT, caps);

  if (caps && gst_caps_get_size (caps) > 0) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const gchar *str = NULL;
    gint i = 4;

    if ((str = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (str, "adts") == 0) {
        GST_DEBUG_OBJECT (faac, "use ADTS format for output");
        faac->outputformat = 1;
      } else if (strcmp (str, "raw") == 0) {
        GST_DEBUG_OBJECT (faac, "use RAW format for output");
        faac->outputformat = 0;
      } else {
        GST_DEBUG_OBJECT (faac, "unknown stream-format: %s", str);
        faac->outputformat = 0;
      }
    }

    if ((str = gst_structure_get_string (s, "profile"))) {
      if (strcmp (str, "main") == 0) {
        faac->profile = MAIN;
      } else if (strcmp (str, "lc") == 0) {
        faac->profile = LOW;
      } else if (strcmp (str, "ssr") == 0) {
        faac->profile = SSR;
      } else if (strcmp (str, "ltp") == 0) {
        faac->profile = LTP;
      } else {
        faac->profile = LOW;
      }
    }

    if (!gst_structure_get_int (s, "mpegversion", &i) || i == 4) {
      faac->mpegversion = 4;
    } else {
      faac->mpegversion = 2;
    }
  }

  if (caps)
    gst_caps_unref (caps);
}

static gboolean
gst_faac_configure_source_pad (GstFaac * faac)
{
  GstCaps *srccaps;
  gboolean ret = FALSE;
  faacEncConfiguration *conf;
  guint maxbitrate;

  /* negotiate stream format */
  gst_faac_negotiate (faac);

  /* we negotiated caps update current configuration */
  conf = faacEncGetCurrentConfiguration (faac->handle);
  conf->mpegVersion = (faac->mpegversion == 4) ? MPEG4 : MPEG2;
  conf->aacObjectType = faac->profile;
  conf->allowMidside = faac->midside;
  conf->useLfe = 0;
  conf->useTns = faac->tns;

  if (faac->brtype == VBR) {
    conf->quantqual = faac->quality;
  } else if (faac->brtype == ABR) {
    conf->bitRate = faac->bitrate / faac->channels;
  }

  conf->inputFormat = faac->format;
  conf->outputFormat = faac->outputformat;
  conf->shortctl = faac->shortctl;

  /* check, warn and correct if the max bitrate for the given samplerate is
   * exceeded. Maximum of 6144 bit for a channel */
  maxbitrate =
      (unsigned int) (6144.0 * (double) faac->samplerate / (double) 1024.0 +
      .5);
  if (conf->bitRate > maxbitrate) {
    GST_ELEMENT_WARNING (faac, RESOURCE, SETTINGS, (NULL),
        ("bitrate %lu exceeds maximum allowed bitrate of %u for samplerate %d. "
            "Setting bitrate to %u", conf->bitRate, maxbitrate,
            faac->samplerate, maxbitrate));
    conf->bitRate = maxbitrate;
  }

  /* default 0 to start with, libfaac chooses based on bitrate */
  conf->bandWidth = 0;

  if (!faacEncSetConfiguration (faac->handle, conf))
    goto set_failed;

  /* let's see what really happened,
   * note that this may not really match desired rate */
  GST_DEBUG_OBJECT (faac, "average bitrate: %lu kbps",
      (conf->bitRate + 500) / 1000 * faac->channels);
  GST_DEBUG_OBJECT (faac, "quantization quality: %ld", conf->quantqual);
  GST_DEBUG_OBJECT (faac, "bandwidth: %d Hz", conf->bandWidth);

  /* now create a caps for it all */
  srccaps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, faac->mpegversion,
      "channels", G_TYPE_INT, faac->channels,
      "rate", G_TYPE_INT, faac->samplerate,
      "stream-format", G_TYPE_STRING, (faac->outputformat ? "adts" : "raw"),
      NULL);

  /* DecoderSpecificInfo is only available for mpegversion=4 */
  if (faac->mpegversion == 4) {
    guint8 *config = NULL;
    gulong config_len = 0;

    /* get the config string */
    GST_DEBUG_OBJECT (faac, "retrieving decoder info");
    faacEncGetDecoderSpecificInfo (faac->handle, &config, &config_len);

    if (!gst_codec_utils_aac_caps_set_level_and_profile (srccaps, config,
            config_len)) {
      free (config);
      gst_caps_unref (srccaps);
      goto invalid_codec_data;
    }

    if (!faac->outputformat) {
      GstBuffer *codec_data;

      /* copy it into a buffer */
      codec_data = gst_buffer_new_and_alloc (config_len);
      memcpy (GST_BUFFER_DATA (codec_data), config, config_len);

      /* add to caps */
      gst_caps_set_simple (srccaps,
          "codec_data", GST_TYPE_BUFFER, codec_data, NULL);

      gst_buffer_unref (codec_data);
    }

    free (config);
  } else {
    const gchar *profile;

    /* Add least add the profile to the caps */
    switch (faac->profile) {
      case MAIN:
        profile = "main";
        break;
      case LTP:
        profile = "ltp";
        break;
      case SSR:
        profile = "ssr";
        break;
      case LOW:
      default:
        profile = "lc";
        break;
    }
    gst_caps_set_simple (srccaps, "profile", G_TYPE_STRING, profile, NULL);
    /* FIXME: How to get the profile for mpegversion==2? */
  }

  GST_DEBUG_OBJECT (faac, "src pad caps: %" GST_PTR_FORMAT, srccaps);

  ret = gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (faac), srccaps);
  gst_caps_unref (srccaps);

  return ret;

  /* ERROR */
set_failed:
  {
    GST_WARNING_OBJECT (faac, "Faac doesn't support the current configuration");
    return FALSE;
  }
invalid_codec_data:
  {
    GST_ERROR_OBJECT (faac, "Invalid codec data");
    return FALSE;
  }
}

static GstFlowReturn
gst_faac_handle_frame (GstAudioEncoder * enc, GstBuffer * in_buf)
{
  GstFaac *faac = GST_FAAC (enc);
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *out_buf;
  gint size, ret_size;
  const guint8 *data;

  out_buf = gst_buffer_new_and_alloc (faac->bytes);

  if (G_LIKELY (in_buf)) {
    data = GST_BUFFER_DATA (in_buf);
    size = GST_BUFFER_SIZE (in_buf);
  } else {
    data = NULL;
    size = 0;
  }

  if (G_UNLIKELY ((ret_size = faacEncEncode (faac->handle, (gint32 *) data,
                  size / faac->bps, GST_BUFFER_DATA (out_buf),
                  GST_BUFFER_SIZE (out_buf))) < 0))
    goto encode_failed;

  GST_LOG_OBJECT (faac, "encoder return: %d", ret_size);
  if (G_LIKELY (ret_size > 0)) {
    GST_BUFFER_SIZE (out_buf) = ret_size;
    ret = gst_audio_encoder_finish_frame (enc, out_buf, faac->samples);
  } else {
    gst_buffer_unref (out_buf);
  }

  return ret;

  /* ERRORS */
encode_failed:
  {
    gst_buffer_unref (out_buf);
    GST_ELEMENT_ERROR (faac, LIBRARY, ENCODE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static void
gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFaac *faac = GST_FAAC (object);

  GST_OBJECT_LOCK (faac);

  switch (prop_id) {
    case PROP_QUALITY:
      faac->quality = g_value_get_int (value);
      break;
    case PROP_BITRATE:
      faac->bitrate = g_value_get_int (value);
      break;
    case PROP_RATE_CONTROL:
      faac->brtype = g_value_get_enum (value);
      break;
    case PROP_TNS:
      faac->tns = g_value_get_boolean (value);
      break;
    case PROP_MIDSIDE:
      faac->midside = g_value_get_boolean (value);
      break;
    case PROP_SHORTCTL:
      faac->shortctl = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (faac);
}

static void
gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFaac *faac = GST_FAAC (object);

  GST_OBJECT_LOCK (faac);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_int (value, faac->quality);
      break;
    case PROP_BITRATE:
      g_value_set_int (value, faac->bitrate);
      break;
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, faac->brtype);
      break;
    case PROP_TNS:
      g_value_set_boolean (value, faac->tns);
      break;
    case PROP_MIDSIDE:
      g_value_set_boolean (value, faac->midside);
      break;
    case PROP_SHORTCTL:
      g_value_set_enum (value, faac->shortctl);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (faac);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "faac", GST_RANK_SECONDARY,
      GST_TYPE_FAAC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faac",
    "Free AAC Encoder (FAAC)",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
