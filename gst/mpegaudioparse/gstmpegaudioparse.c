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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmpegaudioparse.h"

GST_DEBUG_CATEGORY_STATIC (mp3parse_debug);
#define GST_CAT_DEFAULT mp3parse_debug

/* elementfactory information */
static GstElementDetails mp3parse_details = {
  "MPEG1 Audio Parser",
  "Codec/Parser/Audio",
  "Parses and frames mpeg1 audio streams (levels 1-3), provides seek",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};

static GstStaticPadTemplate mp3_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) [ 8000, 48000], " "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate mp3_sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, " "mpegversion = (int) 1")
    );

/* GstMPEGAudioParse signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_SKIP,
  ARG_BIT_RATE
      /* FILL ME */
};


static void gst_mp3parse_class_init (GstMPEGAudioParseClass * klass);
static void gst_mp3parse_base_init (GstMPEGAudioParseClass * klass);
static void gst_mp3parse_init (GstMPEGAudioParse * mp3parse);

static gboolean gst_mp3parse_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_mp3parse_chain (GstPad * pad, GstBuffer * buffer);

static int head_check (GstMPEGAudioParse * mp3parse, unsigned long head);

static void gst_mp3parse_dispose (GObject * object);
static void gst_mp3parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mp3parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_mp3parse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_mp3parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mp3parse_get_type (void)
{
  static GType mp3parse_type = 0;

  if (!mp3parse_type) {
    static const GTypeInfo mp3parse_info = {
      sizeof (GstMPEGAudioParseClass),
      (GBaseInitFunc) gst_mp3parse_base_init,
      NULL,
      (GClassInitFunc) gst_mp3parse_class_init,
      NULL,
      NULL,
      sizeof (GstMPEGAudioParse),
      0,
      (GInstanceInitFunc) gst_mp3parse_init,
    };

    mp3parse_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstMPEGAudioParse", &mp3parse_info, 0);
  }
  return mp3parse_type;
}

static guint mp3types_bitrates[2][3][16] =
    { {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448,},
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384,},
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,}},
{{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,}},
};

static guint mp3types_freqs[3][3] = { {44100, 48000, 32000},
{22050, 24000, 16000},
{11025, 12000, 8000}
};

static inline guint
mp3_type_frame_length_from_header (GstMPEGAudioParse * mp3parse, guint32 header,
    guint * put_layer, guint * put_channels, guint * put_bitrate,
    guint * put_samplerate)
{
  guint length;
  gulong mode, samplerate, bitrate, layer, channels, padding;
  gint lsf, mpg25;

  if (header & (1 << 20)) {
    lsf = (header & (1 << 19)) ? 0 : 1;
    mpg25 = 0;
  } else {
    lsf = 1;
    mpg25 = 1;
  }

  mode = (header >> 6) & 0x3;
  channels = (mode == 3) ? 1 : 2;
  samplerate = (header >> 10) & 0x3;
  samplerate = mp3types_freqs[lsf + mpg25][samplerate];
  layer = 4 - ((header >> 17) & 0x3);
  bitrate = (header >> 12) & 0xF;
  bitrate = mp3types_bitrates[lsf][layer - 1][bitrate] * 1000;
  if (bitrate == 0)
    return 0;
  padding = (header >> 9) & 0x1;
  switch (layer) {
    case 1:
      length = (bitrate * 12) / samplerate + 4 * padding;
      break;
    case 2:
      length = (bitrate * 144) / samplerate + padding;
      break;
    default:
    case 3:
      length = (bitrate * 144) / (samplerate << lsf) + padding;
      break;
  }

  GST_DEBUG_OBJECT (mp3parse, "Calculated mp3 frame length of %u bytes",
      length);
  GST_DEBUG_OBJECT (mp3parse, "samplerate = %lu, bitrate = %lu, layer = %lu, "
      "channels = %lu", samplerate, bitrate, layer, channels);

  if (put_layer)
    *put_layer = layer;
  if (put_channels)
    *put_channels = channels;
  if (put_bitrate)
    *put_bitrate = bitrate;
  if (put_samplerate)
    *put_samplerate = samplerate;

  return length;
}

static GstCaps *
mp3_caps_create (guint layer, guint channels, guint bitrate, guint samplerate)
{
  GstCaps *new;

  g_assert (layer);
  g_assert (samplerate);
  g_assert (bitrate);
  g_assert (channels);

  new = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "layer", G_TYPE_INT, layer,
      "rate", G_TYPE_INT, samplerate, "channels", G_TYPE_INT, channels, NULL);

  return new;
}

static void
gst_mp3parse_base_init (GstMPEGAudioParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mp3_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&mp3_src_template));
  gst_element_class_set_details (element_class, &mp3parse_details);
}

static void
gst_mp3parse_class_init (GstMPEGAudioParseClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_mp3parse_set_property;
  gobject_class->get_property = gst_mp3parse_get_property;
  gobject_class->dispose = gst_mp3parse_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SKIP,
      g_param_spec_int ("skip", "skip", "skip",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_int ("bitrate", "Bitrate", "Bit Rate",
          G_MININT, G_MAXINT, 0, G_PARAM_READABLE));

  gstelement_class->change_state = gst_mp3parse_change_state;
}

static void
gst_mp3parse_reset (GstMPEGAudioParse * mp3parse)
{
  mp3parse->skip = 0;
  mp3parse->resyncing = TRUE;

  gst_adapter_clear (mp3parse->adapter);

  mp3parse->rate = mp3parse->channels = mp3parse->layer = -1;
}

static void
gst_mp3parse_init (GstMPEGAudioParse * mp3parse)
{
  mp3parse->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&mp3_sink_template), "sink");
  gst_pad_set_event_function (mp3parse->sinkpad, gst_mp3parse_sink_event);
  gst_pad_set_chain_function (mp3parse->sinkpad, gst_mp3parse_chain);
  gst_element_add_pad (GST_ELEMENT (mp3parse), mp3parse->sinkpad);

  mp3parse->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&mp3_src_template), "src");
  gst_pad_use_fixed_caps (mp3parse->srcpad);
  gst_element_add_pad (GST_ELEMENT (mp3parse), mp3parse->srcpad);

  mp3parse->adapter = gst_adapter_new ();

  gst_mp3parse_reset (mp3parse);
}

static void
gst_mp3parse_dispose (GObject * object)
{
  GstMPEGAudioParse *mp3parse = GST_MP3PARSE (object);

  if (mp3parse->adapter) {
    g_object_unref (mp3parse->adapter);
    mp3parse->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_mp3parse_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res;
  GstMPEGAudioParse *mp3parse;

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;

      gst_event_parse_new_segment (event, NULL, NULL, &format, NULL, NULL,
          NULL);

      if (format != GST_FORMAT_TIME)
        mp3parse->next_ts = 0;
      else
        /* we will be receiving timestamps */
        mp3parse->next_ts = -1;
      break;
    }
    default:
      break;
  }
  res = gst_pad_push_event (mp3parse->srcpad, event);

  gst_object_unref (mp3parse);

  return res;
}

static GstFlowReturn
gst_mp3parse_chain (GstPad * pad, GstBuffer * buf)
{
  GstMPEGAudioParse *mp3parse;
  const guchar *data;
  guint32 header;
  int bpf;
  GstBuffer *outbuf;
  GstClockTime timestamp;
  guint available;

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (mp3parse, "received buffer of %d bytes",
      GST_BUFFER_SIZE (buf));

  timestamp = GST_BUFFER_TIMESTAMP (buf);

  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_DEBUG_OBJECT (mp3parse, "Using incoming timestamp of %" GST_TIME_FORMAT,
        GST_TIME_ARGS (timestamp));
    mp3parse->next_ts = timestamp;
  }

  gst_adapter_push (mp3parse->adapter, buf);

  /* while we still have at least 4 bytes (for the header) available */
  while (gst_adapter_available (mp3parse->adapter) >= 4) {

    /* search for a possible start byte */
    data = gst_adapter_peek (mp3parse->adapter, 4);
    if (*data != 0xff) {
      /* It'd be nice to make this efficient, but it's ok for now; this is only
       * when resyncing
       */
      mp3parse->resyncing = TRUE;
      gst_adapter_flush (mp3parse->adapter, 1);
      continue;
    }

    available = gst_adapter_available (mp3parse->adapter);

    /* construct the header word */
    header = GST_READ_UINT32_BE (data);
    /* if it's a valid header, go ahead and send off the frame */
    if (head_check (mp3parse, header)) {
      guint bitrate = 0, layer = 0, rate = 0, channels = 0;

      if (!(bpf = mp3_type_frame_length_from_header (mp3parse, header, &layer,
                  &channels, &bitrate, &rate))) {
        g_error ("Header failed internal error");
      }

      /*************************************************************************
      * robust seek support
      * - This performs additional frame validation if the resyncing flag is set
      *   (indicating a discontinuous stream).
      * - The current frame header is not accepted as valid unless the NEXT 
      *   frame header has the same values for most fields.  This significantly
      *   increases the probability that we aren't processing random data.
      * - It is not clear if this is sufficient for robust seeking of Layer III
      *   streams which utilize the concept of a "bit reservoir" by borrowing
      *   bitrate from previous frames.  In this case, seeking may be more 
      *   complicated because the frames are not independently coded.
      *************************************************************************/
      if (mp3parse->resyncing) {
        guint32 header2;
        const guint8 *data2;

        /* wait until we have the the entire current frame as well as the next 
         * frame header */
        if (available < bpf + 4)
          break;

        data2 = gst_adapter_peek (mp3parse->adapter, bpf + 4);
        header2 = GST_READ_UINT32_BE (data2 + bpf);
        GST_DEBUG_OBJECT (mp3parse, "header=%08X, header2=%08X, bpf=%d",
            (unsigned int) header, (unsigned int) header2, bpf);

/* mask the bits which are allowed to differ between frames */
#define HDRMASK ~((0xF << 12)  /* bitrate */ | \
                  (0x1 <<  9)  /* padding */ | \
                  (0x3 <<  4))  /* mode extension */

        /* require 2 matching headers in a row */
        if ((header2 & HDRMASK) != (header & HDRMASK)) {
          GST_DEBUG_OBJECT (mp3parse, "next header doesn't match "
              "(header=%08X, header2=%08X, bpf=%d)",
              (unsigned int) header, (unsigned int) header2, bpf);
          /* This frame is invalid.  Start looking for a valid frame at the 
           * next position in the stream */
          mp3parse->resyncing = TRUE;
          gst_adapter_flush (mp3parse->adapter, 1);
          continue;
        }
      }

      /* if we don't have the whole frame... */
      if (available < bpf) {
        GST_DEBUG_OBJECT (mp3parse, "insufficient data available, need "
            "%d bytes, have %d", bpf, available);
        break;
      } else {
        if (channels != mp3parse->channels ||
            rate != mp3parse->rate ||
            layer != mp3parse->layer || bitrate != mp3parse->bit_rate) {
          GstCaps *caps;

          caps = mp3_caps_create (layer, channels, bitrate, rate);
          gst_pad_set_caps (mp3parse->srcpad, caps);
          gst_caps_unref (caps);

          mp3parse->channels = channels;
          mp3parse->layer = layer;
          mp3parse->rate = rate;
          mp3parse->bit_rate = bitrate;
        }

        outbuf = gst_adapter_take_buffer (mp3parse->adapter, bpf);

        if (!mp3parse->skip) {
          gint spf;             /* samples per frame */

          mp3parse->resyncing = FALSE;

          GST_DEBUG_OBJECT (mp3parse, "pushing buffer of %d bytes",
              GST_BUFFER_SIZE (outbuf));

          GST_BUFFER_TIMESTAMP (outbuf) = mp3parse->next_ts;

          /* see http://www.codeproject.com/audio/MPEGAudioInfo.asp */
          if (mp3parse->layer == 1)
            spf = 384;
          else if (mp3parse->layer == 2)
            spf = 1152;
          else {
            if (mp3parse->rate < 16000)
              spf = 576;
            else
              spf = 1152;
          }
          GST_BUFFER_DURATION (outbuf) = spf * GST_SECOND / mp3parse->rate;

          mp3parse->next_ts += GST_BUFFER_DURATION (outbuf);

          gst_buffer_set_caps (outbuf, GST_PAD_CAPS (mp3parse->srcpad));

          gst_pad_push (mp3parse->srcpad, outbuf);

        } else {
          GST_DEBUG_OBJECT (mp3parse, "skipping buffer of %d bytes",
              GST_BUFFER_SIZE (outbuf));
          gst_buffer_unref (outbuf);
          mp3parse->skip--;
        }
      }
    } else {
      mp3parse->resyncing = TRUE;
      gst_adapter_flush (mp3parse->adapter, 1);
      GST_DEBUG_OBJECT (mp3parse, "wrong header, skipping byte");
    }
  }

  gst_object_unref (mp3parse);

  return GST_FLOW_OK;
}

static gboolean
head_check (GstMPEGAudioParse * mp3parse, unsigned long head)
{
  GST_DEBUG_OBJECT (mp3parse, "checking mp3 header 0x%08lx", head);
  /* if it's not a valid sync */
  if ((head & 0xffe00000) != 0xffe00000) {
    GST_DEBUG_OBJECT (mp3parse, "invalid sync");
    return FALSE;
  }
  /* if it's an invalid MPEG version */
  if (((head >> 19) & 3) == 0x1) {
    GST_DEBUG_OBJECT (mp3parse, "invalid MPEG version");
    return FALSE;
  }
  /* if it's an invalid layer */
  if (!((head >> 17) & 3)) {
    GST_DEBUG_OBJECT (mp3parse, "invalid layer");
    return FALSE;
  }
  /* if it's an invalid bitrate */
  if (((head >> 12) & 0xf) == 0x0) {
    GST_DEBUG_OBJECT (mp3parse, "invalid bitrate");
    return FALSE;
  }
  if (((head >> 12) & 0xf) == 0xf) {
    GST_DEBUG_OBJECT (mp3parse, "invalid bitrate");
    return FALSE;
  }
  /* if it's an invalid samplerate */
  if (((head >> 10) & 0x3) == 0x3) {
    GST_DEBUG_OBJECT (mp3parse, "invalid samplerate");
    return FALSE;
  }
  if ((head & 0xffff0000) == 0xfffe0000) {
    GST_DEBUG_OBJECT (mp3parse, "invalid sync");
    return FALSE;
  }
  if (head & 0x00000002) {
    GST_DEBUG_OBJECT (mp3parse, "invalid emphasis");
    return FALSE;
  }

  return TRUE;
}

static void
gst_mp3parse_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMPEGAudioParse *src;

  g_return_if_fail (GST_IS_MP3PARSE (object));
  src = GST_MP3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_mp3parse_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPEGAudioParse *src;

  g_return_if_fail (GST_IS_MP3PARSE (object));
  src = GST_MP3PARSE (object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    case ARG_BIT_RATE:
      g_value_set_int (value, src->bit_rate * 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_mp3parse_change_state (GstElement * element, GstStateChange transition)
{
  GstMPEGAudioParse *mp3parse;
  GstStateChangeReturn result;

  mp3parse = GST_MP3PARSE (element);

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_mp3parse_reset (mp3parse);
      break;
    default:
      break;
  }


  return result;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mp3parse_debug, "mp3parse", 0, "MP3 Parser");

  return gst_element_register (plugin, "mp3parse",
      GST_RANK_NONE, GST_TYPE_MP3PARSE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegaudioparse",
    "MPEG-1 layer 1/2/3 audio parser",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
