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

#define SINK_CAPS \
    "audio/x-raw-int, "                \
    "endianness = (int) BYTE_ORDER, "  \
    "signed = (boolean) true, "        \
    "width = (int) 16, "               \
    "depth = (int) 16, "               \
    "rate = (int) [ 8000, 96000 ], "   \
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
    "rate = (int) [ 8000, 96000 ], "   \
    "stream-format = (string) { adts, raw }, " \
    "base-profile = (string) { main, lc, ssr, ltp }; " \
    "audio/mpeg, "                     \
    "mpegversion = (int) 2, "   \
    "channels = (int) [ 1, 6 ], "      \
    "rate = (int) [ 8000, 96000 ], "   \
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
  PROP_BITRATE,
  PROP_PROFILE,
  PROP_TNS,
  PROP_MIDSIDE,
  PROP_SHORTCTL
};

static void gst_faac_base_init (GstFaacClass * klass);
static void gst_faac_class_init (GstFaacClass * klass);
static void gst_faac_init (GstFaac * faac);
static void gst_faac_finalize (GObject * object);
static void gst_faac_reset (GstFaac * faac);

static void gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_faac_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_faac_configure_source_pad (GstFaac * faac);
static gboolean gst_faac_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_faac_sink_getcaps (GstPad * pad);
static GstFlowReturn gst_faac_push_buffers (GstFaac * faac, gboolean force);
static GstFlowReturn gst_faac_chain (GstPad * pad, GstBuffer * data);
static GstStateChangeReturn gst_faac_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (faac_debug);
#define GST_CAT_DEFAULT faac_debug

#define FAAC_DEFAULT_OUTPUTFORMAT 0     /* RAW */
#define FAAC_DEFAULT_BITRATE      128 * 1000
#define FAAC_DEFAULT_TNS          FALSE
#define FAAC_DEFAULT_MIDSIDE      TRUE
#define FAAC_DEFAULT_SHORTCTL     SHORTCTL_NORMAL

GType
gst_faac_get_type (void)
{
  static GType gst_faac_type = 0;

  if (!gst_faac_type) {
    static const GTypeInfo gst_faac_info = {
      sizeof (GstFaacClass),
      (GBaseInitFunc) gst_faac_base_init,
      NULL,
      (GClassInitFunc) gst_faac_class_init,
      NULL,
      NULL,
      sizeof (GstFaac),
      0,
      (GInstanceInitFunc) gst_faac_init,
    };
    const GInterfaceInfo preset_interface_info = {
      NULL,                     /* interface_init */
      NULL,                     /* interface_finalize */
      NULL                      /* interface_data */
    };

    gst_faac_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFaac", &gst_faac_info, 0);

    g_type_add_interface_static (gst_faac_type, GST_TYPE_PRESET,
        &preset_interface_info);
  }

  return gst_faac_type;
}

static void
gst_faac_base_init (GstFaacClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class, "AAC audio encoder",
      "Codec/Encoder/Audio",
      "Free MPEG-2/4 AAC encoder",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  GST_DEBUG_CATEGORY_INIT (faac_debug, "faac", 0, "AAC encoding");
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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_faac_set_property;
  gobject_class->get_property = gst_faac_get_property;
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_faac_finalize);

  /* properties */
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (bps)", "Bitrate in bits/sec",
          8 * 1000, 320 * 1000, FAAC_DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TNS,
      g_param_spec_boolean ("tns", "TNS", "Use temporal noise shaping",
          FAAC_DEFAULT_TNS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIDSIDE,
      g_param_spec_boolean ("midside", "Midside", "Allow mid/side encoding",
          FAAC_DEFAULT_MIDSIDE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHORTCTL,
      g_param_spec_enum ("shortctl", "Block type",
          "Block type encorcing",
          GST_TYPE_FAAC_SHORTCTL, FAAC_DEFAULT_SHORTCTL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* virtual functions */
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_faac_change_state);
}

static void
gst_faac_init (GstFaac * faac)
{
  faac->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_chain));
  gst_pad_set_setcaps_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_sink_setcaps));
  gst_pad_set_getcaps_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_sink_getcaps));
  gst_pad_set_event_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_sink_event));
  gst_element_add_pad (GST_ELEMENT (faac), faac->sinkpad);

  faac->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (faac->srcpad);
  gst_element_add_pad (GST_ELEMENT (faac), faac->srcpad);

  faac->adapter = gst_adapter_new ();

  faac->profile = LOW;
  faac->mpegversion = 4;

  /* default properties */
  faac->bitrate = FAAC_DEFAULT_BITRATE;
  faac->shortctl = FAAC_DEFAULT_SHORTCTL;
  faac->outputformat = FAAC_DEFAULT_OUTPUTFORMAT;
  faac->tns = FAAC_DEFAULT_TNS;
  faac->midside = FAAC_DEFAULT_MIDSIDE;

  gst_faac_reset (faac);
}

static void
gst_faac_reset (GstFaac * faac)
{
  faac->handle = NULL;
  faac->samplerate = -1;
  faac->channels = -1;
  faac->offset = 0;
  gst_adapter_clear (faac->adapter);
}

static void
gst_faac_finalize (GObject * object)
{
  GstFaac *faac = (GstFaac *) object;

  g_object_unref (faac->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_faac_close_encoder (GstFaac * faac)
{
  if (faac->handle)
    faacEncClose (faac->handle);
  faac->handle = NULL;
  gst_adapter_clear (faac->adapter);
  faac->offset = 0;
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
gst_faac_sink_getcaps (GstPad * pad)
{
  static volatile gsize sinkcaps = 0;

  if (g_once_init_enter (&sinkcaps)) {
    GstCaps *tmp = gst_caps_new_empty ();
    GstStructure *s, *t;
    gint i, c;

    s = gst_structure_new ("audio/x-raw-int",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "width", G_TYPE_INT, 16,
        "depth", G_TYPE_INT, 16, "rate", GST_TYPE_INT_RANGE, 8000, 96000, NULL);

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

    GST_DEBUG_OBJECT (pad, "Generated sinkcaps: %" GST_PTR_FORMAT, tmp);

    g_once_init_leave (&sinkcaps, (gsize) tmp);
  }

  return gst_caps_ref ((GstCaps *) sinkcaps);
}

/* check downstream caps to configure format */
static void
gst_faac_negotiate (GstFaac * faac)
{
  GstCaps *caps;

  caps = gst_pad_get_allowed_caps (faac->srcpad);

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
gst_faac_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFaac *faac = GST_FAAC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  faacEncHandle *handle;
  gint channels, samplerate, width;
  gulong samples, bytes, fmt = 0, bps = 0;
  gboolean result = FALSE;

  if (!gst_caps_is_fixed (caps))
    goto refuse_caps;

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &samplerate)) {
    goto refuse_caps;
  }

  if (gst_structure_has_name (structure, "audio/x-raw-int")) {
    gst_structure_get_int (structure, "width", &width);
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
  } else if (gst_structure_has_name (structure, "audio/x-raw-float")) {
    fmt = FAAC_INPUT_FLOAT;
    bps = 4;
  }

  if (!fmt)
    goto refuse_caps;

  /* If the encoder is initialized, do not
     reinitialize it again if not necessary */
  if (faac->handle) {
    if (samplerate == faac->samplerate && channels == faac->channels &&
        fmt == faac->format)
      return TRUE;

    /* clear out pending frames */
    gst_faac_push_buffers (faac, TRUE);

    gst_faac_close_encoder (faac);
  }

  if (!(handle = faacEncOpen (samplerate, channels, &samples, &bytes)))
    goto setup_failed;

  /* ok, record and set up */
  faac->format = fmt;
  faac->bps = bps;
  faac->handle = handle;
  faac->bytes = bytes;
  faac->samples = samples;
  faac->channels = channels;
  faac->samplerate = samplerate;

  gst_faac_negotiate (faac);

  /* finish up */
  result = gst_faac_configure_source_pad (faac);

done:
  gst_object_unref (faac);
  return result;

  /* ERRORS */
setup_failed:
  {
    GST_ELEMENT_ERROR (faac, LIBRARY, SETTINGS, (NULL), (NULL));
    goto done;
  }
refuse_caps:
  {
    GST_WARNING_OBJECT (faac, "refused caps %" GST_PTR_FORMAT, caps);
    goto done;
  }
}

static gboolean
gst_faac_configure_source_pad (GstFaac * faac)
{
  GstCaps *srccaps;
  gboolean ret = FALSE;
  faacEncConfiguration *conf;
  guint maxbitrate;

  /* we negotiated caps update current configuration */
  conf = faacEncGetCurrentConfiguration (faac->handle);
  conf->mpegVersion = (faac->mpegversion == 4) ? MPEG4 : MPEG2;
  conf->aacObjectType = faac->profile;
  conf->allowMidside = faac->midside;
  conf->useLfe = 0;
  conf->useTns = faac->tns;
  conf->bitRate = faac->bitrate / faac->channels;
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

  ret = gst_pad_set_caps (faac->srcpad, srccaps);
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
gst_faac_push_buffers (GstFaac * faac, gboolean force)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint av, frame_size, size, ret_size;
  GstBuffer *outbuf;
  guint64 timestamp, distance;
  const guint8 *data;

  /* samples already considers channel count */
  frame_size = faac->samples * faac->bps;

  while (G_LIKELY (ret == GST_FLOW_OK)) {

    av = gst_adapter_available (faac->adapter);

    GST_LOG_OBJECT (faac, "pushing: force: %d, frame_size: %d, av: %d, "
        "offset: %d", force, frame_size, av, faac->offset);

    /* idea:
     * - start of adapter corresponds with what has already been encoded
     * (i.e. really returned by faac)
     * - start + offset is what needs to be fed to faac next
     * That way we can timestamp the output based
     * on adapter provided timestamp (and duration is a fixed frame duration) */

    /* not enough data for one frame and no flush forcing */
    if (!force && (av < frame_size + faac->offset))
      break;

    if (G_LIKELY (av - faac->offset >= frame_size)) {
      GST_LOG_OBJECT (faac, "encoding a frame");
      data = gst_adapter_peek (faac->adapter, faac->offset + frame_size);
      data += faac->offset;
      size = frame_size;
    } else if (av - faac->offset > 0) {
      GST_LOG_OBJECT (faac, "encoding leftover");
      data = gst_adapter_peek (faac->adapter, av);
      data += faac->offset;
      size = av - faac->offset;
    } else {
      GST_LOG_OBJECT (faac, "emptying encoder");
      data = NULL;
      size = 0;
    }

    outbuf = gst_buffer_new_and_alloc (faac->bytes);

    if (G_UNLIKELY ((ret_size = faacEncEncode (faac->handle, (gint32 *) data,
                    size / faac->bps, GST_BUFFER_DATA (outbuf),
                    faac->bytes)) < 0)) {
      gst_buffer_unref (outbuf);
      goto encode_failed;
    }

    GST_LOG_OBJECT (faac, "encoder return: %d", ret_size);

    /* consumed, advanced view */
    faac->offset += size;
    g_assert (faac->offset <= av);

    if (G_UNLIKELY (!ret_size)) {
      gst_buffer_unref (outbuf);
      if (size)
        continue;
      else
        break;
    }

    /* deal with encoder lead-out */
    if (G_UNLIKELY (av == 0 && faac->offset == 0)) {
      GST_DEBUG_OBJECT (faac, "encoder returned additional data");
      /* continuous with previous output, ok to have 0 duration */
      timestamp = faac->next_ts;
    } else {
      /* after some caching, finally some data */
      /* adapter gives time */
      timestamp = gst_adapter_prev_timestamp (faac->adapter, &distance);
    }

    if (G_LIKELY ((av = gst_adapter_available (faac->adapter)) >= frame_size)) {
      /* must have then come from a complete frame */
      gst_adapter_flush (faac->adapter, frame_size);
      faac->offset -= frame_size;
      size = frame_size;
    } else {
      /* otherwise leftover */
      gst_adapter_clear (faac->adapter);
      faac->offset = 0;
      size = av;
    }

    GST_BUFFER_SIZE (outbuf) = ret_size;
    if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (timestamp)))
      GST_BUFFER_TIMESTAMP (outbuf) = timestamp +
          GST_FRAMES_TO_CLOCK_TIME (distance / faac->channels / faac->bps,
          faac->samplerate);
    GST_BUFFER_DURATION (outbuf) =
        GST_FRAMES_TO_CLOCK_TIME (size / faac->channels / faac->bps,
        faac->samplerate);
    faac->next_ts =
        GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf);

    /* perhaps check/set DISCONT based on timestamps ? */

    GST_LOG_OBJECT (faac, "Pushing out buffer time: %" GST_TIME_FORMAT
        " duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (faac->srcpad));
    ret = gst_pad_push (faac->srcpad, outbuf);
  }

  /* in case encoder returns less than expected, clear our view as well */
  if (G_UNLIKELY (force)) {
#ifndef GST_DISABLE_GST_DEBUG
    if ((av = gst_adapter_available (faac->adapter)))
      GST_WARNING_OBJECT (faac, "encoder left %d bytes; discarding", av);
#endif
    gst_adapter_clear (faac->adapter);
    faac->offset = 0;
  }

  return ret;

  /* ERRORS */
encode_failed:
  {
    GST_ELEMENT_ERROR (faac, LIBRARY, ENCODE, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_faac_sink_event (GstPad * pad, GstEvent * event)
{
  GstFaac *faac;
  gboolean ret;

  faac = GST_FAAC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (faac, "received %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      if (faac->handle) {
        /* flush first */
        GST_DEBUG_OBJECT (faac, "Pushing out remaining buffers because of EOS");
        gst_faac_push_buffers (faac, TRUE);
      }

      ret = gst_pad_event_default (pad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;

  }
  gst_object_unref (faac);
  return ret;
}

static GstFlowReturn
gst_faac_chain (GstPad * pad, GstBuffer * inbuf)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstFaac *faac;

  faac = GST_FAAC (gst_pad_get_parent (pad));

  if (!faac->handle)
    goto no_handle;

  GST_LOG_OBJECT (faac, "Got buffer time: %" GST_TIME_FORMAT " duration: %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)));

  gst_adapter_push (faac->adapter, inbuf);

  result = gst_faac_push_buffers (faac, FALSE);

done:
  gst_object_unref (faac);

  return result;

  /* ERRORS */
no_handle:
  {
    GST_ELEMENT_ERROR (faac, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    gst_buffer_unref (inbuf);
    result = GST_FLOW_ERROR;
    goto done;
  }
}

static void
gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFaac *faac = GST_FAAC (object);

  GST_OBJECT_LOCK (faac);

  switch (prop_id) {
    case PROP_BITRATE:
      faac->bitrate = g_value_get_int (value);
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
    case PROP_BITRATE:
      g_value_set_int (value, faac->bitrate);
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

static GstStateChangeReturn
gst_faac_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFaac *faac = GST_FAAC (element);

  /* upwards state changes */
  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* downwards state changes */
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      gst_faac_close_encoder (faac);
      gst_faac_reset (faac);
      break;
    }
    default:
      break;
  }

  return ret;
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
