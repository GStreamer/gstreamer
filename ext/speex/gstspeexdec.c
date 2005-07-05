/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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
#  include "config.h"
#endif

#include "gstspeexdec.h"
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY (speexdec_debug);
#define GST_CAT_DEFAULT speexdec_debug

static GstElementDetails speex_dec_details = {
  "SpeexDec",
  "Codec/Decoder/Audio",
  "decode speex streams to audio",
  "Wim Taymans <wim@fluendo.com>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_ENH             TRUE

enum
{
  ARG_0,
  ARG_ENH
};

static GstStaticPadTemplate speex_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate speex_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex")
    );

GST_BOILERPLATE (GstSpeexDec, gst_speex_dec, GstElement, GST_TYPE_ELEMENT);

static void speex_dec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn speex_dec_change_state (GstElement * element);
static const GstFormat *speex_dec_get_formats (GstPad * pad);

static gboolean speex_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean speex_dec_src_query (GstPad * pad,
    GstQueryType query, GstFormat * format, gint64 * value);
static gboolean speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static void gst_speexdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speexdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void
gst_speex_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&speex_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&speex_dec_sink_factory));
  gst_element_class_set_details (element_class, &speex_dec_details);
}

static void
gst_speex_dec_class_init (GstSpeexDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ENH,
      g_param_spec_boolean ("enh", "Enh", "Enable perceptual enhancement",
          DEFAULT_ENH, G_PARAM_READWRITE));

  gstelement_class->change_state = speex_dec_change_state;

  gobject_class->set_property = gst_speexdec_set_property;
  gobject_class->get_property = gst_speexdec_get_property;

  GST_DEBUG_CATEGORY_INIT (speexdec_debug, "speexdec", 0,
      "speex decoding element");
}

static const GstFormat *
speex_dec_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* samples in the audio case */
    GST_FORMAT_TIME,
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,         /* samples */
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}

static const GstEventMask *
speex_get_event_masks (GstPad * pad)
{
  static const GstEventMask speex_dec_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return speex_dec_src_event_masks;
}

static const GstQueryType *
speex_get_query_types (GstPad * pad)
{
  static const GstQueryType speex_dec_src_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return speex_dec_src_query_types;
}

static void
gst_speex_dec_init (GstSpeexDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&speex_dec_sink_factory), "sink");
  gst_pad_set_chain_function (dec->sinkpad, speex_dec_chain);
  gst_pad_set_formats_function (dec->sinkpad, speex_dec_get_formats);
  gst_pad_set_convert_function (dec->sinkpad, speex_dec_convert);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&speex_dec_src_factory), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_pad_set_event_mask_function (dec->srcpad, speex_get_event_masks);
  gst_pad_set_event_function (dec->srcpad, speex_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, speex_get_query_types);
  gst_pad_set_query_function (dec->srcpad, speex_dec_src_query);
  gst_pad_set_formats_function (dec->srcpad, speex_dec_get_formats);
  gst_pad_set_convert_function (dec->srcpad, speex_dec_convert);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->enh = DEFAULT_ENH;

  GST_FLAG_SET (dec, GST_ELEMENT_EVENT_AWARE);
}

static gboolean
speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexDec *dec;
  guint64 scale = 1;

  dec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  if (dec->packetno < 1)
    return FALSE;

  if (pad == dec->sinkpad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (float) * dec->header->nb_channels;
        case GST_FORMAT_DEFAULT:
          *dest_value = scale * (src_value * dec->header->rate / GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * sizeof (float) * dec->header->nb_channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / dec->header->rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (sizeof (float) * dec->header->nb_channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND /
              (dec->header->rate * sizeof (float) * dec->header->nb_channels);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
speex_dec_src_query (GstPad * pad, GstQueryType query, GstFormat * format,
    gint64 * value)
{
  gint64 samples_out = 0;
  GstSpeexDec *dec = GST_SPEEXDEC (gst_pad_get_parent (pad));
  GstFormat my_format = GST_FORMAT_DEFAULT;

  if (query == GST_QUERY_POSITION) {
    samples_out = dec->samples_out;
  } else {
    /* query peer in default format */
    if (!gst_pad_query (GST_PAD_PEER (dec->sinkpad), query, &my_format,
            &samples_out))
      return FALSE;
  }

  /* and convert to the final format */
  if (!gst_pad_convert (pad, GST_FORMAT_DEFAULT, samples_out, format, value))
    return FALSE;

  GST_LOG_OBJECT (dec,
      "query %u: peer returned samples_out: %llu - we return %llu (format %u)\n",
      query, samples_out, *value, *format);
  return TRUE;
}

static gboolean
speex_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSpeexDec *dec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      guint64 value;
      GstFormat my_format = GST_FORMAT_DEFAULT;

      /* convert to samples_out */
      res = speex_dec_convert (pad, GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &my_format, &value);
      if (res) {
        GstEvent *real_seek = gst_event_new_seek (
            (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) |
            GST_FORMAT_DEFAULT,
            value);

        res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      }
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static void
speex_dec_event (GstSpeexDec * dec, GstEvent * event)
{
  guint64 value, time, bytes;

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT,
              (gint64 *) & value)) {
        dec->samples_out = value;
        GST_DEBUG_OBJECT (dec,
            "setting samples_out to %" G_GUINT64_FORMAT " after discont",
            value);
      } else {
        GST_WARNING_OBJECT (dec,
            "discont event didn't include offset, we might set it wrong now");
      }
      if (dec->packetno < 2) {
        if (dec->samples_out != 0)
          GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
              ("can't handle discont before parsing first 2 packets"));
        dec->packetno = 0;
        gst_pad_push (dec->srcpad, GST_DATA (gst_event_new_discontinuous (FALSE,
                    GST_FORMAT_TIME, (guint64) 0, GST_FORMAT_DEFAULT,
                    (guint64) 0, GST_FORMAT_BYTES, (guint64) 0, 0)));
      } else {
        GstFormat time_format, default_format, bytes_format;

        time_format = GST_FORMAT_TIME;
        default_format = GST_FORMAT_DEFAULT;
        bytes_format = GST_FORMAT_BYTES;

        dec->packetno = 2;
        /* if one of them works, all of them work */
        if (speex_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->samples_out, &time_format, &time)
            && speex_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->samples_out, &bytes_format, &bytes)) {
          gst_pad_push (dec->srcpad,
              GST_DATA (gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                      time, GST_FORMAT_DEFAULT, dec->samples_out,
                      GST_FORMAT_BYTES, bytes, 0)));
        } else {
          GST_ERROR_OBJECT (dec,
              "failed to parse data for DISCONT event, not sending any");
        }
      }
      gst_data_unref (GST_DATA (event));
      break;
    default:
      gst_pad_event_default (dec->sinkpad, event);
      break;
  }
}

static void
speex_dec_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf;
  GstSpeexDec *dec;

  dec = GST_SPEEXDEC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    speex_dec_event (dec, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);

  if (dec->packetno == 0) {
    GstCaps *caps;

    /* get the header */
    dec->header =
        speex_packet_to_header (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
    gst_data_unref (data);
    if (!dec->header) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL), ("couldn't read header"));
      return;
    }
    if (dec->header->mode >= SPEEX_NB_MODES) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL),
          ("Mode number %d does not (yet/any longer) exist in this version",
              dec->header->mode));
      return;
    }

    dec->mode = (SpeexMode *) speex_mode_list[dec->header->mode];

    /* initialize the decoder */
    dec->state = speex_decoder_init (dec->mode);
    if (!dec->state) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL), ("couldn't initialize decoder"));
      gst_data_unref (data);
      return;
    }

    speex_decoder_ctl (dec->state, SPEEX_SET_ENH, &dec->enh);
    speex_decoder_ctl (dec->state, SPEEX_GET_FRAME_SIZE, &dec->frame_size);

    if (dec->header->nb_channels != 1) {
      dec->callback.callback_id = SPEEX_INBAND_STEREO;
      dec->callback.func = speex_std_stereo_request_handler;
      dec->callback.data = &dec->stereo;
      speex_decoder_ctl (dec->state, SPEEX_SET_HANDLER, &dec->callback);
    }

    speex_decoder_ctl (dec->state, SPEEX_SET_SAMPLING_RATE, &dec->header->rate);

    speex_bits_init (&dec->bits);

    /* set caps */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->header->rate,
        "channels", G_TYPE_INT, dec->header->nb_channels,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

    if (!gst_pad_set_explicit_caps (dec->srcpad, caps)) {
      gst_caps_free (caps);
      return;
    }
    gst_caps_free (caps);
  } else if (dec->packetno == 1) {
    gchar *encoder = NULL;

    /* FIXME parse comments */
    GstTagList *list = gst_tag_list_from_vorbiscomment_buffer (buf, "", 1,
        &encoder);

    gst_data_unref (data);

    if (!list) {
      GST_WARNING_OBJECT (dec, "couldn't decode comments");
      list = gst_tag_list_new ();
    }
    if (encoder) {
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_ENCODER, encoder, NULL);
      g_free (encoder);
    }
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, "Speex", NULL);
    /*
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_ENCODER_VERSION, dec->vi.version, NULL);

       if (dec->vi.bitrate_upper > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
       if (vd->vi.bitrate_nominal > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
       if (vd->vi.bitrate_lower > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
     */
    gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, 0, list);
  } else {
    gint i;

    /* send data to the bitstream */
    speex_bits_read_from (&dec->bits, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    gst_data_unref (data);

    /* now decode each frame */
    for (i = 0; i < dec->header->frames_per_packet; i++) {
      gint ret;
      GstBuffer *outbuf;
      gint16 *out_data;

      ret = speex_decode (dec->state, &dec->bits, dec->output);
      if (ret == -1) {
        /* uh? end of stream */
        GST_WARNING_OBJECT (dec, "Unexpected end of stream found");
        break;
      } else if (ret == -2) {
        GST_WARNING_OBJECT (dec, "Decoding error: corrupted stream?");
        break;
      }
      if (speex_bits_remaining (&dec->bits) < 0) {
        GST_WARNING_OBJECT (dec, "Decoding overflow: corrupted stream?");
        break;
      }
      if (dec->header->nb_channels == 2)
        speex_decode_stereo (dec->output, dec->frame_size, &dec->stereo);

      outbuf = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
          dec->frame_size * dec->header->nb_channels * 2);
      out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

      /*PCM saturation (just in case) */
      for (i = 0; i < dec->frame_size * dec->header->nb_channels; i++) {
        if (dec->output[i] > 32767.0)
          out_data[i] = 32767;
        else if (dec->output[i] < -32768.0)
          out_data[i] = -32768;
        else
          out_data[i] = (gint16) dec->output[i];
      }

      GST_BUFFER_OFFSET (outbuf) = dec->samples_out;
      GST_BUFFER_OFFSET_END (outbuf) = dec->samples_out + dec->frame_size;
      GST_BUFFER_TIMESTAMP (outbuf) =
          dec->samples_out * GST_SECOND / dec->header->rate;
      GST_BUFFER_DURATION (outbuf) =
          dec->frame_size * GST_SECOND / dec->header->rate;
      gst_pad_push (dec->srcpad, GST_DATA (outbuf));
      dec->samples_out += dec->frame_size;
    }
  }
  dec->packetno++;
}

static void
gst_speexdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  g_return_if_fail (GST_IS_SPEEXDEC (object));

  speexdec = GST_SPEEXDEC (object);

  switch (prop_id) {
    case ARG_ENH:
      g_value_set_boolean (value, speexdec->enh);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_speexdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  g_return_if_fail (GST_IS_SPEEXDEC (object));

  speexdec = GST_SPEEXDEC (object);

  switch (prop_id) {
    case ARG_ENH:
      speexdec->enh = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
speex_dec_change_state (GstElement * element)
{
  GstSpeexDec *vd = GST_SPEEXDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      vd->packetno = 0;
      vd->samples_out = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return parent_class->change_state (element);
}
