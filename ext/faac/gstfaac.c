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

#include "gstfaac.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) { 4, 2 }, "
        "channels = (int) [ 1, 6 ], " "rate = (int) [ 8000, 96000 ]")
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, " "endianness = (int) BYTE_ORDER, " "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16, " "rate = (int) [ 8000, 96000 ], " "channels = (int) [ 1, 6]; " "audio/x-raw-int, " "endianness = (int) BYTE_ORDER, " "signed = (boolean) TRUE, " "width = (int) 32, " "depth = (int) 24, " "rate = (int) [ 8000, 96000], " "channels = (int) [ 1, 6]; " "audio/x-raw-float, " "endianness = (int) BYTE_ORDER, " "depth = (int) 32, "    /* sizeof (gfloat) */
        "rate = (int) [ 8000, 96000], " "channels = (int) [ 1, 6]")
    );

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_PROFILE,
  ARG_TNS,
  ARG_MIDSIDE,
  ARG_SHORTCTL
      /* FILL ME */
};

static void gst_faac_base_init (GstFaacClass * klass);
static void gst_faac_class_init (GstFaacClass * klass);
static void gst_faac_init (GstFaac * faac);

static void gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_faac_sinkconnect (GstPad * pad, const GstCaps * caps);
static GstPadLinkReturn
gst_faac_srcconnect (GstPad * pad, const GstCaps * caps);
static void gst_faac_chain (GstPad * pad, GstData * data);
static GstElementStateReturn gst_faac_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/* static guint gst_faac_signals[LAST_SIGNAL] = { 0 }; */

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
  GstElementDetails gst_faac_details = {
    "Free AAC Encoder (FAAC)",
    "Codec/Encoder/Audio",
    "Free MPEG-2/4 AAC encoder",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_faac_details);
}

#define GST_TYPE_FAAC_PROFILE (gst_faac_profile_get_type ())
static GType
gst_faac_profile_get_type (void)
{
  static GType gst_faac_profile_type = 0;

  if (!gst_faac_profile_type) {
    static GEnumValue gst_faac_profile[] = {
      {MAIN, "MAIN", "Main profile"},
      {LOW, "LOW", "Low complexity profile"},
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

static void
gst_faac_class_init (GstFaacClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  /* properties */
  g_object_class_install_property (gobject_class, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (bps)", "Bitrate in bits/sec",
          8 * 1024, 320 * 1024, 128 * 1024, G_PARAM_READWRITE));
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

  /* virtual functions */
  gstelement_class->change_state = gst_faac_change_state;

  gobject_class->set_property = gst_faac_set_property;
  gobject_class->get_property = gst_faac_get_property;
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

  GST_FLAG_SET (faac, GST_ELEMENT_EVENT_AWARE);

  faac->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (faac), faac->sinkpad);
  gst_pad_set_chain_function (faac->sinkpad, gst_faac_chain);
  gst_pad_set_link_function (faac->sinkpad, gst_faac_sinkconnect);

  faac->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (faac), faac->srcpad);
  gst_pad_set_link_function (faac->srcpad, gst_faac_srcconnect);

  /* default properties */
  faac->bitrate = 1024 * 128;
  faac->profile = MAIN;
  faac->shortctl = SHORTCTL_NORMAL;
  faac->tns = FALSE;
  faac->midside = TRUE;
}

static GstPadLinkReturn
gst_faac_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstFaac *faac = GST_FAAC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  faacEncHandle *handle;
  gint channels, samplerate, depth;
  gulong samples, bytes, fmt = 0, bps = 0;

  if (!gst_caps_is_fixed (caps))
    return GST_PAD_LINK_DELAYED;

  if (faac->handle) {
    faacEncClose (faac->handle);
    faac->handle = NULL;
  }
  if (faac->cache) {
    gst_buffer_unref (faac->cache);
    faac->cache = NULL;
  }

  gst_structure_get_int (structure, "channels", &channels);
  gst_structure_get_int (structure, "rate", &samplerate);
  gst_structure_get_int (structure, "depth", &depth);

  /* open a new handle to the encoder */
  if (!(handle = faacEncOpen (samplerate, channels, &samples, &bytes)))
    return GST_PAD_LINK_REFUSED;

  switch (depth) {
    case 16:
      fmt = FAAC_INPUT_16BIT;
      bps = 2;
      break;
    case 24:
      fmt = FAAC_INPUT_32BIT;   /* 24-in-32, actually */
      bps = 4;
      break;
    case 32:
      fmt = FAAC_INPUT_FLOAT;   /* see template, this is right */
      bps = 4;
      break;
  }

  if (!fmt) {
    faacEncClose (handle);
    return GST_PAD_LINK_REFUSED;
  }

  faac->format = fmt;
  faac->bps = bps;
  faac->handle = handle;
  faac->bytes = bytes;
  faac->samples = samples;
  faac->channels = channels;
  faac->samplerate = samplerate;

  /* if the other side was already set-up, redo that */
  if (GST_PAD_CAPS (faac->srcpad))
    return gst_faac_srcconnect (faac->srcpad,
        gst_pad_get_allowed_caps (faac->srcpad));

  /* else, that'll be done later */
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_faac_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstFaac *faac = GST_FAAC (gst_pad_get_parent (pad));
  gint n;

  if (!faac->handle || (faac->samplerate == -1 || faac->channels == -1)) {
    return GST_PAD_LINK_DELAYED;
  }

  /* we do samplerate/channels ourselves */
  for (n = 0; n < gst_caps_get_size (caps); n++) {
    GstStructure *structure = gst_caps_get_structure (caps, n);

    gst_structure_remove_field (structure, "rate");
    gst_structure_remove_field (structure, "channels");
  }

  /* go through list */
  caps = gst_caps_normalize (caps);
  for (n = 0; n < gst_caps_get_size (caps); n++) {
    GstStructure *structure = gst_caps_get_structure (caps, n);
    faacEncConfiguration *conf;
    gint mpegversion = 0;
    GstCaps *newcaps;
    GstPadLinkReturn ret;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);

    /* new conf */
    conf = faacEncGetCurrentConfiguration (faac->handle);
    conf->mpegVersion = (mpegversion == 4) ? MPEG4 : MPEG2;
    conf->aacObjectType = faac->profile;
    conf->allowMidside = faac->midside;
    conf->useLfe = 0;
    conf->useTns = faac->tns;
    conf->bitRate = faac->bitrate;
    conf->inputFormat = faac->format;

    /* FIXME: this one here means that we do not support direct
     * "MPEG audio file" output (like mp3). This means we can
     * only mux this into mov/qt (mp4a) or matroska or so. If
     * we want to support direct AAC file output, we need ADTS
     * headers, and we need to find a way in the caps to detect
     * that (that the next element is filesink or any element
     * that does want ADTS headers). */

    conf->outputFormat = 0;     /* raw, no ADTS headers */
    conf->shortctl = faac->shortctl;
    if (!faacEncSetConfiguration (faac->handle, conf)) {
      GST_WARNING ("Faac doesn't support the current conf");
      continue;
    }

    newcaps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, mpegversion,
        "channels", G_TYPE_INT, faac->channels,
        "rate", G_TYPE_INT, faac->samplerate, NULL);
    ret = gst_pad_try_set_caps (faac->srcpad, newcaps);

    switch (ret) {
      case GST_PAD_LINK_OK:
      case GST_PAD_LINK_DONE:
        return GST_PAD_LINK_DONE;
      case GST_PAD_LINK_DELAYED:
        return GST_PAD_LINK_DELAYED;
      default:
        break;
    }
  }

  return GST_PAD_LINK_REFUSED;
}

static void
gst_faac_chain (GstPad * pad, GstData * data)
{
  GstFaac *faac = GST_FAAC (gst_pad_get_parent (pad));
  GstBuffer *inbuf, *outbuf, *subbuf;
  guint size, ret_size, in_size, frame_size;

  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        /* flush first */
        while (1) {
          outbuf = gst_buffer_new_and_alloc (faac->bytes);
          if ((ret_size = faacEncEncode (faac->handle,
                      NULL, 0, GST_BUFFER_DATA (outbuf), faac->bytes)) < 0) {
            GST_ELEMENT_ERROR (faac, LIBRARY, ENCODE, (NULL), (NULL));
            gst_event_unref (event);
            gst_buffer_unref (outbuf);
            return;
          }

          if (ret_size > 0) {
            GST_BUFFER_SIZE (outbuf) = ret_size;
            GST_BUFFER_TIMESTAMP (outbuf) = 0;
            GST_BUFFER_DURATION (outbuf) = 0;
            gst_pad_push (faac->srcpad, GST_DATA (outbuf));
          } else {
            break;
          }
        }

        gst_element_set_eos (GST_ELEMENT (faac));
        gst_pad_push (faac->srcpad, data);
        return;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  }

  inbuf = GST_BUFFER (data);

  if (!faac->handle) {
    GST_ELEMENT_ERROR (faac, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before chain function"));
    gst_buffer_unref (inbuf);
    return;
  }

  if (!GST_PAD_CAPS (faac->srcpad)) {
    if (gst_faac_srcconnect (faac->srcpad,
            gst_pad_get_allowed_caps (faac->srcpad)) <= 0) {
      GST_ELEMENT_ERROR (faac, CORE, NEGOTIATION, (NULL),
          ("failed to negotiate MPEG/AAC format with next element"));
      gst_buffer_unref (inbuf);
      return;
    }
  }

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

      return;
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

    outbuf = gst_buffer_new_and_alloc (faac->bytes);
    if ((ret_size = faacEncEncode (faac->handle,
                (gint32 *) GST_BUFFER_DATA (subbuf),
                GST_BUFFER_SIZE (subbuf) / faac->bps,
                GST_BUFFER_DATA (outbuf), faac->bytes)) < 0) {
      GST_ELEMENT_ERROR (faac, LIBRARY, ENCODE, (NULL), (NULL));
      gst_buffer_unref (inbuf);
      gst_buffer_unref (subbuf);
      return;
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
      gst_pad_push (faac->srcpad, GST_DATA (outbuf));
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
}

static void
gst_faac_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFaac *faac = GST_FAAC (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_faac_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFaac *faac = GST_FAAC (object);

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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_faac_change_state (GstElement * element)
{
  GstFaac *faac = GST_FAAC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
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
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
