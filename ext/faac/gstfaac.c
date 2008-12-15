/* GStreamer FAAC (Free AAC Encoder) plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <string.h>

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
    "mpegversion = (int) { 4, 2 }, "   \
    "channels = (int) [ 1, 6 ], "      \
    "rate = (int) [ 8000, 96000 ]"
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS));

static const GstElementDetails gst_faac_details =
GST_ELEMENT_DETAILS ("AAC audio encoder",
    "Codec/Encoder/Audio",
    "Free MPEG-2/4 AAC encoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

enum
{
  ARG_0,
  ARG_OUTPUTFORMAT,
  ARG_BITRATE,
  ARG_PROFILE,
  ARG_TNS,
  ARG_MIDSIDE,
  ARG_SHORTCTL
};

static void gst_faac_base_init (GstFaacClass * klass);
static void gst_faac_class_init (GstFaacClass * klass);
static void gst_faac_init (GstFaac * faac);

static void gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_faac_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_faac_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_faac_chain (GstPad * pad, GstBuffer * data);
static GstStateChangeReturn gst_faac_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GST_DEBUG_CATEGORY_STATIC (faac_debug);
#define GST_CAT_DEFAULT faac_debug

#define FAAC_DEFAULT_MPEGVERSION 4

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

    gst_faac_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFaac", &gst_faac_info, 0);
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

  gst_element_class_set_details (element_class, &gst_faac_details);

  GST_DEBUG_CATEGORY_INIT (faac_debug, "faac", 0, "AAC encoding");
}

#define GST_TYPE_FAAC_PROFILE (gst_faac_profile_get_type ())
static GType
gst_faac_profile_get_type (void)
{
  static GType gst_faac_profile_type = 0;

  if (!gst_faac_profile_type) {
    static GEnumValue gst_faac_profile[] = {
      {MAIN, "MAIN", "Main profile"},
      {LOW, "LC", "Low complexity profile"},
      {SSR, "SSR", "Scalable sampling rate profile"},
      {LTP, "LTP", "Long term prediction profile"},
      {0, NULL, NULL},
    };

    gst_faac_profile_type = g_enum_register_static ("GstFaacProfile",
        gst_faac_profile);
  }

  return gst_faac_profile_type;
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

#define GST_TYPE_FAAC_OUTPUTFORMAT (gst_faac_outputformat_get_type ())
static GType
gst_faac_outputformat_get_type (void)
{
  static GType gst_faac_outputformat_type = 0;

  if (!gst_faac_outputformat_type) {
    static GEnumValue gst_faac_outputformat[] = {
      {0, "OUTPUTFORMAT_RAW", "Raw AAC"},
      {1, "OUTPUTFORMAT_ADTS", "ADTS headers"},
      {0, NULL, NULL},
    };

    gst_faac_outputformat_type = g_enum_register_static ("GstFaacOutputFormat",
        gst_faac_outputformat);
  }

  return gst_faac_outputformat_type;
}

static void
gst_faac_class_init (GstFaacClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_faac_set_property;
  gobject_class->get_property = gst_faac_get_property;

  /* properties */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (bps)", "Bitrate in bits/sec",
          8 * 1000, 320 * 1000, 128 * 1000, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PROFILE,
      g_param_spec_enum ("profile", "Profile", "MPEG/AAC encoding profile",
          GST_TYPE_FAAC_PROFILE, MAIN, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_TNS,
      g_param_spec_boolean ("tns", "TNS", "Use temporal noise shaping",
          FALSE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MIDSIDE,
      g_param_spec_boolean ("midside", "Midside", "Allow mid/side encoding",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SHORTCTL,
      g_param_spec_enum ("shortctl", "Block type",
          "Block type encorcing",
          GST_TYPE_FAAC_SHORTCTL, MAIN, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_OUTPUTFORMAT,
      g_param_spec_enum ("outputformat", "Output format",
          "Format of output frames",
          GST_TYPE_FAAC_OUTPUTFORMAT, 0 /* RAW */ , G_PARAM_READWRITE));

  /* virtual functions */
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_faac_change_state);
}

static void
gst_faac_init (GstFaac * faac)
{
  faac->handle = NULL;
  faac->samplerate = -1;
  faac->channels = -1;
  faac->cache = NULL;
  faac->cache_time = GST_CLOCK_TIME_NONE;
  faac->cache_duration = 0;
  faac->next_ts = GST_CLOCK_TIME_NONE;

  faac->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_chain));
  gst_pad_set_setcaps_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_sink_setcaps));
  gst_pad_set_event_function (faac->sinkpad,
      GST_DEBUG_FUNCPTR (gst_faac_sink_event));
  gst_element_add_pad (GST_ELEMENT (faac), faac->sinkpad);

  faac->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_use_fixed_caps (faac->srcpad);
  gst_element_add_pad (GST_ELEMENT (faac), faac->srcpad);

  /* default properties */
  faac->bitrate = 1000 * 128;
  faac->profile = MAIN;
  faac->shortctl = SHORTCTL_NORMAL;
  faac->outputformat = 0;       /* RAW */
  faac->tns = FALSE;
  faac->midside = TRUE;
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
    goto done;

  GST_OBJECT_LOCK (faac);
  if (faac->handle) {
    faacEncClose (faac->handle);
    faac->handle = NULL;
  }
  if (faac->cache) {
    gst_buffer_unref (faac->cache);
    faac->cache = NULL;
  }
  GST_OBJECT_UNLOCK (faac);

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &samplerate)) {
    goto done;
  }

  if (!(handle = faacEncOpen (samplerate, channels, &samples, &bytes)))
    goto done;

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

  if (!fmt) {
    faacEncClose (handle);
    goto done;
  }

  GST_OBJECT_LOCK (faac);
  faac->format = fmt;
  faac->bps = bps;
  faac->handle = handle;
  faac->bytes = bytes;
  faac->samples = samples;
  faac->channels = channels;
  faac->samplerate = samplerate;
  GST_OBJECT_UNLOCK (faac);

  result = TRUE;

done:
  gst_object_unref (faac);
  return result;
}

static gboolean
gst_faac_configure_source_pad (GstFaac * faac)
{
  GstCaps *allowed_caps;
  GstCaps *srccaps;
  gboolean ret = FALSE;
  gint n, ver, mpegversion;
  faacEncConfiguration *conf;
  guint maxbitrate;

  mpegversion = FAAC_DEFAULT_MPEGVERSION;

  allowed_caps = gst_pad_get_allowed_caps (faac->srcpad);
  GST_DEBUG_OBJECT (faac, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

  if (allowed_caps == NULL)
    return FALSE;

  if (gst_caps_is_empty (allowed_caps))
    goto empty_caps;

  if (!gst_caps_is_any (allowed_caps)) {
    for (n = 0; n < gst_caps_get_size (allowed_caps); n++) {
      GstStructure *s = gst_caps_get_structure (allowed_caps, n);

      if (gst_structure_get_int (s, "mpegversion", &ver) &&
          (ver == 4 || ver == 2)) {
        mpegversion = ver;
        break;
      }
    }
  }
  gst_caps_unref (allowed_caps);

  /* we negotiated caps update current configuration */
  conf = faacEncGetCurrentConfiguration (faac->handle);
  conf->mpegVersion = (mpegversion == 4) ? MPEG4 : MPEG2;
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
        ("bitrate %u exceeds maximum allowed bitrate of %u for samplerate %d. "
            "Setting bitrate to %u", conf->bitRate, maxbitrate,
            faac->samplerate, maxbitrate));
    conf->bitRate = maxbitrate;
  }

  if (!faacEncSetConfiguration (faac->handle, conf))
    goto set_failed;

  /* now create a caps for it all */
  srccaps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, mpegversion,
      "channels", G_TYPE_INT, faac->channels,
      "rate", G_TYPE_INT, faac->samplerate, NULL);

  if (mpegversion == 4) {
    GstBuffer *codec_data;
    guint8 *config = NULL;
    gulong config_len = 0;

    /* get the config string */
    GST_DEBUG_OBJECT (faac, "retrieving decoder info");
    faacEncGetDecoderSpecificInfo (faac->handle, &config, &config_len);

    /* copy it into a buffer */
    codec_data = gst_buffer_new_and_alloc (config_len);
    memcpy (GST_BUFFER_DATA (codec_data), config, config_len);

    /* add to caps */
    gst_caps_set_simple (srccaps,
        "codec_data", GST_TYPE_BUFFER, codec_data, NULL);

    gst_buffer_unref (codec_data);
  }

  GST_DEBUG_OBJECT (faac, "src pad caps: %" GST_PTR_FORMAT, srccaps);

  ret = gst_pad_set_caps (faac->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return ret;

  /* ERROR */
empty_caps:
  {
    gst_caps_unref (allowed_caps);
    return FALSE;
  }
set_failed:
  {
    GST_WARNING_OBJECT (faac, "Faac doesn't support the current configuration");
    return FALSE;
  }
}

static gboolean
gst_faac_sink_event (GstPad * pad, GstEvent * event)
{
  GstFaac *faac;
  gboolean ret;

  faac = GST_FAAC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      GstBuffer *outbuf;

      if (!faac->handle)
        ret = FALSE;
      else
        ret = TRUE;

      /* flush first */
      GST_DEBUG ("Pushing out remaining buffers because of EOS");
      while (ret) {
        if (gst_pad_alloc_buffer_and_set_caps (faac->srcpad,
                GST_BUFFER_OFFSET_NONE, faac->bytes,
                GST_PAD_CAPS (faac->srcpad), &outbuf) == GST_FLOW_OK) {
          gint ret_size;

          GST_DEBUG ("next_ts %" GST_TIME_FORMAT,
              GST_TIME_ARGS (faac->next_ts));

          if ((ret_size = faacEncEncode (faac->handle, NULL, 0,
                      GST_BUFFER_DATA (outbuf), faac->bytes)) > 0) {
            GST_BUFFER_SIZE (outbuf) = ret_size;
            GST_BUFFER_TIMESTAMP (outbuf) = faac->next_ts;
            /* faac seems to always consume a fixed number of input samples,
             * therefore extrapolate the duration from that value and the incoming
             * bitrate */
            GST_BUFFER_DURATION (outbuf) = gst_util_uint64_scale (faac->samples,
                GST_SECOND, faac->channels * faac->samplerate);
            if (GST_CLOCK_TIME_IS_VALID (faac->next_ts))
              faac->next_ts += GST_BUFFER_DURATION (outbuf);
            gst_pad_push (faac->srcpad, outbuf);
          } else {
            gst_buffer_unref (outbuf);
            ret = FALSE;
          }
        } else
          ret = FALSE;
      }
      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_NEWSEGMENT:
      ret = gst_pad_push_event (faac->srcpad, event);
      break;
    case GST_EVENT_TAG:
      ret = gst_pad_event_default (pad, event);
      break;
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
  GstBuffer *outbuf, *subbuf;
  GstFaac *faac;
  guint size, ret_size, in_size, frame_size;

  faac = GST_FAAC (gst_pad_get_parent (pad));

  if (!faac->handle)
    goto no_handle;

  if (!GST_PAD_CAPS (faac->srcpad)) {
    if (!gst_faac_configure_source_pad (faac))
      goto nego_failed;
  }

  GST_DEBUG ("Got buffer time:%" GST_TIME_FORMAT " duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)));

  size = GST_BUFFER_SIZE (inbuf);
  in_size = size;
  if (faac->cache)
    in_size += GST_BUFFER_SIZE (faac->cache);
  frame_size = faac->samples * faac->bps;

  while (1) {
    /* do we have enough data for one frame? */
    if (in_size / faac->bps < faac->samples) {
      if (in_size > size) {
        GstBuffer *merge;

        /* this is panic! we got a buffer, but still don't have enough
         * data. Merge them and retry in the next cycle... */
        merge = gst_buffer_merge (faac->cache, inbuf);
        gst_buffer_unref (faac->cache);
        gst_buffer_unref (inbuf);
        faac->cache = merge;
      } else if (in_size == size) {
        /* this shouldn't happen, but still... */
        faac->cache = inbuf;
      } else if (in_size > 0) {
        faac->cache = gst_buffer_create_sub (inbuf, size - in_size, in_size);
        GST_BUFFER_DURATION (faac->cache) =
            GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (faac->cache) / size;
        GST_BUFFER_TIMESTAMP (faac->cache) =
            GST_BUFFER_TIMESTAMP (inbuf) + (GST_BUFFER_DURATION (inbuf) *
            (size - in_size) / size);
        gst_buffer_unref (inbuf);
      } else {
        gst_buffer_unref (inbuf);
      }

      goto done;
    }

    /* create the frame */
    if (in_size > size) {
      GstBuffer *merge;

      /* merge */
      subbuf = gst_buffer_create_sub (inbuf, 0, frame_size - (in_size - size));
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      merge = gst_buffer_merge (faac->cache, subbuf);
      gst_buffer_unref (faac->cache);
      gst_buffer_unref (subbuf);
      subbuf = merge;
      faac->cache = NULL;
    } else {
      subbuf = gst_buffer_create_sub (inbuf, size - in_size, frame_size);
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      GST_BUFFER_TIMESTAMP (subbuf) =
          GST_BUFFER_TIMESTAMP (inbuf) + (GST_BUFFER_DURATION (inbuf) *
          (size - in_size) / size);
    }

    result =
        gst_pad_alloc_buffer_and_set_caps (faac->srcpad, GST_BUFFER_OFFSET_NONE,
        faac->bytes, GST_PAD_CAPS (faac->srcpad), &outbuf);
    if (result != GST_FLOW_OK)
      goto done;

    if ((ret_size = faacEncEncode (faac->handle,
                (gint32 *) GST_BUFFER_DATA (subbuf),
                GST_BUFFER_SIZE (subbuf) / faac->bps,
                GST_BUFFER_DATA (outbuf), faac->bytes)) < 0) {
      GST_ELEMENT_ERROR (faac, LIBRARY, ENCODE, (NULL), (NULL));
      gst_buffer_unref (inbuf);
      gst_buffer_unref (subbuf);
      result = GST_FLOW_ERROR;
      goto done;
    }

    if (ret_size > 0) {
      GST_BUFFER_SIZE (outbuf) = ret_size;
      if (faac->cache_time != GST_CLOCK_TIME_NONE) {
        GST_BUFFER_TIMESTAMP (outbuf) = faac->cache_time;
        faac->cache_time = GST_CLOCK_TIME_NONE;
      } else
        GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (subbuf);
      GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (subbuf);
      if (faac->cache_duration) {
        GST_BUFFER_DURATION (outbuf) += faac->cache_duration;
        faac->cache_duration = 0;
      }
      /* Store the value of the next expected timestamp to output
       * This is required in order to output the trailing encoded packets
       * at EOS with proper timestamps and duration. */
      faac->next_ts =
          GST_BUFFER_TIMESTAMP (outbuf) + GST_BUFFER_DURATION (outbuf);
      GST_DEBUG ("Pushing out buffer time:%" GST_TIME_FORMAT " duration:%"
          GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));
      result = gst_pad_push (faac->srcpad, outbuf);
    } else {
      /* FIXME: what I'm doing here isn't fully correct, but there
       * really isn't a better way yet.
       * Problem is that libfaac caches buffers (for encoding
       * purposes), so the timestamp of the outgoing buffer isn't
       * the same as the timestamp of the data that I pushed in.
       * However, I don't know the delay between those two so I
       * cannot really say aything about it. This is a bad guess. */

      gst_buffer_unref (outbuf);
      if (faac->cache_time != GST_CLOCK_TIME_NONE)
        faac->cache_time = GST_BUFFER_TIMESTAMP (subbuf);
      faac->cache_duration += GST_BUFFER_DURATION (subbuf);
    }

    in_size -= frame_size;
    gst_buffer_unref (subbuf);
  }

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
nego_failed:
  {
    GST_ELEMENT_ERROR (faac, CORE, NEGOTIATION, (NULL),
        ("failed to negotiate MPEG/AAC format with next element"));
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
    case ARG_BITRATE:
      faac->bitrate = g_value_get_int (value);
      break;
    case ARG_PROFILE:
      faac->profile = g_value_get_enum (value);
      break;
    case ARG_TNS:
      faac->tns = g_value_get_boolean (value);
      break;
    case ARG_MIDSIDE:
      faac->midside = g_value_get_boolean (value);
      break;
    case ARG_SHORTCTL:
      faac->shortctl = g_value_get_enum (value);
      break;
    case ARG_OUTPUTFORMAT:
      faac->outputformat = g_value_get_enum (value);
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
    case ARG_BITRATE:
      g_value_set_int (value, faac->bitrate);
      break;
    case ARG_PROFILE:
      g_value_set_enum (value, faac->profile);
      break;
    case ARG_TNS:
      g_value_set_boolean (value, faac->tns);
      break;
    case ARG_MIDSIDE:
      g_value_set_boolean (value, faac->midside);
      break;
    case ARG_SHORTCTL:
      g_value_set_enum (value, faac->shortctl);
      break;
    case ARG_OUTPUTFORMAT:
      g_value_set_enum (value, faac->outputformat);
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
      GST_OBJECT_LOCK (faac);
      if (faac->handle) {
        faacEncClose (faac->handle);
        faac->handle = NULL;
      }
      if (faac->cache) {
        gst_buffer_unref (faac->cache);
        faac->cache = NULL;
      }
      faac->cache_time = GST_CLOCK_TIME_NONE;
      faac->cache_duration = 0;
      faac->samplerate = -1;
      faac->channels = -1;
      faac->next_ts = GST_CLOCK_TIME_NONE;
      GST_OBJECT_UNLOCK (faac);
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
  return gst_element_register (plugin, "faac", GST_RANK_NONE, GST_TYPE_FAAC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faac",
    "Free AAC Encoder (FAAC)",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
