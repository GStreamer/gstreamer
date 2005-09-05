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

GST_DEBUG_CATEGORY_STATIC (faad_debug);
#define GST_CAT_DEFAULT faad_debug

static GstElementDetails faad_details = {
  "Free AAC Decoder (FAAD)",
  "Codec/Decoder/Audio",
  "Free MPEG-2/4 AAC decoder",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
};

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

#if 0
#define STATIC_FLOAT_CAPS(bpp) \
  "audio/x-raw-float, " \
    "endianness = (int) BYTE_ORDER, " \
    "depth = (int) " G_STRINGIFY (bpp) ", " \
    "rate = (int) [ 8000, 96000 ], " \
    "channels = (int) [ 1, 8 ]"
#endif

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

static gboolean gst_faad_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_faad_srcgetcaps (GstPad * pad);
static gboolean gst_faad_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_faad_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_faad_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class;   /* NULL */

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
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &faad_details);
}

static void
gst_faad_class_init (GstFaadClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state = gst_faad_change_state;

  GST_DEBUG_CATEGORY_INIT (faad_debug, "faad", 0, "AAC decoding");
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
  faad->next_ts = 0;
  faad->bytes_in = 0;
  faad->sum_dur_out = 0;
  faad->packetised = FALSE;

  faad->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_template),
      "sink");
  gst_element_add_pad (GST_ELEMENT (faad), faad->sinkpad);
  gst_pad_set_event_function (faad->sinkpad, gst_faad_event);
  gst_pad_set_setcaps_function (faad->sinkpad, gst_faad_setcaps);
  gst_pad_set_chain_function (faad->sinkpad, gst_faad_chain);

  faad->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_template),
      "src");
  gst_element_add_pad (GST_ELEMENT (faad), faad->srcpad);
  gst_pad_use_fixed_caps (faad->srcpad);
  gst_pad_set_getcaps_function (faad->srcpad, gst_faad_srcgetcaps);
}

static gboolean
gst_faad_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFaad *faad = GST_FAAD (gst_pad_get_parent (pad));
  GstStructure *str = gst_caps_get_structure (caps, 0);
  GstBuffer *buf;
  const GValue *value;

  /* Assume raw stream */
  faad->packetised = FALSE;

  if ((value = gst_structure_get_value (str, "codec_data"))) {
    gulong samplerate;
    guchar channels;

    /* We have codec data, means packetised stream */
    faad->packetised = TRUE;
    buf = GST_BUFFER (gst_value_get_mini_object (value));

    /* someone forgot that char can be unsigned when writing the API */
    if ((gint8) faacDecInit2 (faad->handle, GST_BUFFER_DATA (buf),
            GST_BUFFER_SIZE (buf), &samplerate, &channels) < 0) {
      GST_DEBUG ("faacDecInit2() failed");
      return FALSE;
    }
#if 0
    faad->samplerate = samplerate;
    faad->channels = channels;
#endif
    /* not updating these here, so they are updated in the
     * chain function, and new caps are created etc. */
    faad->samplerate = 0;
    faad->channels = 0;

    faad->init = TRUE;

    if (faad->tempbuf) {
      gst_buffer_unref (faad->tempbuf);
      faad->tempbuf = NULL;
    }
  } else {
    faad->init = FALSE;
  }

  faad->need_channel_setup = TRUE;

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
  GstFaad *faad = GST_FAAD (GST_OBJECT_PARENT (pad));
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

        /* put channel information here */
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

static gboolean
gst_faad_event (GstPad * pad, GstEvent * event)
{
  GstFaad *faad;
  gboolean res = TRUE;

  faad = GST_FAAD (gst_pad_get_parent (pad));

  GST_LOG ("handling event %d", GST_EVENT_TYPE (event));

  /* FIXME: we should probably handle FLUSH and also
   *  SEEK in the case where we are not in a container
   *  (when our newsegment was in BYTES) */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (faad->tempbuf != NULL) {
        gst_buffer_unref (faad->tempbuf);
        faad->tempbuf = NULL;
      }
      GST_STREAM_LOCK (pad);
      res = gst_pad_push_event (faad->srcpad, event);
      GST_STREAM_UNLOCK (pad);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat fmt;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_newsegment (event, &rate, &fmt, &start, &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG ("Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
      } else if (fmt == GST_FORMAT_BYTES) {
        GstEvent *new_ev;
        guint64 new_start = 0;
        guint64 new_end = GST_CLOCK_TIME_NONE;

        GST_DEBUG ("Got NEWSEGMENT event in GST_FORMAT_BYTES (%"
            G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT ")", start, end);

        if (faad->bytes_in > 0 && faad->sum_dur_out > 0) {
          /* try to convert based on the average bitrate so far */
          new_start = (faad->sum_dur_out * start) / faad->bytes_in;
          if (new_end != (guint64) - 1) {
            new_end = (faad->sum_dur_out * end) / faad->bytes_in;
          }
        } else {
          GST_DEBUG
              ("no average bitrate yet, sending newsegment with start at 0");
        }
        new_ev =
            gst_event_new_newsegment (rate, GST_FORMAT_TIME, new_start, new_end,
            base);
        gst_event_unref (event);
        event = new_ev;
        GST_DEBUG ("Sending new NEWSEGMENT event, time %" GST_TIME_FORMAT
            " - %" GST_TIME_FORMAT, GST_TIME_ARGS (new_start),
            GST_TIME_ARGS (new_end));
      }

      GST_STREAM_LOCK (pad);
      res = gst_pad_push_event (faad->srcpad, event);
      GST_STREAM_UNLOCK (pad);
      break;
    }
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (faad->srcpad, event);
      break;
    default:
      GST_STREAM_LOCK (pad);
      res = gst_pad_push_event (faad->srcpad, event);
      GST_STREAM_UNLOCK (pad);
      break;
  }

/*  res = gst_pad_event_default (faad->sinkpad, event); */

  return res;
}

static gboolean
gst_faad_update_caps (GstFaad * faad, faacDecFrameInfo * info,
    GstCaps ** p_caps)
{
  GstAudioChannelPosition *pos;
  GstCaps *caps;

  /* store new negotiation information */
  faad->samplerate = info->samplerate;
  faad->channels = info->channels;
  g_free (faad->channel_positions);
  faad->channel_positions = g_memdup (info->channel_position, faad->channels);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "rate", G_TYPE_INT, faad->samplerate,
      "channels", G_TYPE_INT, faad->channels, NULL);

  faad->bps = 16 / 8;

  pos = gst_faad_chanpos_to_gst (faad->channel_positions, faad->channels);
  gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  g_free (pos);

  GST_DEBUG ("New output caps: %" GST_PTR_FORMAT, caps);

  if (!gst_pad_set_caps (faad->srcpad, caps)) {
    gst_caps_unref (caps);
    return FALSE;
  }

  *p_caps = caps;

  return TRUE;
}

static GstFlowReturn
gst_faad_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  guint input_size;
  guint skip_bytes = 0;
  guchar *input_data;
  GstFaad *faad;
  GstBuffer *outbuf;
  GstCaps *caps = NULL;
  faacDecFrameInfo info;
  void *out;
  gboolean run_loop = TRUE;

  faad = GST_FAAD (GST_OBJECT_PARENT (pad));

  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE) {
    faad->next_ts = GST_BUFFER_TIMESTAMP (buffer);
    GST_DEBUG ("Timestamp on incoming buffer: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (faad->next_ts));
  }

  /* buffer + remaining data */
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

    /* make sure we create new caps below */
    faad->samplerate = 0;
    faad->channels = 0;
  }

  /* decode cycle */
  input_data = GST_BUFFER_DATA (buffer);
  input_size = GST_BUFFER_SIZE (buffer);
  info.bytesconsumed = input_size - skip_bytes;

  if (!faad->packetised) {
    /* We must check that ourselves for raw stream */
    run_loop = (input_size >= FAAD_MIN_STREAMSIZE);
  }

  while ((input_size > 0) && run_loop) {

    if (faad->packetised) {
      /* Only one packet per buffer, no matter how much is really consumed */
      run_loop = FALSE;
    } else {
      if (input_size < FAAD_MIN_STREAMSIZE || info.bytesconsumed <= 0) {
        break;
      }
    }

    out = faacDecDecode (faad->handle, &info, input_data + skip_bytes,
        input_size - skip_bytes);
    if (info.error) {
      GST_ELEMENT_ERROR (faad, STREAM, DECODE, (NULL),
          ("Failed to decode buffer: %s", faacDecGetErrorMessage (info.error)));
      ret = GST_FLOW_ERROR;
      goto out;
    }

    if (info.bytesconsumed > input_size)
      info.bytesconsumed = input_size;
    input_size -= info.bytesconsumed;
    input_data += info.bytesconsumed;

    if (out && info.samples > 0) {
      gboolean fmt_change = FALSE;

      /* see if we need to renegotiate */
      if (info.samplerate != faad->samplerate ||
          info.channels != faad->channels || !faad->channel_positions) {
        fmt_change = TRUE;
      } else {
        gint i;

        for (i = 0; i < info.channels; i++) {
          if (info.channel_position[i] != faad->channel_positions[i])
            fmt_change = TRUE;
        }
      }

      if (fmt_change) {
        if (!gst_faad_update_caps (faad, &info, &caps)) {
          GST_ELEMENT_ERROR (faad, CORE, NEGOTIATION, (NULL),
              ("Setting caps on source pad failed"));
          ret = GST_FLOW_ERROR;
          goto out;
        }
      }

      /* play decoded data */
      if (info.samples > 0 && GST_PAD_PEER (faad->srcpad)) {
        GstFlowReturn r;
        guint bufsize = info.samples * faad->bps;

        /* note: info.samples is total samples, not per channel */
        r = gst_pad_alloc_buffer (faad->srcpad, 0, bufsize, caps, &outbuf);
        if (r != GST_FLOW_OK) {
          GST_DEBUG ("Failed to allocate buffer");
          ret = r;              //GST_FLOW_OK;    /* CHECK: or return something else? */
          goto out;
        }

        memcpy (GST_BUFFER_DATA (outbuf), out, GST_BUFFER_SIZE (outbuf));
        GST_BUFFER_OFFSET (outbuf) =
            (faad->next_ts * faad->samplerate) / GST_SECOND;
        GST_BUFFER_TIMESTAMP (outbuf) = faad->next_ts;
        GST_BUFFER_DURATION (outbuf) = (guint64) GST_SECOND *info.samples / (faad->samplerate * 2);     ///////// over 2?

        faad->next_ts += GST_BUFFER_DURATION (outbuf);
        faad->sum_dur_out += GST_BUFFER_DURATION (outbuf);

        GST_DEBUG ("pushing buffer, off=%" G_GUINT64_FORMAT ", ts=%"
            GST_TIME_FORMAT, GST_BUFFER_OFFSET (outbuf),
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
        if ((ret = gst_pad_push (faad->srcpad, outbuf)) != GST_FLOW_OK &&
            ret != GST_FLOW_NOT_LINKED)
          goto out;
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

  faad->bytes_in += input_size;

out:

  if (caps)
    gst_caps_unref (caps);

  gst_buffer_unref (buffer);

  return ret;
}

static GstStateChangeReturn
gst_faad_change_state (GstElement * element, GstStateChange transition)
{
  GstFaad *faad = GST_FAAD (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      if (!(faad->handle = faacDecOpen ()))
        return GST_STATE_CHANGE_FAILURE;
      else {
        faacDecConfiguration *conf;

        conf = faacDecGetCurrentConfiguration (faad->handle);
        conf->defObjectType = LC;
        /* conf->dontUpSampleImplicitSBR = 1; */
        conf->outputFormat = FAAD_FMT_16BIT;
        if (faacDecSetConfiguration (faad->handle, conf) == 0)
          return GST_STATE_CHANGE_FAILURE;
      }
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      faad->samplerate = -1;
      faad->channels = -1;
      faad->need_channel_setup = TRUE;
      faad->init = FALSE;
      g_free (faad->channel_positions);
      faad->channel_positions = NULL;
      faad->next_ts = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
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
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
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
