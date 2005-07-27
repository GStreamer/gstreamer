/* GStreamer FAAD (Free AAC Decoder) plugin
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

#include <gst/audio/multichannel.h>

#include "gstfaad.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) { 2, 4 }")
    );

#define STATIC_INT_CAPS(bpp) \
  "audio/x-raw-int, " \
    "endianness = (int) BYTE_ORDER, " \
    "signed = (bool) TRUE, " \
    "width = (int) " G_STRINGIFY (bpp) ", " \
    "depth = (int) " G_STRINGIFY (bpp) ", " \
    "rate = (int) [ 8000, 96000 ], " \
    "channels = (int) [ 1, 8 ]"

#define STATIC_FLOAT_CAPS(bpp) \
  "audio/x-raw-float, " \
    "endianness = (int) BYTE_ORDER, " \
    "depth = (int) " G_STRINGIFY (bpp) ", " \
    "rate = (int) [ 8000, 96000 ], " \
    "channels = (int) [ 1, 8 ]"

/*
 * All except 16-bit integer are disabled until someone fixes FAAD.
 * FAAD allocates approximately 8*1024*2 bytes bytes, which is enough
 * for 1 frame (1024 samples) of 6 channel (5.1) 16-bit integer 16bpp
 * audio, but not for any other. You'll get random segfaults, crashes
 * and even valgrind goes crazy.
 */

#define STATIC_CAPS \
  STATIC_INT_CAPS (16)
#if 0
#define NOTUSED "; " \
STATIC_INT_CAPS (24) \
    "; " \
STATIC_INT_CAPS (32) \
    "; " \
STATIC_FLOAT_CAPS (32) \
    "; " \
STATIC_FLOAT_CAPS (64)
#endif
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (STATIC_CAPS)
    );

static void gst_faad_base_init (GstFaadClass * klass);
static void gst_faad_class_init (GstFaadClass * klass);
static void gst_faad_init (GstFaad * faad);

/*
static GstPadLinkReturn
gst_faad_sinkconnect (GstPad * pad, const GstCaps * caps);
static GstPadLinkReturn
gst_faad_srcconnect (GstPad * pad, const GstCaps * caps);*/
static gboolean gst_faad_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_faad_srcgetcaps (GstPad * pad);
static gboolean gst_faad_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_faad_chain (GstPad * pad, GstBuffer * buffer);
static GstElementStateReturn gst_faad_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/* static guint gst_faad_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_faad_get_type (void)
{
  static GType gst_faad_type = 0;

  if (!gst_faad_type) {
    static const GTypeInfo gst_faad_info = {
      sizeof (GstFaadClass),
      (GBaseInitFunc) gst_faad_base_init,
      NULL,
      (GClassInitFunc) gst_faad_class_init,
      NULL,
      NULL,
      sizeof (GstFaad),
      0,
      (GInstanceInitFunc) gst_faad_init,
    };

    gst_faad_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstFaad", &gst_faad_info, 0);
  }

  return gst_faad_type;
}

static void
gst_faad_base_init (GstFaadClass * klass)
{
  static GstElementDetails gst_faad_details =
      GST_ELEMENT_DETAILS ("Free AAC Decoder (FAAD)",
      "Codec/Decoder/Audio",
      "Free MPEG-2/4 AAC decoder",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_faad_details);
}

static void
gst_faad_class_init (GstFaadClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_faad_change_state;
}

static void
gst_faad_init (GstFaad * faad)
{
  faad->handle = NULL;
  faad->samplerate = -1;
  faad->channels = -1;
  faad->tempbuf = NULL;
  faad->need_channel_setup = TRUE;
  faad->channel_positions = NULL;
  faad->init = FALSE;

  /* GST_FLAG_SET (faad, GST_ELEMENT_EVENT_AWARE); */

  faad->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (faad), faad->sinkpad);
  gst_pad_set_event_function (faad->sinkpad, gst_faad_event);
  gst_pad_set_setcaps_function (faad->sinkpad, gst_faad_setcaps);
  gst_pad_set_chain_function (faad->sinkpad, gst_faad_chain);
  /*gst_pad_set_link_function (faad->sinkpad, gst_faad_sinkconnect); */

  faad->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (faad), faad->srcpad);
  gst_pad_use_fixed_caps (faad->srcpad);
  /*gst_pad_set_link_function (faad->srcpad, gst_faad_srcconnect); */
  gst_pad_set_getcaps_function (faad->srcpad, gst_faad_srcgetcaps);
}

static gboolean
gst_faad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  GstFaad *faad;
  GstCaps *copy;

  faad = GST_FAAD (GST_PAD_PARENT (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* get channel count */
  gst_structure_get_int (structure, "channels", &faad->channels);
  gst_structure_get_int (structure, "rate", &faad->samplerate);

  /* create reverse caps */
  copy = gst_caps_new_simple ("audio/x-raw-float",
      "channels", G_TYPE_INT, faad->channels,
      "depth", G_TYPE_INT, G_STRINGIFY (bpp),
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "rate", G_TYPE_INT, faad->samplerate);

  gst_pad_set_caps (faad->srcpad, copy);
  gst_caps_unref (copy);

  return TRUE;
}


/*
 * Channel identifier conversion - caller g_free()s result!
 */
/*
static guchar *
gst_faad_chanpos_from_gst (GstAudioChannelPosition * pos, guint num)
{
  guchar *fpos = g_new (guchar, num);
  guint n;

  for (n = 0; n < num; n++) {
    switch (pos[n]) {
      case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
        fpos[n] = FRONT_CHANNEL_LEFT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
        fpos[n] = FRONT_CHANNEL_RIGHT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_MONO:
        fpos[n] = FRONT_CHANNEL_CENTER;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
        fpos[n] = SIDE_CHANNEL_LEFT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
        fpos[n] = SIDE_CHANNEL_RIGHT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
        fpos[n] = BACK_CHANNEL_LEFT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
        fpos[n] = BACK_CHANNEL_RIGHT;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
        fpos[n] = BACK_CHANNEL_CENTER;
        break;
      case GST_AUDIO_CHANNEL_POSITION_LFE:
        fpos[n] = LFE_CHANNEL;
        break;
      default:
        GST_WARNING ("Unsupported GST channel position 0x%x encountered",
            pos[n]);
        g_free (fpos);
        return NULL;
    }
  }

  return fpos;
}
*/
static GstAudioChannelPosition *
gst_faad_chanpos_to_gst (guchar * fpos, guint num)
{
  GstAudioChannelPosition *pos = g_new (GstAudioChannelPosition, num);
  guint n;

  for (n = 0; n < num; n++) {
    switch (fpos[n]) {
      case FRONT_CHANNEL_LEFT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
        break;
      case FRONT_CHANNEL_RIGHT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
        break;
      case FRONT_CHANNEL_CENTER:
        /* argh, mono = center */
        if (num == 1)
          pos[n] = GST_AUDIO_CHANNEL_POSITION_FRONT_MONO;
        else
          pos[n] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
        break;
      case SIDE_CHANNEL_LEFT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
        break;
      case SIDE_CHANNEL_RIGHT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
        break;
      case BACK_CHANNEL_LEFT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
        break;
      case BACK_CHANNEL_RIGHT:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
        break;
      case BACK_CHANNEL_CENTER:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
        break;
      case LFE_CHANNEL:
        pos[n] = GST_AUDIO_CHANNEL_POSITION_LFE;
        break;
      default:
        GST_WARNING ("Unsupported FAAD channel position 0x%x encountered",
            fpos[n]);
        g_free (pos);
        return NULL;
    }
  }

  return pos;
}

/*
static GstPadLinkReturn
gst_faad_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *value;
  GstBuffer *buf;

  // Assume raw stream 
  faad->packetised = FALSE;

  if ((value = gst_structure_get_value (str, "codec_data"))) {
    gulong samplerate;
    guchar channels;

    // We have codec data, means packetised stream 
    faad->packetised = TRUE;
    buf = g_value_get_boxed (value);

    // someone forgot that char can be unsigned when writing the API 
    if ((gint8) faacDecInit2 (faad->handle, GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf), &samplerate, &channels) < 0)
      return GST_PAD_LINK_REFUSED;

    //faad->samplerate = samplerate;
    //faad->channels = channels;
    faad->init = TRUE;

    if (faad->tempbuf) {
      gst_buffer_unref (faad->tempbuf);
      faad->tempbuf = NULL;
    }
  } else {
    faad->init = FALSE;
  }

  faad->need_channel_setup = TRUE;

  // if there's no decoderspecificdata, it's all fine. We cannot know
  // * much more at this point... 
  return GST_PAD_LINK_OK;
}
*/
static GstCaps *
gst_faad_srcgetcaps (GstPad * pad)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  static GstAudioChannelPosition *supported_positions = NULL;
  static gint num_supported_positions = LFE_CHANNEL - FRONT_CHANNEL_CENTER + 1;
  GstCaps *templ;

  if (!supported_positions) {
    guchar *supported_fpos = g_new0 (guchar, num_supported_positions);
    gint n;

    for (n = 0; n < num_supported_positions; n++) {
      supported_fpos[n] = n + FRONT_CHANNEL_CENTER;
    }
    supported_positions = gst_faad_chanpos_to_gst (supported_fpos,
        num_supported_positions);
    g_free (supported_fpos);
  }

  if (faad->handle != NULL && faad->channels != -1 && faad->samplerate != -1) {
    GstCaps *caps = gst_caps_new_empty ();
    GstStructure *str;
    gint fmt[] = {
      FAAD_FMT_16BIT,
#if 0
      FAAD_FMT_24BIT,
      FAAD_FMT_32BIT,
      FAAD_FMT_FLOAT,
      FAAD_FMT_DOUBLE,
#endif
      -1
    }
    , n;

    for (n = 0; fmt[n] != -1; n++) {
      switch (fmt[n]) {
        case FAAD_FMT_16BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
          break;
#if 0
        case FAAD_FMT_24BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 24, "depth", G_TYPE_INT, 24, NULL);
          break;
        case FAAD_FMT_32BIT:
          str = gst_structure_new ("audio/x-raw-int",
              "signed", G_TYPE_BOOLEAN, TRUE,
              "width", G_TYPE_INT, 32, "depth", G_TYPE_INT, 32, NULL);
          break;
        case FAAD_FMT_FLOAT:
          str = gst_structure_new ("audio/x-raw-float",
              "depth", G_TYPE_INT, 32, NULL);
          break;
        case FAAD_FMT_DOUBLE:
          str = gst_structure_new ("audio/x-raw-float",
              "depth", G_TYPE_INT, 64, NULL);
          break;
#endif
        default:
          str = NULL;
          break;
      }
      if (!str)
        continue;

      if (faad->samplerate != -1) {
        gst_structure_set (str, "rate", G_TYPE_INT, faad->samplerate, NULL);
      } else {
        gst_structure_set (str, "rate", GST_TYPE_INT_RANGE, 8000, 96000, NULL);
      }

      if (faad->channels != -1) {
        gst_structure_set (str, "channels", G_TYPE_INT, faad->channels, NULL);

        // put channel information here */
        if (faad->channel_positions) {
          GstAudioChannelPosition *pos;

          pos = gst_faad_chanpos_to_gst (faad->channel_positions,
              faad->channels);
          if (!pos) {
            gst_structure_free (str);
            continue;
          }
          gst_audio_set_channel_positions (str, pos);
          g_free (pos);
        } else {
          gst_audio_set_structure_channel_positions_list (str,
              supported_positions, num_supported_positions);
        }
      } else {
        gst_structure_set (str, "channels", GST_TYPE_INT_RANGE, 1, 8, NULL);
        /* we set channel positions later */
      }

      gst_structure_set (str, "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);

      gst_caps_append_structure (caps, str);
    }

    if (faad->channels == -1) {
      gst_audio_set_caps_channel_positions_list (caps,
          supported_positions, num_supported_positions);
    }

    return caps;
  }

  /* template with channel positions */
  templ = gst_caps_copy (GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad)));
  gst_audio_set_caps_channel_positions_list (templ,
      supported_positions, num_supported_positions);

  return templ;
}

/**
static GstPadLinkReturn
gst_faad_srcconnect (GstPad * pad, const GstCaps * caps)
{
  GstStructure *structure;
  const gchar *mimetype;
  gint fmt = -1;
  gint depth, rate, channels;
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!faad->handle || (faad->samplerate == -1 || faad->channels == -1) ||
      !faad->channel_positions) {
    return GST_PAD_LINK_DELAYED;
  }

  mimetype = gst_structure_get_name (structure);

  // Samplerate and channels are normally provided through
  // * the getcaps function 
  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate) ||
      rate != faad->samplerate || channels != faad->channels) {
    return GST_PAD_LINK_REFUSED;
  }

  // Another internal checkup. 
  if (faad->need_channel_setup) {
    GstAudioChannelPosition *pos;
    guchar *fpos;
    guint i;

    pos = gst_audio_get_channel_positions (structure);
    if (!pos) {
      return GST_PAD_LINK_DELAYED;
    }
    fpos = gst_faad_chanpos_from_gst (pos, faad->channels);
    g_free (pos);
    if (!fpos)
      return GST_PAD_LINK_REFUSED;

    for (i = 0; i < faad->channels; i++) {
      if (fpos[i] != faad->channel_positions[i]) {
        g_free (fpos);
        return GST_PAD_LINK_REFUSED;
      }
    }
    g_free (fpos);
  }

  if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint width;

    if (!gst_structure_get_int (structure, "depth", &depth) ||
        !gst_structure_get_int (structure, "width", &width))
      return GST_PAD_LINK_REFUSED;
    if (depth != width)
      return GST_PAD_LINK_REFUSED;

    switch (depth) {
      case 16:
        fmt = FAAD_FMT_16BIT;
        break;
#if 0
      case 24:
        fmt = FAAD_FMT_24BIT;
        break;
      case 32:
        fmt = FAAD_FMT_32BIT;
        break;
#endif
    }
  } else {
    if (!gst_structure_get_int (structure, "depth", &depth))
      return GST_PAD_LINK_REFUSED;

    switch (depth) {
#if 0
      case 32:
        fmt = FAAD_FMT_FLOAT;
        break;
      case 64:
        fmt = FAAD_FMT_DOUBLE;
        break;
#endif
    }
  }

  if (fmt != -1) {
    faacDecConfiguration *conf;

    conf = faacDecGetCurrentConfiguration (faad->handle);
    conf->outputFormat = fmt;
    if (faacDecSetConfiguration (faad->handle, conf) == 0)
      return GST_PAD_LINK_REFUSED;

    // FIXME: handle return value, how? 
    faad->bps = depth / 8;

    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
}*/

/*
 * Data reading.
 */
static gboolean
gst_faad_event (GstPad * pad, GstEvent * event)
{
  GstFaad *faad;
  gboolean res;

  faad = GST_FAAD (gst_pad_get_parent (pad));

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    case GST_EVENT_NEWSEGMENT:
    default:
      break;
  }

  res = gst_pad_event_default (faad->sinkpad, event);

  return res;
}

static GstFlowReturn
gst_faad_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint input_size;
  guint skip_bytes = 0;
  guchar *input_data;
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstBuffer *outbuf;
  faacDecFrameInfo *info;
  guint64 next_ts;
  void *out;
  gboolean run_loop = TRUE;

/*
  if (GST_IS_EVENT (data)) {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
        if (faad->tempbuf != NULL) {
          gst_buffer_unref (faad->tempbuf);
          faad->tempbuf = NULL;
        }
        gst_element_set_eos (GST_ELEMENT (faad));
        gst_pad_push (faad->srcpad, data);
        return;
      default:
        gst_pad_event_default (pad, event);
        return;
    }
  }
*/
  info = g_new0 (faacDecFrameInfo, 1);

  /* buffer + remaining data */
  /* buf = GST_BUFFER (data); */
  next_ts = GST_BUFFER_TIMESTAMP (buffer);
  if (faad->tempbuf) {
    buffer = gst_buffer_join (faad->tempbuf, buffer);
    faad->tempbuf = NULL;
  }

  /* init if not already done during capsnego */
  if (!faad->init) {
    gulong samplerate;
    guchar channels;
    glong init_res;

    init_res = faacDecInit (faad->handle,
        GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), &samplerate,
        &channels);
    if (init_res < 0) {
      GST_ELEMENT_ERROR (faad, STREAM, DECODE, (NULL),
          ("Failed to init decoder from stream"));
      return GST_FLOW_UNEXPECTED;
    }
    skip_bytes = init_res;
    faad->init = TRUE;

    /* store for renegotiation later on */
    /* FIXME: that's moot, info will get zeroed in DecDecode() */
    info->samplerate = samplerate;
    info->channels = channels;
  } else {
    info->samplerate = 0;
    info->channels = 0;
  }

  /* decode cycle */
  input_data = GST_BUFFER_DATA (buffer);
  input_size = GST_BUFFER_SIZE (buffer);
  info->bytesconsumed = input_size - skip_bytes;

  if (!faad->packetised) {
    /* We must check that ourselves for raw stream */
    run_loop = (input_size >= FAAD_MIN_STREAMSIZE);
  }

  while ((input_size > 0) && run_loop) {

    if (faad->packetised) {
      /* Only one packet per buffer, no matter how much is really consumed */
      run_loop = FALSE;
    } else {
      if (input_size < FAAD_MIN_STREAMSIZE || info->bytesconsumed <= 0) {
        break;
      }
    }

    out = faacDecDecode (faad->handle, info, input_data + skip_bytes,
        input_size - skip_bytes);
    if (info->error) {
      GST_ELEMENT_ERROR (faad, STREAM, DECODE, (NULL),
          ("Failed to decode buffer: %s",
              faacDecGetErrorMessage (info->error)));
      break;
    }

    if (info->bytesconsumed > input_size)
      info->bytesconsumed = input_size;
    input_size -= info->bytesconsumed;
    input_data += info->bytesconsumed;

    if (out && info->samples > 0) {
      gboolean fmt_change = FALSE;

      /* see if we need to renegotiate */
      if (info->samplerate != faad->samplerate ||
          info->channels != faad->channels || !faad->channel_positions) {
        fmt_change = TRUE;
      } else {
        gint i;

        for (i = 0; i < info->channels; i++) {
          if (info->channel_position[i] != faad->channel_positions[i])
            fmt_change = TRUE;
        }
      }

      if (fmt_change) {
        /*GstPadLinkReturn ret; */

        /* store new negotiation information */
        faad->samplerate = info->samplerate;
        faad->channels = info->channels;
        if (faad->channel_positions)
          g_free (faad->channel_positions);
        faad->channel_positions = g_new (guint8, faad->channels);
        memcpy (faad->channel_positions, info->channel_position,
            faad->channels);

        /* and negotiate 
           ret = gst_pad_renegotiate (faad->srcpad);
           if (GST_PAD_LINK_FAILED (ret)) {
           GST_ELEMENT_ERROR (faad, CORE, NEGOTIATION, (NULL), (NULL));
           break;
           } */
      }

      /* play decoded data */
      if (info->samples > 0) {
        outbuf = gst_buffer_new_and_alloc (info->samples * faad->bps);
        /* ugh */
        memcpy (GST_BUFFER_DATA (outbuf), out, GST_BUFFER_SIZE (outbuf));
        GST_BUFFER_TIMESTAMP (outbuf) = next_ts;
        GST_BUFFER_DURATION (outbuf) =
            (guint64) GST_SECOND *info->samples / faad->samplerate;
        if (GST_CLOCK_TIME_IS_VALID (next_ts)) {
          next_ts += GST_BUFFER_DURATION (outbuf);
        }
        gst_pad_push (faad->srcpad, outbuf);
      }
    }
  }

  /* Keep the leftovers in raw stream */
  if (input_size > 0 && !faad->packetised) {
    if (input_size < GST_BUFFER_SIZE (buffer)) {
      faad->tempbuf = gst_buffer_create_sub (buffer,
          GST_BUFFER_SIZE (buffer) - input_size, input_size);
    } else {
      faad->tempbuf = buffer;
      gst_buffer_ref (buffer);
    }
  }

  gst_buffer_unref (buffer);

  g_free (info);

  return ret;
}

static GstElementStateReturn
gst_faad_change_state (GstElement * element)
{
  GstFaad *faad = GST_FAAD (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!(faad->handle = faacDecOpen ()))
        return GST_STATE_FAILURE;
      else {
        faacDecConfiguration *conf;

        conf = faacDecGetCurrentConfiguration (faad->handle);
        conf->defObjectType = LC;
        //conf->dontUpSampleImplicitSBR = 1;
        faacDecSetConfiguration (faad->handle, conf);
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      faad->samplerate = -1;
      faad->channels = -1;
      faad->need_channel_setup = TRUE;
      faad->init = FALSE;
      g_free (faad->channel_positions);
      faad->channel_positions = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
      faacDecClose (faad->handle);
      faad->handle = NULL;
      if (faad->tempbuf) {
        gst_buffer_unref (faad->tempbuf);
        faad->tempbuf = NULL;
      }
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
  return gst_element_register (plugin, "faad", GST_RANK_PRIMARY, GST_TYPE_FAAD);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faad",
    "Free AAC Decoder (FAAD)",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
